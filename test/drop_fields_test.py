#!/usr/bin/env python3
"""Hold that no function dels below a field or an element of a span whose type has a value drop.

A value drop takes its value by `move` and holds each release of its type once (the drop form,
notes/compiler.md 6). A function that dels the storage below such a field or element, as
`del(v.text.data)` for a field `text` of type str_buf, holds a second copy of a release of
strbuf.drop, so a defect of the drop shows on one path only. Such a field ends through the drop of
its type, `strbuf.drop(move(v.text))`.

This test reads the .ft files of src/fort, src/lsp and std. It reads each declaration at the top
of a file and each declaration in each branch of a module `$if`, so it reads the code of both
targets. Of each function it reads the parameters, the locals and each `del`. It lists each `del`
whose operand passes through a field or an element of a span, strictly below its root, whose type
has a value drop. It holds the count of the list at 0. The scan reads the code of a line:
it removes each `//` comment and the text of each string and char literal first, so a brace in a
literal does not move the end of a body. A control tree gives one line for each form that the
scan follows, and no line for a `del` of the storage of a local of such a type itself (the Del
form of notes/compiler.md 6).

A type has a value drop when a function whose name holds `drop` takes it by value:
`fn <name>(<type> mut <param>) void`. A drop_made takes a pointer, so it gives no value drop.

What the scan cannot see, so it lists no line for it:
- a type that no `struct` of src/fort, src/lsp or std declares (test/fort/support is outside
  this test), or a type name that it cannot resolve through the imports of the file;
- a field that does not stand on a line of its own, and of two branches of a `$if` that declare
  one struct, the type that the first branch gives to a field that both declare;
- a module that both std/linux and std/darwin hold: a type name resolves to the std/darwin file;
- a local that no line of the form `<type> <name> = ...;`, `<type> <name>;` or
  `for (<type> <name> : ...)` declares, as a declaration over two lines;
- a `del` whose operand is no path of a parameter or a local: a call, a cast, a dereference, a
  global, or an element of a span that a call gives;
- a `del` over two lines, and a `del` with an index that holds a `]`.
"""

import argparse
import os
import re
import sys
import tempfile
import unittest

TREES = ("src/fort", "src/lsp", "std")

# A declared type: a name, then `mut`, `own`, `*`, `@` and `[N]` in any order. N is any text
# without a `]`, as `2 * 2`.
TYPE = r"[\w.]+(?:\s+(?:mut|own)\b|\s*(?:\*|@|\[[^\]]*\]))*"
DROP = re.compile(r"^fn (\w*drop\w*)\(\s*([\w.]+) mut (\w+)\s*\) void$")
STRUCT = re.compile(r"^struct (\w+)\s*\{")
FIELD = re.compile(r"^\s+(.+?)\s+(\w+)\s*;")
IMPORT = re.compile(r"^import\s+([\w.]+?)(?:\.\{([^}]*)\})?(?:\s+as\s+(\w+))?\s*;")
LOCAL = re.compile(r"^\s+(" + TYPE + r")\s+(\w+)\s*(?:=|;)")
FOR = re.compile(r"\bfor\s*\(\s*(" + TYPE + r")\s+(\w+)\s*:")
DEL = re.compile(r"\bdel\s*\(\s*(\w+)((?:(?:\.|->)\w+|\[[^\]]*\])+)\s*\)")
STEP = re.compile(r"\.(\w+)|->(\w+)|\[[^\]]*\]")
SUFFIX = re.compile(r"\*|@|\[[^\]]*\]")
KEYWORDS = {"return", "del", "defer", "if", "while", "for", "switch", "case", "break",
            "continue", "else", "fn", "struct", "enum", "import", "extern", "move", "new"}

OPTIONS = None


def module_name(rel):
    """The module path of a file: src/fort/owner/prover.ft is fort.owner.prover, src/lsp/json.ft
    lsp.json, std/strbuf.ft std.strbuf, and std/linux/libc.ft std.libc."""
    parts = rel[:-len(".ft")].split("/")
    if parts[0] == "src":
        return ".".join(parts[1:])
    if parts[0] == "std" and len(parts) == 3 and parts[1] in ("linux", "darwin"):
        return "std." + parts[2]
    return ".".join(parts)


