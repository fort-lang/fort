#!/usr/bin/env python3
"""tools/fir_diff.py: the oracle of the FIR migration (spec/fir.md 16.2).

usage: fir_diff.py [--llvm-diff PATH] [--jobs N] [--list] <before> <after> <out>

<before> and <after> are two snapshots of tools/ir_snapshot.sh: the direct path's and the FIR
path's. Both must use one <std-dir> path (notes/testing.md 6).

For each module of each mode, the script runs llvm-diff and writes its report to
<out>/llvm-diff/<mode>/<name>.txt. llvm-diff-18 alone is no oracle: it reports none of a dropped
`zeroext`, a changed `align` and a dropped `sret`, and its report is no record of instructions.
So the script compares every definition whose text differs, and each function that llvm-diff
names must be one of them. It reads a definition as follows.

- The rewrites of spec/fir.md 12.5 run first, on both texts: `result`, `slot`, `text`, `copy`
  and `argument`. The list ITEMS says what each one accepts, and the docstring of each rewrite
  gives its conditions.
- The control flow graph is then normalized: a block that holds only `br label` is skipped, a
  block that no path from `entry` reaches is dropped, a block that ends in `br label` to a block
  with no other predecessor absorbs it, and the cases of a `switch` stand in value order. Each
  block takes the label `B<n>` of its place in a walk from `entry` over the successors in operand
  order, so two texts whose blocks are numbered apart but branch alike give each block one
  label, and two texts that swap two targets do not.
- Each block is the sequence of its writes (`store`, `llvm.memcpy`, `llvm.memset`, every other
  `call`) and its terminator, in order. An operand that is a register stands as the expression
  that defines it, with its type, its alignment and its attributes. A `load` carries the writes
  that can reach it and can write its storage: the last such write before it in its block, or
  the set of such writes at the ends of the predecessors, from `entry` on. So a load may move
  past a write into other storage, and not past a write into its own. Two storages are one
  unless both are named objects (allocas, `%tmp` slots, parameters, `%ret.sret`, globals), or
  one is a local whose address has not escaped at the load and the other a pointer that a load
  gave. A call writes every storage except a local whose address does not escape and that the
  call does not name; a print entry of std.rt and its string equality write no global of
  another module. A call result stands as the place of the call, a `%tmp` slot as the order in
  which the walk first writes it, a named local as its name, a `@.str.N` or `@.file.N` as its
  bytes.
- A run of consecutive stores, memcpys and memsets into one storage stands as its effect, the
  value that each field holds after the run, when the two texts differ in that run: `zero`
  accepts a memset and stores of zero with one effect. A memset or a memcpy fits the size and
  the alignment of the type it writes. The padding of each struct that the run splits into
  fields is one more value of the effect, which only a memset or a memcpy of the struct or of a
  struct around it writes. A load inside a run names the write of the run that reaches it by
  its path, by the count of writes to that path before it and by the value that it writes.
- When a definition does not agree, a second pass moves each store, memset and memcpy into a
  local or into `%ret.sret` down to the first item that can read or write its storage
  (`aggregate`). It passes a call only when the storage is a local whose address does not escape
  and that the call does not name, and a check branch only when the failure block does not read
  the storage and its call passes no pointer but a global constant: the entry of a check kind of
  std.rt or `llvm.trap`, and never `panic`, whose message can point into a local. It rests on
  the fact that such an entry reads no storage of the function and no aggregate result.
- A run of the FIR path may zero padding that the direct path leaves as it was (`padding`): the
  FIR lowering zeroes the temporary of each member of a literal and of each cast operand whose
  type has padding (spec/fir.md 9.4), and the direct path builds that value in its destination.
  The reverse, padding that the direct path zeroes and the FIR path does not, is a difference.
  So when `copy` writes such a temporary in place, the FIR side drops its first whole memset if
  a call that takes the slot as its `sret` pointer or a whole memcpy writes the slot on every
  path from the memset to the memcpy; else the memset writes the destination. `%ret.sret` and
  a global can be one object, because the caller can pass a global as the `sret` pointer
  (`g = f()`).
- A definition agrees when its allocas and each of its blocks agree. Its difference is counted
  under each item that changed it, and under `order` when no rewrite was needed. A definition
  that does not agree is unclassified, and <out>/unclassified.txt gives its lines of each side
  that the other side lacks. <out>/classified.txt lists the classified ones with --list.
- The lines outside the definitions agree when the two multisets of lines are equal, with each
  `@.str.N` and `@.file.N` written as its bytes: only their order may differ. A module that
  stands in one snapshot only is unclassified.

The script prints the counts and exits 0 when no difference is unclassified, 1 when one is, and
2 for a usage error.
"""

import argparse
import collections
import concurrent.futures
import os
import re
import subprocess
import sys

MODES = ("default", "release", "nobounds")

# A register, a slot, a label, a parameter or a global in an instruction.
TOKEN = re.compile(r'%[A-Za-z0-9_.]+|@"[^"]*"|@[A-Za-z0-9_.$]+')
REGISTER = re.compile(r"^%t\d+$")
SLOT = re.compile(r"^%tmp\d+$")

STORE = re.compile(r"^store (\S+) (\S+), ptr (\S+), align (\d+)$")
LOAD = re.compile(r"^load (\S+), ptr (\S+), align (\d+)$")
MEMCPY = re.compile(
    r"^call void @llvm\.memcpy\.p0\.p0\.i64\(ptr align (\d+) (\S+), "
    r"ptr align (\d+) (\S+), i64 (\d+), i1 false\)$"
)
MEMSET = re.compile(
    r"^call void @llvm\.memset\.p0\.i64\(ptr align (\d+) (\S+), i8 0, "
    r"i64 (\d+), i1 false\)$"
)
# A call of a print entry of std.rt or of its string equality: it reads its arguments and writes
# the buffers of std.rt, and no global of another module (std/rt.ft).
QUIET_CALL = re.compile(r'^(?:%t\d+ = )?call [^@]*@"std\.rt\.(?:print_[a-z0-9_]+|str_eq)"\(')
PURE_CALL = re.compile(r"^call .*@llvm\.(?:[su](?:add|sub|mul)\.with\.overflow|fpto[su]i\.sat)\.")
ZERO = re.compile(r"^(0|null|false|zeroinitializer|0\.000000e\+00|0x0+)$")

# The items of spec/fir.md 12.5 that the script accepts, in the order it prints them.
ITEMS = (
    ("result", "a scalar _0 is the alloca %result: its store is the ret that loads it"),
    ("copy", "an aggregate value goes through a %tmp slot: one whole memcpy more or less"),
    ("argument", "an aggregate argument that is a place is copied right before its call"),
    ("slot", "a branch on a one-byte slot that its block has just stored loads it again"),
    ("text", "a string constant operand of `==` is a %tmp span that str_eq loads"),
    (
        "aggregate",
        "a store into a local or the aggregate result stands after the reads, "
        "calls and checks that cannot read its storage",
    ),
    ("zero", "a memset against stores of zero in its fields, padding the same"),
    ("padding", "the FIR path zeroes padding that the direct path leaves as it was"),
    ("span", "the two tests of a span check stand in the other order"),
    (
        "order",
        "only reads that cross no write into their storage move, and the register, "
        "block, %tmp and data numbers differ",
    ),
    ("declarations", "modules whose lines outside the definitions differ in order alone"),
)


# ---- the module --------------------------------------------------------------------------------


def split_types(text):
    """The comma-separated types of a struct body, brackets and braces kept whole."""
    out = []
    depth = 0
    cur = ""
    for ch in text:
        if ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def read_module(path):
    """The function bodies of the module at `path` by name, the bytes of its data, and its named
    types."""
    funcs = {}
    data = {}
    types = {}
    name = None
    body = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("define "):
                m = re.search(r'@("[^"]+"|[A-Za-z0-9_.]+)\(', line)
                name = m.group(1).strip('"')
                body = [line]
            elif name is not None:
                body.append(line)
                if line == "}":
                    funcs[name] = body
                    name = None
            else:
                m = re.match(
                    r"^(@\.(?:str|file)\.\d+) = private unnamed_addr constant "
                    r'\[\d+ x i8\] (c".*"), align 1$',
                    line,
                )
                if m:
                    data[m.group(1)] = m.group(2)
                m = re.match(r"^(%[A-Za-z0-9_.]+) = type \{ (.*) \}$", line)
                if m:
                    types[m.group(1)] = split_types(m.group(2))
    return funcs, data, types


def layout(ty, types):
    """The size and the alignment of the LLVM type `ty` on both targets, or None."""
    scalars = {"i1": 1, "i8": 1, "i16": 2, "i32": 4, "i64": 8, "ptr": 8, "float": 4, "double": 8}
    if ty in scalars:
        return scalars[ty], scalars[ty]
    m = re.match(r"^\[(\d+) x (.+)\]$", ty)
    if m:
        inner = layout(m.group(2), types)
        if inner is None:
            return None
        return int(m.group(1)) * inner[0], inner[1]
    if ty in types:
        size = 0
        align = 1
        for field in types[ty]:
            fl = layout(field, types)
            if fl is None:
                return None
            size = (size + fl[1] - 1) // fl[1] * fl[1] + fl[0]
            align = max(align, fl[1])
        return (size + align - 1) // align * align, align
    return None


def padded(ty, types):
    """Whether the LLVM type `ty` holds padding bytes: between two fields, after the last field,
    or inside a field or an element. An unknown type counts as padded."""
    lay = layout(ty, types)
    if lay is None:
        return True
    m = re.match(r"^\[(\d+) x (.+)\]$", ty)
    if m:
        return padded(m.group(2), types)
    if ty not in types:
        return False
    offset = 0
    for field in types[ty]:
        fl = layout(field, types)
        if fl is None or offset % fl[1] != 0 or padded(field, types):
            return True
        offset += fl[0]
    return offset != lay[0]


