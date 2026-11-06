#!/usr/bin/env python3
"""Hold that no function frees the storage of a field whose type has a value drop with a `del`.

A value drop takes its value by `move` and holds each release of its type once (the drop form,
notes/compiler.md 6). A function that dels the storage below such a field, as `del(v.text.data)`
for a field `text` of type str_buf, holds a second copy of a release of strbuf.drop, so a defect
of the drop shows on one path only. Such a field ends through the drop of its type,
`strbuf.drop(move(v.text))`. This test reads each function of src/fort, src/lsp and std, with its
parameters and its locals, and lists each `del` whose operand passes through a field whose type
has a value drop. It holds the count of the list at 0. A control tree that holds such a `del` in a
drop, through a pointer parameter and through a local must give three lines, and a `del` of the
storage of a local of such a type itself (the Del form of notes/compiler.md 6) must give none.

A type has a value drop when a function whose name holds `drop`, and not `drop_made`, takes it by
value: `fn <name>(<type> mut <param>) void`.

What the scan cannot see, so it lists no line for it:
- a type that no `struct` of src/fort, src/lsp or std declares (test/fort/support is outside
  this test), or a type name that it cannot resolve through the imports of the file;
- a local that no declaration line of the form `<type> [mut] <name> = ...;` or `<type> [mut]
  <name>;` gives, as the variable of a range `for` or a declaration over two lines;
- a `del` whose operand is no path of a parameter or a local: a call, a cast, a global, or an
  element of a span that a call gives;
- a step through a field of a pointer to a pointer, or through an element of an array of arrays.
"""

import argparse
import os
import re
import sys
import tempfile
import unittest

TREES = ("src/fort", "src/lsp", "std")

DROP = re.compile(r"^fn (\w*drop\w*)\(\s*([\w.]+) mut (\w+)\s*\) void \{")
STRUCT = re.compile(r"^struct (\w+) \{")
FIELD = re.compile(r"^\s+(.+?)\s+(\w+)\s*;")
IMPORT = re.compile(r"^import\s+([\w.]+)(?:\s+as\s+(\w+))?\s*;")
LOCAL = re.compile(r"^\s+((?:[\w.]+)(?:\s+mut)?(?:\s*(?:\*|@|\[\w*\]))*(?:\s+own)?(?:\s+mut)?)"
                   r"\s+(\w+)\s*(?:=|;)")
DEL = re.compile(r"\bdel\(\s*(\w+)((?:(?:\.|->)\w+|\[[^\]]*\])+)\s*\)")
STEP = re.compile(r"\.(\w+)|->(\w+)|\[[^\]]*\]")
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