def code(line):
    """The code of a line: each string and char literal empty, and no `//` comment. No token spans
    a newline (spec/grammar.md 1), so a line holds each literal whole."""
    out = []
    i = 0
    while i < len(line):
        ch = line[i]
        if line.startswith("//", i):
            break
        if ch in "\"'":
            j = i + 1
            while j < len(line) and line[j] != ch:
                j += 2 if line[j] == "\\" else 1
            out.append(ch + ch)
            i = j + 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def depth(text):
    """The braces that `text` opens less the braces that it closes."""
    return text.count("{") - text.count("}")


def base_type(text):
    """The named type of a declared type and its suffixes from the name outward: ('T', []) for
    `T`, ('T', ['pointer']) for `T*`, `T mut*` and `T mut* own`, ('T', ['span']) for `T@` and
    `T[N]`, and ('T', ['pointer', 'span']) for `T mut* mut@ own`, a span of pointers. None for
    any other form, as a function type. Only the whole words `mut` and `own` are qualifiers:
    `owner_rec` is a name."""
    text = re.sub(r"\b(?:mut|own)\b", " ", text)
    m = re.match(r"^\s*([\w.]+)\s*((?:(?:\*|@|\[[^\]]*\])\s*)*)$", text)
    if not m:
        return None
    kinds = ["pointer" if s == "*" else "span" for s in SUFFIX.findall(m.group(2))]
    return (m.group(1), kinds)


def parameters(header):
    """The parameters of a function header as {name: declared type}. The list ends at the `)`
    that closes its `(`, so a return type of a function type, as `fn (u64) void`, is no part of
    it."""
    m = re.match(r"^fn \w+\(", header)
    params = {}
    if not m:
        return params
    level, cur, parts = 0, "", []
    for ch in header[m.end():]:
        if ch == ")" and level == 0:
            break
        if ch == "(":
            level += 1
        elif ch == ")":
            level -= 1
        if ch == "," and level == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    for p in parts:
        p = p.strip()
        if "(" in p or not p:
            continue
        words = p.rsplit(None, 1)
        if len(words) == 2:
            params[words[1]] = words[0]
    return params