def own_padding(ty, types):
    """Whether the struct type `ty` holds padding bytes of its own, between two fields or after
    the last field, apart from the padding inside a field. An array has none of its own, and a
    struct with a field of unknown layout counts as padded."""
    if ty not in types:
        return False
    lay = layout(ty, types)
    if lay is None:
        return True
    offset = 0
    for field in types[ty]:
        fl = layout(field, types)
        if fl is None or offset % fl[1] != 0:
            return True
        offset += fl[0]
    return offset != lay[0]


def children(ty, types):
    """The member types of an aggregate type, or None for a scalar or an unknown type."""
    m = re.match(r"^\[(\d+) x (.+)\]$", ty)
    if m:
        return [m.group(2)] * int(m.group(1))
    return types.get(ty)


def blocks_of(body):
    """The blocks of a definition: its label and its instructions, a `switch` on one line."""
    blocks = []
    cur = None
    pending = None
    for line in body[1:]:
        if line == "}":
            break
        if line == "":
            continue
        m = re.match(r"^(entry|L\d+):$", line)
        if m:
            cur = ["%" + m.group(1), []]
            blocks.append(cur)
            continue
        if cur is None:
            cur = ["%entry", []]
            blocks.append(cur)
        s = line.strip()
        if pending is not None:
            pending += " " + s
            if s == "]":
                cur[1].append(pending)
                pending = None
            continue
        if s.startswith("switch ") and s.endswith("["):
            pending = s
            continue
        cur[1].append(s)
    return blocks


def successors(term):
    """The targets of a terminator, in operand order: a `switch` gives its default first."""
    return re.findall(r"label (%[A-Za-z0-9_.]+)", term)


def is_terminator(ins):
    return ins.startswith(("br ", "switch ", "ret ", "unreachable"))


def rhs_of(ins):
    m = re.match(r"^(%[A-Za-z0-9_.]+) = (.*)$", ins)
    return (m.group(1), m.group(2)) if m else (None, ins)


def is_write(ins):
    """Whether `ins` writes memory: a store or a call that is no pure intrinsic."""
    _, rhs = rhs_of(ins)
    if rhs.startswith("store "):
        return True
    return rhs.startswith("call ") and not PURE_CALL.match(rhs)


def gep_parts(rhs):
    """The type, the base and the indices of a `getelementptr inbounds`, or None."""
    if not rhs.startswith("getelementptr inbounds "):
        return None
    rest = rhs[len("getelementptr inbounds ") :]
    depth = 0
    for i, ch in enumerate(rest):
        if ch in "[{":
            depth += 1
        elif ch in "]}":
            depth -= 1
        elif ch == "," and depth == 0:
            ty = rest[:i]
            m = re.match(r"^, ptr (\S+), (.*)$", rest[i:])
            if not m:
                return None
            return ty, m.group(1), m.group(2)
    return None


# ---- one definition ----------------------------------------------------------------------------