def base_type(text):
    """The named type of a declared type and how it is reached: ('T', 'value') for `T`,
    ('T', 'pointer') for `T*`, `T mut*` and `T mut* own`, ('T', 'span') for `T@` and `T[N]`, or
    None for any other form (a function type, a pointer to a pointer)."""
    text = " ".join(text.replace("mut", " ").replace("own", " ").split())
    m = re.match(r"^([\w.]+)\s*(\*|@|\[\w*\])?$", text)
    if not m:
        return None
    kind = {None: "value", "*": "pointer"}.get(m.group(2), "span")
    return (m.group(1), kind)


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
        self.lines = {}
        for f in self.files:
            with open(os.path.join(root, f)) as handle:
                self.lines[f] = handle.read().split("\n")
        self.by_module = {}
        for f in self.files:
            self.by_module.setdefault(module_name(f), f)
        self.imports = {}
        self.structs = {}
        self.drops = {}
        for f in self.files:
            imp = {}
            for line in self.lines[f]:
                m = IMPORT.match(line)
                if m:
                    imp[m.group(2) or m.group(1).split(".")[-1]] = m.group(1)
            self.imports[f] = imp
        for f in self.files:
            lines = self.lines[f]
            i = 0
            while i < len(lines):
                m = STRUCT.match(lines[i])
                if m:
                    fields = {}
                    i += 1
                    while i < len(lines) and not lines[i].startswith("}"):
                        fm = FIELD.match(lines[i].split("//")[0])
                        if fm:
                            fields[fm.group(2)] = fm.group(1)
                        i += 1
                    self.structs[(f, m.group(1))] = fields
                m = DROP.match(lines[i]) if i < len(lines) else None
                if m and not m.group(1).endswith("_made"):
                    key = self.resolve(f, m.group(2))
                    if key:
                        self.drops[key] = m.group(1)
                i += 1

    def resolve(self, f, name):
        """The (file, struct) that the type name `name` written in file `f` names, or None."""
        if "." not in name:
            return (f, name)
        prefix, leaf = name.rsplit(".", 1)
        full = self.imports[f].get(prefix)
        g = self.by_module.get(full) if full else None
        return (g, leaf) if g else None

    def functions(self, f):
        """Each function of file `f`: its name, the line of its header, its parameters as
        {name: declared type}, and the lines of its body."""
        lines = self.lines[f]
        i = 0
        while i < len(lines):
            if not lines[i].startswith("fn "):
                i += 1
                continue
            start = i
            header = lines[i]
            while not header.rstrip().endswith("{") and i + 1 < len(lines):
                i += 1
                header += " " + lines[i].strip()
            m = re.match(r"^fn (\w+)\((.*)\)", header)
            params = {}
            if m:
                depth, cur, parts = 0, "", []
                for ch in m.group(2):
                    if ch == "(":
                        depth += 1
                    elif ch == ")":
                        depth -= 1
                    if ch == "," and depth == 0:
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
            j = i + 1
            while j < len(lines) and not lines[j].startswith("}"):
                j += 1
            yield (m.group(1) if m else "?", start + 1, params, range(i + 1, j))
            i = j + 1

    def hits(self):
        """Each `del` below a field whose type has a value drop: (file, line, function, text,
        drop)."""
        out = []
        self.read = 0
        for f in self.files:
            for name, _, params, body in self.functions(f):
                env = dict(params)
                for k in body:
                    line = self.lines[f][k].split("//")[0]
                    m = LOCAL.match(line)
                    if m and m.group(2) not in KEYWORDS and m.group(1).split()[0] not in KEYWORDS:
                        env[m.group(2)] = m.group(1)
                    for dm in DEL.finditer(line):
                        self.read += 1
                        if dm.group(1) not in env:
                            continue
                        found = self.walk(f, env[dm.group(1)], dm.group(2))
                        if found:
                            out.append((f, k + 1, name, dm.group(0), found))
        return out

    def walk(self, f, declared, path):
        """The drop of the first field strictly below the root and above the last step of `path`
        whose type has a value drop, or None."""
        steps = [m for m in STEP.finditer(path)]
        cur = base_type(declared)
        if cur is None:
            return None
        key, kind = self.resolve(f, cur[0]), cur[1]
        for n, step in enumerate(steps[:-1]):
            if key is None:
                return None
            if step.group(0).startswith("["):
                if kind != "span":
                    return None
                kind = "value"
            else:
                if kind == "span" or (step.group(2) and kind != "pointer"):
                    return None
                fields = self.structs.get(key)
                if fields is None:
                    return None
                field = step.group(1) or step.group(2)
                if field not in fields:
                    return None
                nxt = base_type(fields[field])
                if nxt is None:
                    return None
                key, kind = self.resolve(key[0], nxt[0]), nxt[1]
                if kind == "value" and key in self.drops:
                    return self.drops[key]
        return None


def listing(found):
    return "\n".join("%s:%d: %s: %s (%s)" % h for h in found)


class DropFields(unittest.TestCase):
    def test_no_function_dels_below_a_field_whose_type_has_a_value_drop(self):
        scan = Scan(OPTIONS.root)
        found = scan.hits()
        names = {key[1] for key in scan.drops}
        for name in ("str_buf", "str_map", "int_vec", "bits"):
            self.assertIn(name, names, "the scan finds no value drop of %s" % name)
        # 537 `del`s of a path at the drop form change; a scan that reads none would pass
        self.assertGreater(scan.read, 300, "the scan reads too few `del`s of a path")
        self.assertEqual(found, [], "%d `del`s below a field whose type has a value drop:\n%s"
                         % (len(found), listing(found)))

    def test_the_control_gives_each_line_and_no_other(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, "std"))
            os.makedirs(os.path.join(root, "src", "fort"))
            with open(os.path.join(root, "std", "strbuf.ft"), "w") as h:
                h.write(CONTROL_STD)
            with open(os.path.join(root, "src", "fort", "holder.ft"), "w") as h:
                h.write(CONTROL)
            found = Scan(root).hits()
            got = sorted((f, line, fn) for f, line, fn, _, _ in found)
            self.assertEqual(got, CONTROL_LINES, listing(found))


CONTROL_STD = """struct str_buf {
    u8 mut@ own data;
    u64 len;
}

fn drop(str_buf mut b) void {
    del(b.data);
}
"""

# Three `del`s below the field `text` (a drop, a pointer parameter, a local) and one `del` of the
# storage of a local str_buf itself, which is the Del form and no line.
CONTROL = """import std.strbuf;

struct holder {
    strbuf.str_buf text;
    u64 n;
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
"""
CONTROL_LINES = [("src/fort/holder.ft", 9, "holder_drop"), ("src/fort/holder.ft", 13, "clear"),
                 ("src/fort/holder.ft", 18, "local")]


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