class Scan:
    """The structs, the value drops and the functions of the .ft files under TREES of `root`."""

    def __init__(self, root):
        self.root = root
        self.files = []
        for tree in TREES:
            for top, _, names in os.walk(os.path.join(root, tree)):
                for name in sorted(names):
                    if name.endswith(".ft"):
                        self.files.append(os.path.relpath(os.path.join(top, name), root))
        self.files.sort()
        self.code = {}
        for f in self.files:
            with open(os.path.join(root, f)) as handle:
                self.code[f] = [code(line) for line in handle.read().split("\n")]
        self.by_module = {}
        for f in self.files:
            self.by_module.setdefault(module_name(f), f)
        self.imports = {}
        self.structs = {}
        self.funcs = {}
        decls = {f: list(self.declarations(f)) for f in self.files}
        for f in self.files:
            self.imports[f] = {}
            for d in decls[f]:
                if d[0] == "import":
                    self.add_import(f, d[1])
        self.drops = {}
        for f in self.files:
            self.funcs[f] = []
            for d in decls[f]:
                if d[0] == "struct":
                    self.structs.setdefault((f, d[1]), {}).update(d[2])
                elif d[0] == "fn":
                    self.funcs[f].append(d[1:])
                    m = DROP.match(d[3])
                    if m:
                        key = self.resolve(f, m.group(2))
                        if key:
                            self.drops[key] = m.group(1)

    def declarations(self, f):
        """Each declaration of file `f` at the top or in a branch of a module `$if`:
        ('import', text), ('struct', name, {field: declared type}), and
        ('fn', name, header line, header up to its brace, [(line, code)] of the body)."""
        lines = self.code[f]
        i = 0
        while i < len(lines):
            c = lines[i].strip()
            if c.startswith(("$if", "else")):
                # A line that opens a branch of a module `$if`: the lines of each branch are
                # declarations, as the lines at the top are. A line `}` or `} else {` that closes
                # a branch opens no more braces than it closes, so the scan steps over it.
                i += 1
                continue
            end = i
            text = c
            if c.startswith("import "):
                # An import ends at its `;`.
                while not text.endswith(";") and end + 1 < len(lines):
                    end += 1
                    text += " " + lines[end].strip()
                yield ("import", text)
                i = end + 1
                continue
            if c.startswith(("fn ", "struct ")):
                # A header ends at the brace of its body, or at the `;` of a function with no body.
                while "{" not in text and not text.endswith(";") and end + 1 < len(lines):
                    end += 1
                    text += " " + lines[end].strip()
            last = end
            level = depth(text)
            while level > 0 and last + 1 < len(lines):
                last += 1
                level += depth(lines[last])
            m = STRUCT.match(text)
            if m:
                fields = {}
                for k in range(end + 1, last):
                    fm = FIELD.match(lines[k])
                    if fm:
                        fields[fm.group(2)] = fm.group(1)
                yield ("struct", m.group(1), fields)
            elif c.startswith("fn ") and "{" in text:
                m = re.match(r"^fn (\w+)\(", text)
                body = [(end, lines[end][lines[end].index("{") + 1:])]
                body += [(k, lines[k]) for k in range(end + 1, last + 1)]
                yield ("fn", m.group(1) if m else "?", i + 1, text[:text.index("{")].strip(), body)
            i = last + 1

    def add_import(self, f, text):
        """Binds the names of one import of file `f` to their paths: `import a.b;` binds `b`,
        `import a.b as c;` binds `c`, and `import a.b.{s, t as u};` binds `s` and `u`."""
        m = IMPORT.match(text)
        if not m:
            return
        if m.group(2) is None:
            self.imports[f][m.group(3) or m.group(1).split(".")[-1]] = m.group(1)
            return
        for item in m.group(2).split(","):
            words = item.split()
            if words:
                self.imports[f][words[-1]] = m.group(1) + "." + words[0]

    def resolve(self, f, name):
        """The (file, struct) that the type name `name` written in file `f` names, or None."""
        if "." in name:
            prefix, leaf = name.rsplit(".", 1)
            full = self.imports[f].get(prefix)
            g = self.by_module.get(full) if full else None
            return (g, leaf) if g else None
        full = self.imports[f].get(name)
        if full and "." in full:
            # A symbol import: `import a.b.sym;` or `import a.b.{sym}`.
            prefix, leaf = full.rsplit(".", 1)
            g = self.by_module.get(prefix)
            if g:
                return (g, leaf)
        return (f, name)

    def hits(self):
        """Each `del` below a field or an element of a span whose type has a value drop: (file,
        line, function, text, drop)."""
        out = []
        self.read = 0
        for f in self.files:
            for name, _, header, body in self.funcs[f]:
                env = parameters(header)
                for k, line in body:
                    m = LOCAL.match(line)
                    if m and m.group(1).split()[0] not in KEYWORDS:
                        env[m.group(2)] = m.group(1)
                    for fm in FOR.finditer(line):
                        env[fm.group(2)] = fm.group(1)
                    for dm in DEL.finditer(line):
                        self.read += 1
                        if dm.group(1) not in env:
                            continue
                        found = self.walk(f, env[dm.group(1)], dm.group(2))
                        if found:
                            out.append((f, k + 1, name, dm.group(0), found))
        return out

    def walk(self, f, declared, path):
        """The drop of the first field or element of a span strictly below the root of `path`
        whose type has a value drop, or None. The current value has the named type `key` and the
        suffixes `kinds`, from the name outward. `[i]` takes a span off the end, `->f` needs a
        pointer at the end and reads the field, and `.f` reads the field. A valid `del` ends on an
        owner, a pointer or a span, so its last step gives no value without a suffix, and the walk
        reads that step too. `.` on a pointer is an error of the checker, so `.f` does not test
        the suffixes."""
        steps = list(STEP.finditer(path))
        cur = base_type(declared)
        if cur is None:
            return None
        key, kinds = self.resolve(f, cur[0]), list(cur[1])
        for step in steps:
            if key is None:
                return None
            if step.group(0).startswith("["):
                if not kinds or kinds[-1] != "span":
                    return None
                kinds.pop()
            else:
                if step.group(2) and (not kinds or kinds[-1] != "pointer"):
                    return None
                fields = self.structs.get(key)
                field = step.group(1) or step.group(2)
                if fields is None or field not in fields:
                    return None
                nxt = base_type(fields[field])
                if nxt is None:
                    return None
                key, kinds = self.resolve(key[0], nxt[0]), list(nxt[1])
            if not kinds and key in self.drops:
                return self.drops[key]
        return None