class Function:
    """One definition as blocks of instructions, with the rewrites of the items, the normalized
    graph and the events."""

    def __init__(self, body, data, types):
        self.define = body[0]
        self.data = data
        self.types = types
        self.blocks = blocks_of(body)
        self.fired = set()
        # the FIR path's text, the <after> snapshot: the item `padding` reads it alone
        self.after = False
        m = re.search(r"ptr sret\(([^)]*)\) %ret\.sret", self.define)
        self.sret = m.group(1) if m else None

    def instructions(self):
        for _, ins in self.blocks:
            yield from ins

    def replace(self, fn):
        """Replaces each instruction `i` by the list fn(i)."""
        for block in self.blocks:
            out = []
            for i in block[1]:
                out.extend(fn(i))
            block[1] = out

    def defs(self):
        d = {}
        for i in self.instructions():
            reg, rhs = rhs_of(i)
            if reg:
                d[reg] = rhs
        return d

    def predecessors(self):
        preds = collections.defaultdict(list)
        for name, ins in self.blocks:
            if ins:
                for s in successors(ins[-1]):
                    preds[s].append(name)
        return preds

    def root_path(self, addr, defs):
        """The storage that the address `addr` points into, and the path of constant indices
        from it, or None for the path when an index is no constant."""
        path = []
        while REGISTER.match(addr):
            parts = gep_parts(defs.get(addr, ""))
            if not parts:
                break
            ty, base, idx = parts
            m = re.match(r"^(?:i32|i64) 0, (?:i32|i64) (\d+)$", idx)
            if m and path is not None:
                path.insert(0, (int(m.group(1)), ty))
            else:
                path = None
            addr = base
        return addr, path

    def type_of_root(self, root, defs):
        rhs = defs.get(root, "")
        m = re.match(r"^alloca (.+), align \d+$", rhs)
        if m:
            return m.group(1)
        if root == "%ret.sret":
            return self.sret
        return None

    # ---- the rewrites ------------------------------------------------------------------------

    def rewrite_result(self):
        """`result`: each `ret` of a load of `%result` returns the value of the one store into
        `%result` that reaches it on every path, and the alloca, the stores and the loads of
        `%result` go. The alloca, each store into it and each load of it must have the alignment
        of its type. A `bool` result stores `zext i1 v to i8` and returns the `trunc` of the
        load."""
        allocas = [i for i in self.instructions() if i.startswith("%result = alloca ")]
        if not allocas:
            return
        am = re.match(r"^%result = alloca (.+), align (\d+)$", allocas[0])
        lay = layout(am.group(1), self.types)
        if lay is None or str(lay[1]) != am.group(2):
            # the alloca must have the alignment of its type
            return
        align = am.group(2)
        defs = self.defs()
        loaded = {}
        for reg, rhs in defs.items():
            ld = re.match(r"^load [^,]+, ptr %result, align (\d+)$", rhs)
            if ld and ld.group(1) != align:
                return
            if ld:
                loaded[reg] = False
        for reg, rhs in defs.items():
            m = re.match(r"^trunc i8 (%t\d+) to i1$", rhs)
            if m and m.group(1) in loaded:
                loaded[reg] = True
        preds = self.predecessors()
        byname = {name: ins for name, ins in self.blocks}

        def stores_before(block, at):
            """The stores into `%result` that reach position `at` of `block`, or None when a
            path from `entry` reaches it with no store."""
            found = set()
            stack = [(block, at)]
            seen = set()
            while stack:
                b, k = stack.pop()
                hit = None
                for j in range(k - 1, -1, -1):
                    st = STORE.match(byname[b][j])
                    if st and st.group(3) == "%result":
                        hit = (b, j)
                        break
                if hit:
                    found.add(hit)
                    continue
                if b == "%entry" or not preds[b]:
                    return None
                for pb in preds[b]:
                    if pb not in seen:
                        seen.add(pb)
                        stack.append((pb, len(byname[pb])))
            return found

        rets = {}
        for name, ins in self.blocks:
            for k, i in enumerate(ins):
                m = re.match(r"^ret (\S+) (%t\d+)$", i)
                if not m or m.group(2) not in loaded:
                    continue
                found = stores_before(name, k)
                if not found or len(found) != 1:
                    return
                b, j = found.pop()
                st = STORE.match(byname[b][j])
                ty, v = st.group(1), st.group(2)
                if st.group(4) != align:
                    return
                z = re.match(r"^zext i1 (\S+) to i8$", defs.get(v, ""))
                if ty == "i8" and z and loaded[m.group(2)]:
                    rets[(name, k)] = "ret i1 " + z.group(1)
                elif ty == m.group(1):
                    rets[(name, k)] = "ret %s %s" % (ty, v)
                else:
                    return
        for i in self.instructions():
            st = STORE.match(i)
            if "%result" in TOKEN.findall(i) and not (
                i.startswith("%result = alloca ")
                or st
                and st.group(3) == "%result"
                and st.group(2) != "%result"
                or rhs_of(i)[0] in loaded
            ):
                return
        for block in self.blocks:
            out = []
            for k, i in enumerate(block[1]):
                st = STORE.match(i)
                if (block[0], k) in rets:
                    out.append(rets[(block[0], k)])
                elif i.startswith("%result = alloca ") or rhs_of(i)[0] in loaded:
                    continue
                elif st and st.group(3) == "%result":
                    continue
                else:
                    out.append(i)
            block[1] = out
        self.fired.add("result")

    def rewrite_slot(self):
        """`slot`: a load of a one-byte `%tmp` slot after a store into it in the same block, with
        no write between, is the stored value, so a branch on the `&&`, `||` or `?:` slot that
        the block has just stored reads that value."""
        defs = self.defs()
        for block in self.blocks:
            last = {}
            out = []
            for i in block[1]:
                m = re.match(r"^store i8 (%t\d+), ptr (%tmp\d+), align 1$", i)
                if m:
                    last = {m.group(2): m.group(1)}
                elif is_write(i):
                    last = {}
                m = re.match(r"^(%t\d+) = load i8, ptr (%tmp\d+), align 1$", i)
                if m and m.group(2) in last:
                    z = re.match(r"^zext i1 (\S+) to i8$", defs.get(last[m.group(2)], ""))
                    if z:
                        out.append("%s = zext i1 %s to i8" % (m.group(1), z.group(1)))
                        self.fired.add("slot")
                        continue
                out.append(i)
            block[1] = out

    def rewrite_text(self):
        """`text`: a `%tmp` span whose two fields hold a `@.str.N` constant and its length, and
        that only loads of those fields read, is those two constants."""
        ins = list(self.instructions())
        for slot in [
            m.group(1)
            for m in (re.match(r"^(%tmp\d+) = alloca %fort\.span, align 8$", i) for i in ins)
            if m
        ]:
            fields = {}
            for i in ins:
                m = re.match(
                    r"^(%t\d+) = getelementptr inbounds %fort\.span, ptr "
                    + re.escape(slot)
                    + r", i32 0, i32 ([01])$",
                    i,
                )
                if m:
                    fields[m.group(1)] = m.group(2)
            if not fields:
                continue
            stored = {}
            loads = {}
            ok = True
            for i in ins:
                toks = TOKEN.findall(i)
                if (
                    slot in toks
                    and not i.startswith(slot + " = alloca ")
                    and not re.match(
                        r"^%t\d+ = getelementptr inbounds %fort\.span, ptr "
                        + re.escape(slot)
                        + r", i32 0, i32 [01]$",
                        i,
                    )
                ):
                    ok = False
                    break
                for r, k in fields.items():
                    if r not in toks or i.startswith(r + " = "):
                        continue
                    m = re.match(
                        r"^store (ptr @\.str\.\d+|i64 \d+), ptr " + re.escape(r) + r", align 8$", i
                    )
                    n = re.match(
                        r"^(%t\d+) = load (ptr|i64), ptr " + re.escape(r) + r", align 8$", i
                    )
                    if m and k not in stored:
                        stored[k] = m.group(1).split(" ", 1)[1]
                    elif n:
                        loads[n.group(1)] = k
                    else:
                        ok = False
            if not ok or set(stored) != {"0", "1"}:
                continue
            value = {r: stored[k] for r, k in loads.items()}

            def fn(i, slot=slot, fields=fields, value=value):
                m = re.match(r"^(%[A-Za-z0-9_.]+) = ", i)
                if i.startswith(slot + " = alloca ") or (
                    m and (m.group(1) in fields or m.group(1) in value)
                ):
                    return []
                if (
                    re.match(r"^store .*, ptr (%t\d+), align 8$", i)
                    and TOKEN.findall(i)[-1] in fields
                ):
                    return []
                return [TOKEN.sub(lambda t: value.get(t.group(0), t.group(0)), i)]

            self.replace(fn)
            self.fired.add("text")
            return self.rewrite_text()

    def rewrite_copy(self):
        """`copy`: an aggregate value that goes through a `%tmp` slot, which one whole
        `llvm.memcpy` then copies into its destination. A whole memcpy has the size and the
        alignment of the type of the slot. When stores, memsets and whole memcpys fill the slot,
        each in a block that dominates the memcpy, and no path from a fill to the memcpy writes
        the source of such a memcpy, the fills move down to the memcpy that reads the slot and
        write its destination, in their order. Otherwise, or when a call fills the slot as its
        `sret` pointer, the fills write the destination in place, and no path from a fill to the
        memcpy may read or write the destination. A memset of the storage of the destination on
        such a path first moves up before the first fill (`hoistable`): the memset stands in the
        block of the memcpy and before it, the block of the first fill dominates that block, every
        path from the first fill reaches that block or a failure block of a check, and nothing
        that the memset passes reads or writes that storage. On the FIR side, the first fill of
        the in-place form is no fill of the destination when it is a whole memset of the slot in a
        block that dominates the memcpy, and on every path to the memcpy a call that takes the
        slot as its `sret` pointer or a whole memcpy then writes every field (`padding`); else
        the memset writes the destination as the other fills do. A memcpy of a type with padding
        copies the padding of the slot, which only a whole memset or a whole memcpy of the slot
        writes, so one of those must stand in a block that dominates the memcpy."""
        self.reset()
        dom = self.dominators()
        self._reach = {}
        changed = True
        while changed:
            changed = False
            self.reset()
            defs = self.defs()
            flat = [(name, k, i) for name, ins in self.blocks for k, i in enumerate(ins)]
            ins = [x[2] for x in flat]
            for slot in [
                m.group(1) for m in (re.match(r"^(%tmp\d+) = alloca ", i) for i in ins) if m
            ]:
                if self.copy_slot(slot, flat, defs, dom):
                    self.fired.add("copy")
                    changed = True
                    break

    def copy_slot(self, slot, flat, defs, dom):
        """Applies the rule `copy` to one slot; returns whether it changed the text. `dom` holds
        the dominators of each block, which the rule does not change."""
        ins = [x[2] for x in flat]
        ty = defs[slot][len("alloca ") :].rsplit(", align ", 1)
        lay = layout(ty[0], self.types)
        if lay is None or str(lay[1]) != ty[1]:
            return False
        size, align = str(lay[0]), str(lay[1])
        out, fills, calls, whole_fills = [], [], [], []
        for k, i in enumerate(ins):
            if slot not in TOKEN.findall(i) or i.startswith(slot + " = alloca "):
                continue
            m = MEMCPY.match(i)
            z = MEMSET.match(i)
            whole = bool(m) and m.group(1) == align and m.group(3) == align and m.group(5) == size
            if m and m.group(4) == slot and m.group(2) != slot:
                out.append((k, m.group(2), whole))
            elif m and m.group(2) == slot and m.group(4) != slot and whole:
                fills.append(k)
                whole_fills.append(k)
            elif z and z.group(2) == slot:
                fills.append(k)
                if z.group(1) == align and z.group(3) == size:
                    whole_fills.append(k)
            elif re.match(r"^store .*, ptr " + re.escape(slot) + r", align \d+$", i):
                fills.append(k)
            elif (
                re.search(r"\(ptr sret\([^)]*\) " + re.escape(slot) + r"[,)]", i)
                and TOKEN.findall(i).count(slot) == 1
            ):
                calls.append(k)
            elif re.match(r"^%t\d+ = getelementptr inbounds .*, ptr " + re.escape(slot) + ", ", i):
                continue
            else:
                return False
        if len(out) != 1 or not out[0][2]:
            return False
        fields = self.field_uses(slot, ins)
        if fields is None:
            return False
        fills = sorted(set(fills) | fields)
        pos, target, _ = out[0]
        if not fills and not calls:
            return False
        if any(f > pos for f in fills + calls):
            return False
        where = flat[pos][0]
        if padded(ty[0], self.types) and not any(flat[f][0] in dom[where] for f in whole_fills):
            # the memcpy copies padding bytes that no fill writes on some path
            return False
        # every fill moves down to the memcpy when its block dominates the block of the memcpy,
        # so that no fill of an exclusive branch joins it, and when the memcpys among them can
        # read their sources there; else the fills write the destination in place
        down = not calls
        if any(flat[f][0] not in dom[where] for f in fills):
            down = False
        for f in fills if down else []:
            m = MEMCPY.match(ins[f])
            if m and m.group(2) == slot:
                src = self.root_path(m.group(4), defs)[0]
                if any(
                    is_write(ins[x]) and x not in fills and self.may_write_text(ins[x], src, defs)
                    for x in self.between(flat, f, pos)
                ):
                    down = False
        hoisted, dropped = [], []
        first = min(fills + calls)
        if down:
            moved = fills
        else:
            span = set()
            for f in fills + calls:
                span |= self.between(flat, f, pos)
            span -= set(fills + calls)
            hoisted = self.hoistable(target, flat, span, fills + calls, pos, defs, dom)
            if not self.untouched(target, flat, span - set(hoisted), defs):
                return False
            moved = []
            whole_set = first in whole_fills and MEMSET.match(ins[first])
            walls = set(calls) | {f for f in whole_fills if f != first and MEMCPY.match(ins[f])}
            if (
                self.after
                and whole_set
                and flat[first][0] in dom[where]
                and self.walled(flat, first, pos, walls)
            ):
                # the zeroing of the FIR lowering (spec/fir.md 9.4), which only the padding
                # keeps: on every path a call or a whole memcpy then writes every field
                dropped = [first]
                self.fired.add("padding")

        def swap(i):
            return TOKEN.sub(lambda t: target if t.group(0) == slot else t.group(0), i)

        new_blocks = {name: [] for name, _ in self.blocks}
        for k, (name, j, i) in enumerate(flat):
            if k == first:
                new_blocks[name] += [ins[h] for h in hoisted]
            if k == pos:
                new_blocks[name] += [swap(ins[f]) for f in moved]
                continue
            if k in moved or k in hoisted or k in dropped or i.startswith(slot + " = alloca "):
                continue
            new_blocks[name].append(swap(i))
        self.blocks = [[name, new_blocks[name]] for name, _ in self.blocks]
        return True

    def walled(self, flat, a, b, walls):
        """Whether every path from position `a` of `flat` to position `b` passes one of the
        positions `walls`. A path that ends in a block with no successor reaches no position."""
        start, size = {}, collections.Counter()
        for k, (name, _, _) in enumerate(flat):
            start.setdefault(name, k)
            size[name] += 1
        succ = {name: successors(ins[-1]) if ins else [] for name, ins in self.blocks}

        def scan(first, name):
            """Walks block `name` from position `first`: True at a wall, False at `b`, and
            None at the end of the block."""
            for k in range(first, start[name] + size[name]):
                if k == b:
                    return False
                if k in walls:
                    return True
            return None

        found = scan(a + 1, flat[a][0])
        if found is not None:
            return found
        seen = set()
        stack = list(succ.get(flat[a][0], []))
        while stack:
            x = stack.pop()
            if x in seen or x not in start:
                continue
            seen.add(x)
            found = scan(start[x], x)
            if found is False:
                return False
            if found is None:
                stack.extend(succ.get(x, []))
        return True

    def between(self, flat, a, b):
        """The positions of `flat` that can run after position `a` and before position `b`
        when `a` does not run again in between: the rest of the block of `a`, each block on a
        path from it to the block of `b` that does not pass the block of `a`, back edges
        included, and the start of the block of `b`, or all of that block but `b` when a path
        leaves it and comes back to it without the block of `a`."""
        ablock, bblock = flat[a][0], flat[b][0]
        if ablock == bblock and a < b:
            return set(range(a + 1, b))
        start, size = {}, collections.Counter()
        for k, (name, _, _) in enumerate(flat):
            start.setdefault(name, k)
            size[name] += 1
        succ = {name: successors(ins[-1]) if ins else [] for name, ins in self.blocks}
        preds = self.predecessors()

        def walk(first, step):
            seen = set()
            stack = list(first)
            while stack:
                x = stack.pop()
                if x in seen or x == ablock or x not in start:
                    continue
                seen.add(x)
                stack.extend(step(x))
            return seen

        fwd = walk(succ.get(ablock, []), lambda x: succ.get(x, []))
        bwd = walk(preds[bblock], lambda x: preds[x])
        out = set(range(a + 1, start[ablock] + size[ablock]))
        for x in fwd & bwd:
            if x != bblock:
                out |= set(range(start[x], start[x] + size[x]))
        if bblock in fwd:
            out |= set(range(start[bblock], b))
            if bblock in bwd:
                out |= set(range(b + 1, start[bblock] + size[bblock]))
        return out

    def hoistable(self, target, flat, span, fills, pos, defs, dom):
        """The positions in `span` of the memsets of the storage of `target` that move up
        before the first of `fills`. Each memset stands in the block of `pos` before it. The
        block of the first fill dominates that block, and every path from it reaches that block
        or a failure block of a check, which reads no storage of the function. No fill that a
        memset passes reads or writes that storage: a fill writes its slot, and a call reads a
        local only when its address escapes before the call or the call names it."""
        root, _ = self.root_path(target, defs)
        name = flat[pos][0]
        sets = sorted(
            k
            for k in span
            if k < pos
            and flat[k][0] == name
            and MEMSET.match(flat[k][2])
            and self.root_path(MEMSET.match(flat[k][2]).group(2), defs)[0] == root
        )
        if not sets:
            return []
        first = min(fills)
        if flat[first][0] not in dom[name] or not self.always_reaches(flat[first][0], name):
            return []
        points = self.escape_points(root) if self.is_local(root) else []
        for k in sorted(self.between(flat, first, max(sets)) | {first}):
            if k not in fills:
                continue
            block, j, i = flat[k]
            m = MEMCPY.match(i)
            if m and not self.distinct(self.root_path(m.group(4), defs)[0], root):
                return []
            if (STORE.match(i) or m or MEMSET.match(i)) and not self.distinct(
                self.written(i, defs)[0], root
            ):
                return []
            if rhs_of(i)[1].startswith("call ") and not (m or MEMSET.match(i)):
                if not self.is_local(root) or root in TOKEN.findall(i):
                    return []
                if self.escaped_before(root, block, j, points):
                    return []
        return sets

    def always_reaches(self, a, b):
        """Whether every path from the end of block `a` reaches block `b` before it comes back
        to `a`, apart from a path that ends in a failure block of a check."""
        if a == b:
            return True
        byname = {n: ins for n, ins in self.blocks}
        seen = set()
        stack = list(successors(byname[a][-1])) if byname.get(a) else []
        while stack:
            x = stack.pop()
            if x == b or x in seen:
                continue
            if x == a or x not in byname:
                return False
            seen.add(x)
            if self.failure(byname[x]):
                continue
            nxt = successors(byname[x][-1]) if byname[x] else []
            if not nxt:
                return False
            stack.extend(nxt)
        return True

    def escaped_before(self, root, name, k, points):
        """Whether the address of the local `root` escapes at one of `points`, its escape
        places, from which a path reaches position `k` of block `name`, that position
        included."""
        for b, j in points:
            if (b == name and j <= k) or name in self.reachable(b):
                return True
        return False

    def untouched(self, target, flat, span, defs):
        """Whether no instruction at the positions `span` of `flat` reads or writes the storage
        of `target`: a write or a load whose address has the root of `target`, or a call that
        is no memcpy or memset when that root is no `%tmp` slot or the call names it. A local
        and a pointer that a load gave are two storages when the address of the local has not
        escaped at the instruction."""
        root, _ = self.root_path(target, defs)
        points = {}

        def esc(x, name, j):
            out = set()
            for r in (root, x):
                if self.is_local(r):
                    if r not in points:
                        points[r] = self.escape_points(r)
                    if self.escaped_before(r, name, j, points[r]):
                        out.add(r)
            return out

        for k in sorted(span):
            name, j, i = flat[k]
            reg, rhs = rhs_of(i)
            addr = None
            s = STORE.match(i)
            if s:
                addr = s.group(3)
            m = LOAD.match(rhs)
            if m:
                addr = m.group(2)
            m = MEMCPY.match(i)
            if m:
                src = self.root_path(m.group(4), defs)[0]
                if not self.distinct(src, root, esc(src, name, j)):
                    return False
                addr = m.group(2)
            m = MEMSET.match(i)
            if m:
                addr = m.group(2)
            if addr is None and rhs.startswith("call ") and not PURE_CALL.match(rhs):
                if not SLOT.match(root) or root in TOKEN.findall(i):
                    return False
                continue
            if addr is not None:
                x = self.root_path(addr, defs)[0]
                if not self.distinct(x, root, esc(x, name, j)):
                    return False
        return True

    def rewrite_argument(self):
        """`argument`: a `%tmp` slot that one whole memcpy fills from a place and that one call
        then takes as an aggregate argument is copied right before that call, the copies of one
        call in argument order. The call must stand in the block of the memcpy or at the end of
        a chain of single predecessors from it, and nothing between may write the place."""
        self.reset()
        defs = self.defs()
        byname = {name: ins for name, ins in self.blocks}
        preds = self.predecessors()
        moves = {}
        for name, ins in self.blocks:
            for k, i in enumerate(ins):
                m = re.match(r"^(%tmp\d+) = alloca (.+), align (\d+)$", i)
                if not m:
                    continue
                slot = m.group(1)
                lay = layout(m.group(2), self.types)
                if lay is None or str(lay[1]) != m.group(3):
                    continue
                uses = [
                    (n, j, x)
                    for n, xs in self.blocks
                    for j, x in enumerate(xs)
                    if slot in TOKEN.findall(x) and not x.startswith(slot + " = ")
                ]
                if len(uses) != 2:
                    continue
                fill = [
                    u for u in uses if MEMCPY.match(u[2]) and MEMCPY.match(u[2]).group(2) == slot
                ]
                call = [
                    u
                    for u in uses
                    if u not in fill
                    and rhs_of(u[2])[1].startswith("call ")
                    and not MEMCPY.match(u[2])
                    and not MEMSET.match(u[2])
                    and not re.search(r"sret\([^)]*\) " + re.escape(slot), u[2])
                    and TOKEN.findall(u[2]).count(slot) == 1
                ]
                if len(fill) != 1 or len(call) != 1:
                    continue
                c = MEMCPY.match(fill[0][2])
                if (
                    c.group(1) != str(lay[1])
                    or c.group(3) != str(lay[1])
                    or c.group(5) != str(lay[0])
                    or c.group(4) == slot
                ):
                    continue
                source = self.root_path(c.group(4), defs)[0]
                path = self.chain(fill[0][0], call[0][0], preds)
                if path is None:
                    continue
                if fill[0][0] == call[0][0] and fill[0][1] > call[0][1]:
                    continue
                between = []
                for n in path:
                    xs = byname[n]
                    lo = fill[0][1] + 1 if n == fill[0][0] else 0
                    hi = call[0][1] if n == call[0][0] else len(xs)
                    between += xs[lo:hi]
                if any(is_write(x) and self.may_write_text(x, source, defs) for x in between):
                    continue
                args = call[0][2]
                index = [t for t in TOKEN.findall(args) if SLOT.match(t)].index(slot)
                moves.setdefault(args, []).append((index, fill[0][2]))
        if not moves:
            return
        moved = {x for v in moves.values() for _, x in v}
        for block in self.blocks:
            out = []
            for i in block[1]:
                if i in moved:
                    continue
                if i in moves:
                    out += [x for _, x in sorted(moves[i])]
                out.append(i)
            if out != block[1]:
                self.fired.add("argument")
            block[1] = out

    def chain(self, first, last, preds):
        """The blocks from `first` to `last` when `last` is `first` or the end of a chain of
        single predecessors that starts at `first`, else None."""
        path = [last]
        while path[-1] != first:
            if path[-1] == "%entry" or len(preds[path[-1]]) != 1 or len(path) > len(preds) + 1:
                return None
            path.append(preds[path[-1]][0])
        return list(reversed(path))

    def may_write_text(self, ins, root, defs):
        """Whether the write `ins` can write the storage `root`."""
        written = self.written(ins, defs)
        s = STORE.match(ins) or MEMCPY.match(ins) or MEMSET.match(ins)
        if s:
            return not self.distinct(written[0], root)
        if self.is_local(root):
            return self.escapes(root) or root in TOKEN.findall(ins)
        return self.call_may_write(ins, root)

    def call_may_write(self, ins, root):
        """Whether the call `ins` can write the storage `root`, which is no local: a print entry
        of std.rt or its string equality writes no global of another module."""
        if QUIET_CALL.match(ins) and root.startswith("@") and not root.startswith('@"std.rt.'):
            return False
        return True

    def reset(self):
        """Forgets what the escape analysis learned, after a rewrite."""
        self._escape = {}
        defs = self.defs()
        self._locals = {reg for reg, rhs in defs.items() if rhs.startswith("alloca ")}
        self._locals |= set(re.findall(r"ptr (%[A-Za-z0-9_]+\.in)", self.define))

    def dominators(self):
        """The blocks that dominate each block of the graph as it stands: every path from
        `entry` to the block passes them."""
        names = [name for name, _ in self.blocks]
        preds = self.predecessors()
        dom = {name: set(names) for name in names}
        dom["%entry"] = {"%entry"}
        changed = True
        while changed:
            changed = False
            for name in names:
                if name == "%entry":
                    continue
                ps = [dom[q] for q in preds[name] if q in dom]
                new = (set.intersection(*ps) if ps else set()) | {name}
                if new != dom[name]:
                    dom[name] = new
                    changed = True
        return dom

    def field_uses(self, slot, ins):
        """The positions of the writes into fields of `slot`, or None when a field of it is
        read."""
        regs = set()
        for i in ins:
            reg, rhs = rhs_of(i)
            parts = gep_parts(rhs)
            if parts and (parts[1] == slot or parts[1] in regs):
                regs.add(reg)
        writes = set()
        for k, i in enumerate(ins):
            for r in regs:
                if r not in TOKEN.findall(i) or i.startswith(r + " = "):
                    continue
                s = STORE.match(i)
                if s and s.group(3) == r and s.group(2) != r:
                    writes.add(k)
                    continue
                parts = gep_parts(rhs_of(i)[1])
                if parts and parts[1] == r:
                    continue
                m = re.match(
                    r"^call void @llvm\.mem(?:cpy|set)\.p0\.(?:p0\.)?i64\(ptr align \d+ "
                    r"(\S+), ",
                    i,
                )
                if m and m.group(1) == r and TOKEN.findall(i).count(r) == 1:
                    writes.add(k)
                    continue
                return None
        return writes

    # ---- the graph ---------------------------------------------------------------------------

    def normalize(self):
        """Skips the blocks that hold only `br label`, drops the blocks that no path from
        `entry` reaches, lets a block that ends in `br label` absorb a target that has no other
        predecessor, puts the cases of each `switch` in value order, and labels each block by
        its place in a walk from `entry` over the successors in operand order."""
        byname = {name: ins for name, ins in self.blocks}
        forward = {}
        for name, ins in self.blocks:
            m = re.match(r"^br label (%\S+)$", ins[0]) if len(ins) == 1 else None
            if m and name != "%entry":
                forward[name] = m.group(1)

        def resolve(label):
            seen = set()
            while label in forward and label not in seen:
                seen.add(label)
                label = forward[label]
            return label

        def retarget(term):
            return re.sub(
                r"label (%[A-Za-z0-9_.]+)", lambda m: "label " + resolve(m.group(1)), term
            )

        for name in byname:
            if byname[name]:
                byname[name][-1] = sort_switch(retarget(byname[name][-1]))
        order = []
        seen = set()
        stack = ["%entry"]
        while stack:
            b = stack.pop()
            if b in seen or b not in byname:
                continue
            seen.add(b)
            order.append(b)
            stack.extend(reversed(successors(byname[b][-1]) if byname[b] else []))
        reach = {b: byname[b] for b in order}
        changed = True
        while changed:
            changed = False
            preds = collections.Counter()
            for b, ins in reach.items():
                for s in successors(ins[-1]) if ins else []:
                    preds[s] += 1
            for b, ins in reach.items():
                m = re.match(r"^br label (%\S+)$", ins[-1]) if ins else None
                if (
                    m
                    and m.group(1) != b
                    and m.group(1) != "%entry"
                    and preds[m.group(1)] == 1
                    and m.group(1) in reach
                ):
                    reach[b] = ins[:-1] + reach.pop(m.group(1))
                    changed = True
                    break
        labels = {}
        stack = ["%entry"]
        while stack:
            b = stack.pop()
            if b in labels or b not in reach:
                continue
            labels[b] = "B%d" % len(labels)
            stack.extend(reversed(successors(reach[b][-1]) if reach[b] else []))
        self.labels = labels
        self.blocks = [[b, reach[b]] for b in sorted(reach, key=lambda b: int(labels[b][1:]))]

    def failure(self, ins):
        """Whether the block `ins` is a failure block of a check: reads and values, one call of
        the entry of a check kind of std.rt or of `llvm.trap`, then `unreachable`. Each pointer
        that the call passes is a global constant, the file and the texts of the check, so the
        entry reads no storage of the function. A `panic` is no such block: its message can
        point into a local."""
        if len(ins) < 2 or ins[-1] != "unreachable":
            return False
        m = re.match(
            r'^call void @("std\.rt\.(fail_(bounds|span|overflow|shift|div_zero|div_overflow|'
            r'alloc_count|overwrite|enum)|assert_fail)"|llvm\.trap)\((?P<args>.*)\)$',
            ins[-2],
        )
        if not m:
            return False
        if any(not x.startswith("@") for x in re.findall(r"ptr (\S+?)(?:,|$)", m.group("args"))):
            return False
        return not any(is_write(i) for i in ins[:-2])

    def sink_stores(self):
        """`aggregate`: a store, a memset or a memcpy into a local or into `%ret.sret` moves
        down to the first item that can read or write its storage. It passes values and reads
        of other storage, writes into other storage, a call only when the storage is a local
        whose address does not escape and that the call does not name, and a check branch when
        the failure block does not read the storage and the continuation has no other
        predecessor. Two writes into one storage keep their order."""
        self.reset()
        defs = self.defs()
        byname = {name: ins for name, ins in self.blocks}
        preds = self.preds_normal()

        def target(i):
            st = STORE.match(i)
            m = MEMCPY.match(i) or MEMSET.match(i)
            addr = st.group(3) if st else (m.group(2) if m else None)
            if addr is None:
                return None
            return self.root_path(addr, defs)

        def overlap(p, q):
            if p is None or q is None:
                return True
            p = tuple(n for n, _ in p)
            q = tuple(n for n, _ in q)
            return p[: len(q)] == q[: len(p)]

        def may_touch(root, path, r2, q):
            return not self.distinct(r2, root) and (r2 != root or overlap(path, q))

        def sink_key(root):
            """The key that orders two stores into two storages that commute: the name, or the
            type of a `%tmp` slot, which is the same in both texts."""
            if SLOT.match(root):
                return "%tmp{" + defs.get(root, "") + "}"
            return root

        def sinkable(root):
            return self.is_local(root) or root == "%ret.sret"

        def passable(i, e):
            root, path = target(i)
            m = MEMCPY.match(i)
            src = self.root_path(m.group(4), defs) if m else None
            if is_terminator(e):
                return False
            reg, rhs = rhs_of(e)
            ld = LOAD.match(rhs)
            if ld:
                # a read of any field of the storage stops the store, so that the stores into
                # one storage move as one group
                r2, q = self.root_path(ld.group(2), defs)
                return self.distinct(r2, root)
            if not is_write(e):
                return True
            t2 = target(e)
            if t2 is not None:
                if self.root_key(t2[0], defs) == self.root_key(root, defs):
                    return False
                if sinkable(t2[0]) and sink_key(root) <= sink_key(t2[0]):
                    # two stores that both move keep one order: the smaller key first
                    return False
                if may_touch(root, path, t2[0], t2[1]):
                    return False
                if src and may_touch(src[0], src[1], t2[0], t2[1]):
                    return False
                m2 = MEMCPY.match(e)
                if m2:
                    s2 = self.root_path(m2.group(4), defs)
                    if may_touch(root, path, s2[0], s2[1]):
                        return False
                return True
            names = set(TOKEN.findall(e))
            for r in [root] + ([src[0]] if src else []):
                if not self.is_local(r) or self.escapes(r) or r in names:
                    return False
            return True

        def crossing(name, i):
            ins = byname[name]
            m = re.match(r"^br i1 \S+, label (%\S+), label (%\S+)$", ins[-1])
            if not m:
                return None
            root, path = target(i)
            for f, y in ((m.group(1), m.group(2)), (m.group(2), m.group(1))):
                if y == name or y == "%entry" or preds[y] != [name]:
                    continue
                if not self.failure(byname[f]):
                    continue
                if any(
                    LOAD.match(rhs_of(x)[1])
                    and may_touch(
                        root, path, *self.root_path(LOAD.match(rhs_of(x)[1]).group(2), defs)
                    )
                    for x in byname[f]
                ):
                    continue
                return y
            return None

        moved = False
        changed = True
        rounds = 0
        while changed:
            changed = False
            rounds += 1
            if rounds > 1000:
                raise RuntimeError("sink_stores does not end")
            for name, _ in self.blocks:
                ins = byname[name]
                k = len(ins) - 2
                while k >= 0:
                    i = ins[k]
                    t = target(i)
                    if t is None or not sinkable(t[0]):
                        k -= 1
                        continue
                    j = k + 1
                    while j < len(ins) - 1 and passable(i, ins[j]):
                        j += 1
                    if j == len(ins) - 1 and crossing(name, i):
                        y = crossing(name, i)
                        ins.pop(k)
                        byname[y].insert(0, i)
                        changed = moved = True
                    elif j > k + 1:
                        ins.pop(k)
                        ins.insert(j - 1, i)
                        changed = moved = True
                    k -= 1
        self.blocks = [[name, byname[name]] for name, _ in self.blocks]
        if moved:
            self.fired.add("aggregate")

    def preds_normal(self):
        preds = collections.defaultdict(list)
        for name, ins in self.blocks:
            for s in successors(ins[-1]) if ins else []:
                preds[s].append(name)
        return preds

    # ---- the events --------------------------------------------------------------------------

    def runs(self, defs):
        """The runs of each block: its writes in order, with each maximal sequence of stores,
        memcpys and memsets into fields of one storage grouped. Each item is [kind, positions,
        key, root, paths] with kind `run` or `write`; `paths` holds the path of each write of a
        run, as a tuple of indices."""
        out = {}
        for name, ins in self.blocks:
            items = []
            for k, i in enumerate(ins):
                if is_terminator(i) or not is_write(i):
                    continue
                dest = None
                s = STORE.match(i)
                m = MEMCPY.match(i) or MEMSET.match(i)
                if s:
                    dest = s.group(3)
                elif m:
                    dest = m.group(2)
                root, path = self.root_path(dest, defs) if dest else (None, None)
                if dest is not None and path is not None:
                    key = self.root_key(root, defs)
                    steps = tuple(n for n, _ in path)
                    if items and items[-1][0] == "run" and items[-1][2] == key:
                        items[-1][1].append(k)
                        items[-1][4].append(steps)
                        continue
                    items.append(["run", [k], key, root, [steps]])
                else:
                    items.append(["write", [k], None, None, None])
            out[name] = items
        return out

    def root_key(self, root, defs):
        """The storage `root` as a key that two loads of one pointer share."""
        seen = set()
        while REGISTER.match(root) and root not in seen:
            seen.add(root)
            rhs = defs.get(root, "")
            m = LOAD.match(rhs)
            if m:
                return "load " + self.root_key(m.group(2), defs)
            return root
        return root

    def is_local(self, root):
        """Whether `root` is storage of this function: an alloca, a `%tmp` slot, `%result` or an
        aggregate parameter, whose address a callee reaches only when it escapes or when the
        call names it."""
        return (
            bool(re.match(r"^(%[A-Za-z0-9_]+\.\d+|%tmp\d+|%result|%[A-Za-z0-9_]+\.in)$", root))
            and root in self._locals
        )

    def distinct(self, a, b, esc=None):
        """Whether the storages `a` and `b` are two objects: two allocas, an alloca or a `%tmp`
        against anything else, the `sret` pointer or an aggregate parameter against another
        one of those but a global, or two globals. The caller can pass a global as the `sret`
        pointer (`g = f()`), so `%ret.sret` and a global can be one object. A local and a
        pointer that a load gave are two objects when the address of the local has not escaped:
        `esc`, when given, holds the locals whose address has escaped at the load in question."""
        if a == b:
            return False
        fixed = re.compile(
            r"^(%[A-Za-z0-9_]+\.\d+|%tmp\d+|%result|%ret\.sret|%[A-Za-z0-9_]+"
            r"\.in|@.*)$"
        )
        if fixed.match(a) and fixed.match(b):
            pair = {a[:1], b[:1]}
            if "%ret.sret" in (a, b) and "@" in pair:
                return False
            return True
        for x in (a, b):
            if self.is_local(x):
                return not (x in esc if esc is not None else self.escapes(x))
        return False

    def escape_points(self, root):
        """The places (block, index) where the address of the local `root` escapes."""
        derived = {root}
        for _ in range(2):
            for i in self.instructions():
                reg, rhs = rhs_of(i)
                parts = gep_parts(rhs)
                if parts and parts[1] in derived:
                    derived.add(reg)
        points = []
        for name, ins in self.blocks:
            for k, i in enumerate(ins):
                toks = TOKEN.findall(i)
                if not derived & set(toks):
                    continue
                reg, rhs = rhs_of(i)
                s = STORE.match(i)
                if s and s.group(2) in derived:
                    points.append((name, k))
                elif rhs.startswith("call ") and not (MEMCPY.match(rhs) or MEMSET.match(rhs)):
                    rest = [
                        t
                        for t in toks
                        if t in derived
                        and not re.search(r"sret\([^)]*\) " + re.escape(t) + r"[,)]", rhs)
                    ]
                    if rest:
                        points.append((name, k))
        return points

    def reachable(self, name):
        """The blocks that a path of one edge or more from block `name` reaches."""
        if name in self._reach:
            return self._reach[name]
        succ = {n: successors(ins[-1]) if ins else [] for n, ins in self.blocks}
        seen = set()
        stack = list(succ.get(name, []))
        while stack:
            b = stack.pop()
            if b in seen:
                continue
            seen.add(b)
            stack.extend(succ.get(b, []))
        self._reach[name] = seen
        return seen

    def escapes(self, root):
        """Whether the address of the alloca `root` leaves the function body: it is stored, or
        passed to a call other than as the `sret` pointer or a memcpy or memset operand."""
        if root in self._escape:
            return self._escape[root]
        derived = {root}
        for i in self.instructions():
            reg, rhs = rhs_of(i)
            parts = gep_parts(rhs)
            if parts and parts[1] in derived:
                derived.add(reg)
        result = False
        for i in self.instructions():
            toks = TOKEN.findall(i)
            for d in derived:
                if d not in toks:
                    continue
                reg, rhs = rhs_of(i)
                s = STORE.match(i)
                if s and s.group(2) == d:
                    result = True
                elif (
                    rhs.startswith("call ")
                    and not (MEMCPY.match(rhs) or MEMSET.match(rhs))
                    and not re.search(r"sret\([^)]*\) " + re.escape(d) + r"[,)]", rhs)
                ):
                    result = True
        self._escape[root] = result
        return result

    def events(self):
        """The allocas as a multiset and the events of each block, by label."""
        self.reset()
        defs = self.defs()
        runs = self.runs(defs)
        preds = self.preds_normal()
        pos = {}
        for name, ins in self.blocks:
            for k, i in enumerate(ins):
                reg, _ = rhs_of(i)
                if reg:
                    pos[reg] = (name, k)
        # the unit of each write position: its index among the units of its block
        unit_at = {}
        for name, items in runs.items():
            for u, item in enumerate(items):
                for k in item[1]:
                    unit_at[(name, k)] = u

        self._reach = {}
        points = {r: self.escape_points(r) for r in self._locals if self.escapes(r)}

        def escaped_at(name, k):
            """The locals whose address escapes at a place from which a path reaches position
            `k` of block `name`."""
            out = set()
            for r, ps in points.items():
                for b, j in ps:
                    if (b == name and j < k) or name in self.reachable(b):
                        out.add(r)
                        break
            return frozenset(out)

        calls = {}
        for name, items in runs.items():
            for u, item in enumerate(items):
                if item[0] == "write":
                    i = self.block(name)[item[1][0]]
                    if not STORE.match(i) and not MEMCPY.match(i) and not MEMSET.match(i):
                        calls[(name, u)] = (
                            set(self.written(i, defs))
                            | {t for t in TOKEN.findall(i) if t in self._locals},
                            i,
                        )
                        continue
                    item[3] = self.written(i, defs)[0]

        def overlap(p, q):
            return p is None or q is None or p[: len(q)] == q[: len(p)]

        def write_hits(name, u, j, root, path, esc):
            """Whether write `j` of the run `u` of block `name` can write `root` at `path`."""
            item = runs[name][u]
            if self.distinct(item[3], root, esc):
                return False
            return item[3] != root or overlap(item[4][j], path)

        def may_write(name, u, root, path, esc):
            """Whether unit `u` of block `name` can write the storage `root` at `path`, when the
            locals `esc` have escaped."""
            if (name, u) in calls:
                named, text = calls[(name, u)]
                if self.is_local(root):
                    return self.escapes(root) or root in named
                return self.call_may_write(text, root)
            item = runs[name][u]
            if item[0] == "run":
                return any(write_hits(name, u, j, root, path, esc) for j in range(len(item[1])))
            return not self.distinct(item[3], root, esc)

        flows = {}

        def block_out(root, path, esc):
            """The units that can write `root` at `path` and reach the end of each block."""
            if (root, path, esc) in flows:
                return flows[(root, path, esc)]
            last = {}
            for name, items in runs.items():
                hit = None
                for u in range(len(items)):
                    if may_write(name, u, root, path, esc):
                        hit = u
                last[name] = hit
            out = {name: set() for name in runs}
            changed = True
            while changed:
                changed = False
                for name, _ in self.blocks:
                    if last[name] is not None:
                        new = {"%s#%d" % (self.labels[name], last[name])}
                    else:
                        new = entry_in(name, out)
                    if new != out[name]:
                        out[name] = new
                        changed = True
            flows[(root, path, esc)] = out
            return out

        def entry_in(name, out):
            if name == "%entry":
                return {"entry"}
            acc = set()
            for p in preds[name]:
                acc |= out[p]
            return acc

        def reaching(name, k, root, path):
            """The units that can write `root` at `path` and reach position `k` of block `name`.
            Inside a run, the write of the run that reaches `k`, named by its path, by the
            count of writes to that path before it and by its value, since the effect of the
            run keeps only the last value of each path."""
            items = runs[name]
            esc = escaped_at(name, k)
            for u in range(len(items) - 1, -1, -1):
                item = items[u]
                if item[1][0] >= k:
                    continue
                if item[0] == "run" and k < item[1][-1]:
                    before = [j for j in range(len(item[1])) if item[1][j] < k]
                    hits = [j for j in before if write_hits(name, u, j, root, path, esc)]
                    if not hits:
                        continue
                    j = hits[-1]
                    same = sum(1 for i in before[:j] if item[4][i] == item[4][j])
                    value = write_value(name, item[1][j])
                    return {"%s#%d:%s.%d=%s" % (self.labels[name], u, item[4][j], same, value)}
                if not may_write(name, u, root, path, esc):
                    continue
                return {"%s#%d" % (self.labels[name], u)}
            return entry_in(name, block_out(root, path, esc))

        def write_value(name, k):
            """What the write at position `k` of block `name` puts in memory, apart from its
            address: the type, the value and the alignment of a store, the size of a memset, and
            the source of a memcpy with the writes that reach its read."""
            raw = self.block(name)[k]
            s = STORE.match(raw)
            if s:
                value = TOKEN.sub(lambda t: sym(t.group(0)), s.group(2))
                return "%s %s align %s" % (s.group(1), value, s.group(4))
            z = MEMSET.match(raw)
            if z:
                return "memset %s align %s" % (z.group(3), z.group(1))
            m = MEMCPY.match(raw)
            src = TOKEN.sub(lambda t: sym(t.group(0)), m.group(4))
            return "memcpy %s @%s %s" % (src, read_tag(name, k, m.group(4)), m.group(5))

        def read_tag(name, k, addr):
            """The writes that reach a read of the address `addr` at position `k` of block
            `name`, as text."""
            root, rpath = self.root_path(addr, defs)
            steps = None if rpath is None else tuple(n for n, _ in rpath)
            return ",".join(sorted(reaching(name, k, root, steps)))

        slots = {}
        for name, items in runs.items():
            for item in items:
                for k in item[1]:
                    i = self.block(name)[k]
                    for tok in self.written(i, defs):
                        if SLOT.match(tok) and tok not in slots:
                            slots[tok] = len(slots)
        memo = {}

        def tag_of(reg):
            name, k = pos[reg]
            return read_tag(name, k, LOAD.match(defs[reg]).group(2))

        def sym(tok):
            if tok in self.labels:
                return self.labels[tok]
            if tok.startswith("@.str.") or tok.startswith("@.file."):
                return "@" + self.data.get(tok, tok)
            if REGISTER.match(tok):
                if tok not in memo:
                    memo[tok] = "<cycle>"
                    rhs = defs.get(tok)
                    if rhs is None:
                        memo[tok] = "?" + tok
                    elif rhs.startswith("call ") and not PURE_CALL.match(rhs):
                        name, k = pos[tok]
                        memo[tok] = "[call@%s#%s]" % (self.labels[name], unit_at[(name, k)])
                    elif LOAD.match(rhs):
                        memo[tok] = "[" + expr(rhs) + " @" + tag_of(tok) + "]"
                    else:
                        memo[tok] = "[" + expr(rhs) + "]"
                return memo[tok]
            if SLOT.match(tok):
                ty = defs.get(tok, "alloca ?")[len("alloca ") :]
                return "%%tmp#%s{%s}" % (slots.get(tok, "unwritten"), ty)
            return tok

        span = [False]

        def expr(rhs):
            m = re.match(r"^trunc i8 (%t\d+) to i1$", rhs)
            if m:
                z = re.match(r"^zext i1 (\S+) to i8$", defs.get(m.group(1), ""))
                if z:
                    op = z.group(1)
                    return (
                        sym(op)[1:-1]
                        if REGISTER.match(op)
                        else TOKEN.sub(lambda t: sym(t.group(0)), op)
                    )
            m = re.match(r"^or i1 (%t\d+), (%t\d+)$", rhs)
            span_check = m and all(
                re.match(r"^icmp ugt i64 ", defs.get(x, "")) for x in (m.group(1), m.group(2))
            )
            if span_check:
                # the two tests of a span check, `lo > hi` and `hi > len`, in value order
                a, b = sym(m.group(1)), sym(m.group(2))
                if a > b:
                    span[0] = True
                    a, b = b, a
                return "or i1 %s, %s" % (a, b)
            return TOKEN.sub(lambda t: sym(t.group(0)), rhs)

        allocas = collections.Counter()
        for i in self.instructions():
            reg, rhs = rhs_of(i)
            if rhs.startswith("alloca "):
                allocas[("%tmp" if SLOT.match(reg) else reg) + " = " + rhs] += 1

        def write_text(name, k):
            """The text of the write at position `k` of block `name`, and the tag of the read of
            its source when it is a memcpy, which reads memory where it stands."""
            raw = self.block(name)[k]
            text = expr(rhs_of(raw)[1])
            m = MEMCPY.match(raw)
            if not m:
                return raw, text, None
            tag = read_tag(name, k, m.group(4))
            return raw, text + " <read @" + tag + ">", tag

        blocks = {}
        for name, ins in self.blocks:
            evs = []
            for item in runs[name]:
                if item[0] == "run":
                    evs.append(("run", item[3], [write_text(name, k) for k in item[1]]))
                else:
                    evs.append(("write", write_text(name, item[1][0])[1]))
            if ins and is_terminator(ins[-1]):
                evs.append(("write", expr(ins[-1])))
            blocks[self.labels[name]] = evs
        if span[0]:
            self.fired.add("span")
        self._defs = defs
        self._render = lambda op: TOKEN.sub(lambda t: sym(t.group(0)), op)
        return allocas, blocks

    def block(self, name):
        for n, ins in self.blocks:
            if n == name:
                return ins
        return []

    def written(self, ins, defs):
        """The roots of the storage that `ins` writes."""
        s = STORE.match(ins)
        if s:
            return [self.root_path(s.group(3), defs)[0]]
        m = MEMCPY.match(ins) or MEMSET.match(ins)
        if m:
            return [self.root_path(m.group(2), defs)[0]]
        m = re.search(r"sret\([^)]*\) (%[A-Za-z0-9_.]+)", ins)
        if m:
            return [self.root_path(m.group(1), defs)[0]]
        return []

    def effect(self, root, writes, expand):
        """The effect of the run `writes` into the storage `root`: the value of each node of
        the tree of `root` that `expand` leaves whole, as (type, value, alignment), and of the
        padding of each node that it splits, at the index -1. None when a write does not fit the
        tree."""
        defs = self._defs
        ty = self.type_of_root(root, defs)
        leaves = {}

        def walk(path, t):
            if path in expand:
                kids = children(t, self.types) if t else None
                if kids is None:
                    return False
                for n, kt in enumerate(kids):
                    if not walk(path + (n,), kt):
                        return False
                if own_padding(t, self.types):
                    # the padding of the node, which a memset or a memcpy of it writes and a
                    # store of a field does not
                    leaves[path + (-1,)] = ["pad", "keep", None]
                return True
            leaves[path] = [t, "keep", None]
            return True

        if ty is None:
            # the type of the root comes from the geps into it
            for raw, *_ in writes:
                dest = STORE.match(raw) or MEMCPY.match(raw) or MEMSET.match(raw)
                addr = dest.group(3) if STORE.match(raw) else dest.group(2)
                _, path = self.root_path(addr, defs)
                if path:
                    ty = path[0][1]
                    break
        if ty is None or not walk((), ty):
            return None
        for raw, text, srctag in writes:
            s = STORE.match(raw)
            m = MEMCPY.match(raw)
            z = MEMSET.match(raw)
            addr = s.group(3) if s else (m or z).group(2)
            _, path = self.root_path(addr, defs)
            p = tuple(n for n, _ in path)
            if s:
                if p not in leaves:
                    return None
                value = "zero" if ZERO.match(s.group(2)) else self.render(s.group(2))
                if leaves[p][0] is not None and leaves[p][0] != s.group(1):
                    return None
                leaves[p] = [s.group(1), value, s.group(4)]
                continue
            covered = [q for q in leaves if q[: len(p)] == p]
            node = self.node_type(ty, p)
            lay = layout(node, self.types) if node else None
            if not covered or lay is None or str(lay[1]) != (m or z).group(1):
                return None
            if z and z.group(3) != str(lay[0]):
                return None
            if m and (m.group(5) != str(lay[0]) or m.group(3) != str(lay[1])):
                return None
            for q in covered:
                qt = leaves[q][0]
                qa = layout(qt, self.types)
                if z:
                    value = "zero"
                else:
                    value = "copy(%s @%s)%s" % (self.render(m.group(4)), srctag, q[len(p) :])
                leaves[q] = [qt, value, str(qa[1]) if qa else None]
        return sorted((q, tuple(v)) for q, v in leaves.items())

    def node_type(self, ty, path):
        for n in path:
            kids = children(ty, self.types)
            if kids is None or n >= len(kids):
                return None
            ty = kids[n]
        return ty

    def render(self, operand):
        """The text of `operand` in the events of this definition."""
        return self._render(operand)

    def run_root(self, unit):
        """The text of the storage that the run `unit` writes."""
        return self._render(unit[1])


def sort_switch(term):
    """The `switch` `term` with its cases in value order."""
    m = re.match(r"^switch (\S+) (\S+), label (%\S+) \[(.*)\]$", term)
    if not m:
        return term
    cases = re.findall(r"(\S+) (-?\d+), label (%\S+)", m.group(4))
    cases.sort(key=lambda c: int(c[1]))
    body = " ".join("%s %s, label %s" % c for c in cases)
    return "switch %s %s, label %s [ %s ]" % (m.group(1), m.group(2), m.group(3), body)


def paths_of(f, writes):
    """The paths from the root that the writes of a run address."""
    out = []
    for raw, *_ in writes:
        s = STORE.match(raw)
        m = MEMCPY.match(raw) or MEMSET.match(raw)
        addr = s.group(3) if s else m.group(2)
        _, path = f.root_path(addr, f._defs)
        out.append(tuple(n for n, _ in path))
    return out


def expansion(paths):
    """The nodes that a set of written paths splits: every strict prefix of a path."""
    out = set()
    for p in paths:
        for n in range(len(p)):
            out.add(p[:n])
    return out


def compare(before, after, bdata, adata, btypes, atypes):
    """Classifies the difference of one definition: the set of items and the two residues. The
    second pass moves stores down (the item `aggregate`); it runs only when the first pass
    leaves a residue."""
    items, left, right = compare_pass(before, after, bdata, adata, btypes, atypes, False)
    if left or right:
        again = compare_pass(before, after, bdata, adata, btypes, atypes, True)
        if not again[1] and not again[2]:
            return again
    return items, left, right