def listing(found):
    return "\n".join("%s:%d: %s: %s (%s)" % h for h in found)


class DropFields(unittest.TestCase):
    def test_no_function_dels_below_a_field_or_an_element_of_a_span_with_a_value_drop(self):
        scan = Scan(OPTIONS.root)
        found = scan.hits()
        names = {key[1] for key in scan.drops}
        for name in ("str_buf", "str_map", "int_vec", "bits"):
            self.assertIn(name, names, "the scan finds no value drop of %s" % name)
        # 537 `del`s of a path at the drop form change; a scan that reads none would pass
        self.assertGreater(scan.read, 300, "the scan reads too few `del`s of a path")
        self.assertEqual(found, [], "%d `del`s below a field or an element of a span whose type "
                         "has a value drop:\n%s" % (len(found), listing(found)))

    def test_the_control_gives_each_line_and_no_other(self):
        with tempfile.TemporaryDirectory() as root:
            for rel, text in CONTROL.items():
                os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
                with open(os.path.join(root, rel), "w") as h:
                    h.write(text)
            found = Scan(root).hits()
            got = sorted((f, line, fn) for f, line, fn, _, _ in found)
            self.assertEqual(got, CONTROL_LINES, listing(found))


CONTROL_STD = """struct str_buf {
    u8 mut@ own data;
    u64 len;
}

fn create() str_buf {
    return str_buf{};
}

fn drop(str_buf mut b) void {
    del(b.data);
}
"""

# Each `del` below the field `text` gives a line: through a parameter of a drop, a pointer
# parameter and a local; after a header over two lines with a comment; after a field with no drop
# and a pointer field; after an element of a span field and of a span of pointers; in a struct
# whose name holds `own`; through the variable of a range `for`; after a brace in a literal and in
# a comment; in a body on the line of its header; in each branch of a module `$if`, and in a struct
# that two branches declare with different fields; after a `return` of the parameter, a function
# with no body, a struct whose brace stands on the next line, a return type of a function type,
# `del (` and an array `[2 * 2]`. The `del` of the storage of a local str_buf itself is the Del
# form and gives no line.
CONTROL_HOLDER = """import std.strbuf;

struct holder {
    strbuf.str_buf text;
    u64 n;
}

struct box {
    holder mut* inner;
}

struct outer {
    box b;
}

struct span_holder {
    strbuf.str_buf mut@ own bufs;
}

struct owner_rec {
    strbuf.str_buf text;
}

fn holder_drop(holder mut h) void {
    del(h.text.data);
}

fn clear(holder mut* p) void {
    del(p->text.data);
}

fn local() void {
    holder mut h = holder{};
    defer del(h.text.data);
    strbuf.str_buf mut b = strbuf.str_buf{};
    defer del(b.data);
}

fn joined(u64 n, // the count
          holder mut* p) void {
    del(p->text.data);
}

fn deep(outer mut o) void {
    del(o.b.inner->text.data);
}

fn spans(span_holder mut h, holder mut* mut@ own hs) void {
    del(h.bufs[0].data);
    del(hs[0]->text.data);
}

fn named(owner_rec mut r) void {
    del(r.text.data);
}

fn ranged(holder@ hs) void {
    for (holder mut h : hs) {
        del(h.text.data);
    }
}

fn literal(holder mut h) void {
    string close = "\\"}";
    char brace = '}';
    // a } in a comment
    del(h.text.data);
}

fn short(holder mut h) void { del(h.text.data); }

$if ($cfg(target_os) == "macos") {
    struct mac_holder {
        strbuf.str_buf text;
    }

    fn mac(mac_holder mut h) void {
        del(h.text.data);
    }
} else $if ($cfg(target_os) == "linux") {
    fn linux(holder mut* p) void {
        del(p->text.data);
    }
}
else {
    fn other(holder mut* p) void {
        del(p->text.data);
    }
}

$if ($cfg(target_os) == "macos") {
    struct pair_rec {
        strbuf.str_buf first;
    }
} else {
    struct pair_rec {
        strbuf.str_buf second;
    }
}

fn pair(pair_rec mut r) void {
    del(r.first.data);
    del(r.second.data);
}

fn kept(holder mut h, bool c) holder {
    if (c) {
        return h;
    }
    del(h.text.data);
    return holder{};
}

fn proto() void;

fn after_proto(holder mut h) void {
    del(h.text.data);
}

struct two_line
{
    strbuf.str_buf text;
}

struct sized {
    holder[2 * 2] items;
}

fn two_line_struct(two_line mut t) void {
    del(t.text.data);
}

fn gives_fn(holder mut* p) fn (u64) void {
    del(p->text.data);
}

fn spaced(holder mut h) void {
    del (h.text.data);
}

fn sized_items(sized mut s) void {
    del(s.items[0].text.data);
}
"""