def compare_pass(before, after, bdata, adata, btypes, atypes, sink):
    """One comparison of two definitions; `sink` moves stores down first."""
    b = Function(before, bdata, btypes)
    a = Function(after, adata, atypes)
    a.after = True
    for f in (b, a):
        f.rewrite_result()
        f.rewrite_slot()
        f.rewrite_text()
        f.rewrite_copy()
        f.rewrite_argument()
        f.normalize()
        if sink:
            f.sink_stores()
    ba, bb = b.events()
    aa, ab = a.events()
    items = b.fired | a.fired
    if before[0] != after[0]:
        return items, ["define: " + before[0]], ["define: " + after[0]]
    left, right = [], []
    for x in sorted((ba - aa).elements()):
        left.append("alloca: " + x)
    for x in sorted((aa - ba).elements()):
        right.append("alloca: " + x)
    for label in sorted(set(bb) | set(ab), key=lambda s: int(s[1:])):
        be = bb.get(label, [])
        ae = ab.get(label, [])
        lt, rt = [], []
        for n in range(max(len(be), len(ae))):
            x = be[n] if n < len(be) else None
            y = ae[n] if n < len(ae) else None
            tx, ty, fired = unit_texts(b, a, x, y)
            items |= fired
            if tx != ty:
                lt.append(tx)
                rt.append(ty)
        left += ["%s: %s" % (label, t) for t in lt if t is not None]
        right += ["%s: %s" % (label, t) for t in rt if t is not None]
    if not left and not right and not items:
        items = {"order"}
    return items, left, right


def unit_texts(b, a, x, y):
    """The texts of the units `x` of `b` and `y` of `a` at one place, and the items that made
    two different runs agree."""
    fired = set()

    def raw(u):
        if u is None:
            return None
        if u[0] == "write":
            return u[1]
        return "run " + " ; ".join(t for _, t, _ in u[2])

    if x is None or y is None or x[0] != "run" or y[0] != "run":
        return raw(x), raw(y), fired
    tx, ty = raw(x), raw(y)
    if tx == ty:
        return tx, ty, fired
    bp = paths_of(b, x[2])
    ap = paths_of(a, y[2])
    expand = expansion(bp + ap)
    eb = b.effect(x[1], x[2], expand)
    ea = a.effect(y[1], y[2], expand)
    if eb is None or ea is None or b.run_root(x) != a.run_root(y):
        return tx, ty, fired
    if eb != ea:
        # the FIR path may zero padding that the direct path leaves as it was, and nothing else
        db, da = dict(eb), dict(ea)
        if set(db) != set(da) or any(
            db[q] != da[q] and (q[-1:] != (-1,) or da[q][1] != "zero") for q in db
        ):
            return tx, ty, fired
        fired.add("padding")
    bset = any(MEMSET.match(r) for r, _, _ in x[2])
    aset = any(MEMSET.match(r) for r, _, _ in y[2])
    fired.add("zero" if bset != aset else "aggregate")
    text = "run %s %s" % (b.run_root(x), eb)
    return text, text, fired