# A field whose type the file names through `import ... as`, and through an item of a grouped
# import over two lines under a name of its own, gives a line.
CONTROL_ALIAS = """import std.strbuf as sb;
import std.strbuf.{create,
                   str_buf as text_buf};

struct aliased {
    sb.str_buf text;
}

struct renamed {
    text_buf text;
}

fn by_alias(aliased mut v) void {
    del(v.text.data);
}

fn by_rename(renamed mut v) void {
    del(v.text.data);
}
"""

# A field whose type the file names through an item of a grouped import gives a line, and so does
# a field of a struct of another module whose type that module names through its own import. A
# `del` of the storage of a str_buf that a pointer field points to gives none: the field holds no
# str_buf.
CONTROL_ITEM = """import std.strbuf.{str_buf};
import fort.holder;

struct item {
    str_buf text;
}

struct cursor {
    str_buf mut* buf;
}

fn by_item(item mut v) void {
    del(v.text.data);
}

fn borrowed(cursor mut c) void {
    del(c.buf->data);
}

fn other_module(holder.holder mut h) void {
    del(h.text.data);
}
"""

# A struct of a module of std/linux, which the file names `std.plat`, gives a line.
CONTROL_PLAT = """import std.strbuf;

struct plat_rec {
    strbuf.str_buf text;
}
"""

CONTROL_PLATFORM = """import std.plat;

fn by_platform(plat.plat_rec mut r) void {
    del(r.text.data);
}
"""

CONTROL = {"std/strbuf.ft": CONTROL_STD, "std/linux/plat.ft": CONTROL_PLAT,
           "src/fort/holder.ft": CONTROL_HOLDER, "src/fort/alias.ft": CONTROL_ALIAS,
           "src/fort/platform.ft": CONTROL_PLATFORM, "src/lsp/item.ft": CONTROL_ITEM}
CONTROL_LINES = [
    ("src/fort/alias.ft", 14, "by_alias"), ("src/fort/alias.ft", 18, "by_rename"),
    ("src/fort/holder.ft", 25, "holder_drop"), ("src/fort/holder.ft", 29, "clear"),
    ("src/fort/holder.ft", 34, "local"), ("src/fort/holder.ft", 41, "joined"),
    ("src/fort/holder.ft", 45, "deep"), ("src/fort/holder.ft", 49, "spans"),
    ("src/fort/holder.ft", 50, "spans"), ("src/fort/holder.ft", 54, "named"),
    ("src/fort/holder.ft", 59, "ranged"), ("src/fort/holder.ft", 67, "literal"),
    ("src/fort/holder.ft", 70, "short"), ("src/fort/holder.ft", 78, "mac"),
    ("src/fort/holder.ft", 82, "linux"), ("src/fort/holder.ft", 87, "other"),
    ("src/fort/holder.ft", 102, "pair"), ("src/fort/holder.ft", 103, "pair"),
    ("src/fort/holder.ft", 110, "kept"), ("src/fort/holder.ft", 117, "after_proto"),
    ("src/fort/holder.ft", 130, "two_line_struct"), ("src/fort/holder.ft", 134, "gives_fn"),
    ("src/fort/holder.ft", 138, "spaced"), ("src/fort/holder.ft", 142, "sized_items"),
    ("src/fort/platform.ft", 4, "by_platform"), ("src/lsp/item.ft", 13, "by_item"),
    ("src/lsp/item.ft", 21, "other_module")]


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--root", default=os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))), help="the top of the worktree")
    parser.add_argument("--list", action="store_true",
                        help="print each line that the scan gives, and the count")
    OPTIONS, rest = parser.parse_known_args()
    if OPTIONS.list:
        found = Scan(OPTIONS.root).hits()
        if found:
            print(listing(found))
        print("lines %d" % len(found))
        return 0
    return 0 if unittest.main(argv=[sys.argv[0]] + rest, exit=False).result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