def reported(text):
    """The functions that a report of llvm-diff names."""
    return [m.group(1) for m in re.finditer(r"^in function (\S+):$", text, re.M)]


def run_llvm_diff(tool, left, right):
    p = subprocess.run([tool, left, right], capture_output=True, text=True, check=False)
    return p.returncode, p.stdout + p.stderr


def module_lines(path, data):
    """The lines of the module at `path` outside its definitions, blank lines left out, with
    each `@.str.N` and `@.file.N` written as its bytes."""
    out = []
    inside = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("define "):
                inside = True
            if not inside and line:
                out.append(
                    TOKEN.sub(
                        lambda t: (
                            "@" + data.get(t.group(0), t.group(0))
                            if t.group(0) in data
                            else t.group(0)
                        ),
                        line,
                    )
                )
            if inside and line == "}":
                inside = False
    return out


def compare_one(job):
    """compare_module for one job of the process pool: its counts, its lists and the names of
    the definitions whose text differs."""
    before, after, mode, name = job
    counts = collections.Counter()
    unclassified = []
    classified = []
    differing = compare_module(before, after, mode, name, counts, unclassified, classified)
    return counts, unclassified, classified, differing


def compare_module(before, after, mode, name, counts, unclassified, classified):
    """Compares the two modules `name` of `mode`: each definition whose text differs, and the
    lines outside the definitions. Returns the names of the definitions whose text differs."""
    bfuncs, bdata, btypes = read_module(os.path.join(before, mode, name))
    afuncs, adata, atypes = read_module(os.path.join(after, mode, name))
    differing = set()
    for fn in sorted(set(bfuncs) | set(afuncs)):
        if fn not in bfuncs or fn not in afuncs:
            unclassified.append("%s %s %s: in one module only" % (mode, name, fn))
            differing.add(fn)
            continue
        if bfuncs[fn] == afuncs[fn]:
            continue
        differing.add(fn)
        items, left, right = compare(bfuncs[fn], afuncs[fn], bdata, adata, btypes, atypes)
        if left or right:
            lines = ["%s %s %s:" % (mode, name, fn)]
            lines += ["  < " + e for e in left] + ["  > " + e for e in right]
            unclassified.append("\n".join(lines))
            continue
        counts["functions"] += 1
        classified.append("%s %s %s: %s" % (mode, name, fn, " ".join(sorted(items))))
        for item in items:
            counts[item] += 1
    bl = module_lines(os.path.join(before, mode, name), bdata)
    al = module_lines(os.path.join(after, mode, name), adata)
    if bl != al:
        lb = collections.Counter(bl)
        la = collections.Counter(al)
        if lb == la:
            counts["declarations"] += 1
        else:
            lines = ["%s %s: the lines outside the definitions:" % (mode, name)]
            lines += ["  < " + e for e in sorted((lb - la).elements())]
            lines += ["  > " + e for e in sorted((la - lb).elements())]
            unclassified.append("\n".join(lines))
    return differing


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--llvm-diff", default="llvm-diff")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument(
        "--list",
        action="store_true",
        help="write each classified definition to <out>/classified.txt",
    )
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("out")
    args = ap.parse_args(argv)
    pairs = []
    missing = []
    for mode in MODES:
        d = os.path.join(args.before, mode)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith(".ll"):
                continue
            if os.path.exists(os.path.join(args.after, mode, name)):
                pairs.append((mode, name))
            else:
                missing.append("%s %s: no module in <after>" % (mode, name))
        a = os.path.join(args.after, mode)
        for name in sorted(os.listdir(a)) if os.path.isdir(a) else []:
            if name.endswith(".ll") and not os.path.exists(os.path.join(d, name)):
                missing.append("%s %s: no module in <before>" % (mode, name))
    if not pairs:
        print("fir_diff.py: no module stands in both snapshots", file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    for mode in MODES:
        os.makedirs(os.path.join(args.out, "llvm-diff", mode), exist_ok=True)

    def one(pair):
        mode, name = pair
        left = os.path.join(args.before, mode, name)
        right = os.path.join(args.after, mode, name)
        code, text = run_llvm_diff(args.llvm_diff, left, right)
        with open(
            os.path.join(args.out, "llvm-diff", mode, name[:-3] + ".txt"), "w", encoding="utf-8"
        ) as f:
            f.write(text)
        return pair, code, text

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(one, pairs))

    counts = collections.Counter()
    unclassified = list(missing)
    classified = []
    reports = 0
    jobs = [(args.before, args.after, mode, name) for (mode, name), _, _ in results]
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        compared = list(pool.map(compare_one, jobs, chunksize=4))
    for ((mode, name), code, text), (c, u, k, differing) in zip(results, compared):
        if code not in (0, 1):
            unclassified.append(
                "%s %s: llvm-diff exited %d: %s" % (mode, name, code, text.strip()[:200])
            )
            continue
        counts.update(c)
        unclassified += u
        classified += k
        names = reported(text)
        reports += len(names)
        for fn in names:
            if fn not in differing:
                unclassified.append(
                    "%s %s %s: llvm-diff names a function whose text is equal" % (mode, name, fn)
                )
    with open(os.path.join(args.out, "unclassified.txt"), "w", encoding="utf-8") as f:
        for u in unclassified:
            f.write(u + "\n")
    if args.list:
        with open(os.path.join(args.out, "classified.txt"), "w", encoding="utf-8") as f:
            for c in classified:
                f.write(c + "\n")
    print(
        "fir_diff.py: %d modules, %d in one snapshot only; llvm-diff names %d functions; "
        "the text of %d differs"
        % (
            len(pairs),
            len(missing),
            reports,
            counts["functions"] + len(unclassified) - len(missing),
        )
    )
    for key, text in ITEMS:
        print("fir_diff.py: %-12s %6d  %s" % (key, counts[key], text))
    print("fir_diff.py: unclassified %d (unclassified.txt)" % len(unclassified))
    return 1 if unclassified else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
