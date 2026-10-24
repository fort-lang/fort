#!/usr/bin/env python3
"""Write each import of a compiler module in the long spelling `fort.<path>`.

D9.11 makes the file `src/fort/a/b.ft` the module `fort.a.b` of the library `fort`, and every
import spells it that way, in the library and outside it (module-system.md 10.1). The
whole-program build reaches the same file through the search root `src` (D9.2), so one
spelling serves both builds.

Use `long_imports.py [--root DIR] [--check] [file.ft ...]`. Without file arguments the tool
reads the files of SOURCE_GLOBS. It rewrites two kinds of line:

- An import in the header of a file whose first segment names a module or a directory of
  `src/fort` gets the prefix `fort.`. The header is the one of grammar.md 2: imports and the
  `$if` chains that hold imports. It ends at the first other declaration, so a line in a string
  or in a body never changes.
- A `//! flags:` directive of a module test gets the root `../../src` in place of
  `../../src/fort`, once.

A path that a module beside the file, or a module of a support directory of the test corpora, holds
by either reading of D9.3 is not a compiler module there, so the tool keeps it. The tool is
idempotent: no module of `src/fort` is named `fort`. `--check` writes nothing and exits 1 when a
file would change. A branch that adds a short import runs the tool again after a rebase.
"""

import argparse
import re
import sys
from pathlib import Path

LIBRARY = "fort"
LIBRARY_ROOT = "src/fort"
SOURCE_GLOBS = (
    "src/fort/**/*.ft",
    "src/lsp/**/*.ft",
    "test/fort/**/*.ft",
    "test/lsp/**/*.ft",
    # the checker of test/library/interface_test.py, which imports the driver
    "test/library/checker.ft",
)
# The directories that the module tests give as `-I support` or `-I ../fort/support`.
SUPPORT_DIRS = ("test/fort/support",)
OLD_ROOT = "../../src/fort"
NEW_ROOT = "../../src"

IMPORT = re.compile(r"\A(?P<lead>\s*import\s+)(?P<path>[A-Za-z_][A-Za-z0-9_.]*[A-Za-z0-9_])"
                    r"(?P<rest>(?:\.\{|[\s;]).*)\Z", re.S)
FLAGS = re.compile(r"\A(?P<lead>//! flags:)(?P<words>.*?)(?P<end>\s*)\Z", re.S)


def library_segments(root):
    """The first segments that name a module of the library: a file or a directory of it."""
    base = root / LIBRARY_ROOT
    names = set()
    for path in base.iterdir():
        if path.is_file() and path.suffix == ".ft":
            names.add(path.stem)
        elif path.is_dir() and any(path.rglob("*.ft")):
            names.add(path.name)
    return names


def local_dirs(root, path):
    """The roots that a file searches before the compiler: its directory and the support dirs."""
    relative = path.relative_to(root).as_posix()
    if relative.startswith(LIBRARY_ROOT + "/"):
        # a module of the library imports a sibling by the long path only (D9.11)
        return []
    dirs = [path.parent]
    if relative.startswith("test/"):
        dirs.extend(root / d for d in SUPPORT_DIRS)
    return dirs


def resolves_locally(dirs, module):
    """True when a root of `dirs` holds `module` by the module or the symbol reading (D9.3)."""
    parts = module.split(".")
    for directory in dirs:
        if directory.joinpath(*parts).with_suffix(".ft").is_file():
            return True
        if len(parts) > 1 and directory.joinpath(*parts[:-1]).with_suffix(".ft").is_file():
            return True
    return False


def code_of(line):
    """The text of `line` that the lexer reads as code: no `//` comment, each string blanked."""
    out = []
    quote = None
    i = 0
    while i < len(line):
        ch = line[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = None
            out.append(" ")
        elif ch in "\"'":
            quote = ch
            out.append(" ")
        elif line.startswith("//", i):
            break
        else:
            out.append(ch)
        i += 1
    return "".join(out)


def is_import(code):
    """True when the code of a line starts an import declaration."""
    return re.match(r"\s*import\b", code) is not None


def chain_end(codes, first):
    """The index of the last line of the `$if` chain that starts at line `first`.

    The chain is `$if (c) { ... }`, then any number of `else $if (c) { ... }`, then an optional
    `else { ... }` (grammar.md 2). The braces of a branch nest. Returns the last line when the
    chain does not close, so the caller reads no line after it.
    """
    text = []
    for index in range(first, len(codes)):
        text.extend((ch, index) for ch in codes[index] + "\n")
    pos = 0

    def skip_space():
        nonlocal pos
        while pos < len(text) and text[pos][0].isspace():
            pos += 1

    def balanced(opening, closing):
        nonlocal pos
        skip_space()
        if pos >= len(text) or text[pos][0] != opening:
            return False
        depth = 0
        while pos < len(text):
            ch = text[pos][0]
            depth += ch == opening
            depth -= ch == closing
            pos += 1
            if depth == 0:
                return True
        return False

    def word(name):
        nonlocal pos
        skip_space()
        found = "".join(ch for ch, _ in text[pos:pos + len(name)])
        after = text[pos + len(name)][0] if pos + len(name) < len(text) else " "
        if found == name and not (after.isalnum() or after == "_"):
            pos += len(name)
            return True
        return False

    last = len(codes) - 1
    if not word("$if") or not balanced("(", ")") or not balanced("{", "}"):
        return last
    end = text[pos - 1][1]
    while True:
        mark = pos
        if not word("else"):
            pos = mark
            return end
        if word("$if"):
            if not balanced("(", ")") or not balanced("{", "}"):
                return last
        elif not balanced("{", "}"):
            return last
        end = text[pos - 1][1]


def header_imports(lines):
    """The indexes of the lines that start an import of the module header (grammar.md 2).

    The header is `{ import_decl | compile_module_if }`: imports, and `$if` chains whose branches
    hold imports (D21.3). It ends at the first other declaration, or at a `$if` chain that holds
    no import, which is a `compile_decl_if`. Comments and blank lines do not end it.
    """
    codes = [code_of(line) for line in lines]
    found = []
    i = 0
    while i < len(lines):
        code = codes[i].strip()
        if code == "":
            i += 1
        elif is_import(code):
            found.append(i)
            # an import ends at its `;`, so a brace list can span lines
            while i < len(lines) and ";" not in codes[i]:
                i += 1
            i += 1
        elif code.startswith("$if"):
            end = chain_end(codes, i)
            inner = [k for k in range(i, end + 1) if is_import(codes[k])]
            if not inner:
                break
            found.extend(inner)
            i = end + 1
        else:
            break
    return found


def is_compiler_import(module, segments, dirs):
    """True when `module` is a short path of a compiler module that no local root holds."""
    head = module.split(".")[0]
    return head in segments and not resolves_locally(dirs, module)


def rewrite_flags(line):
    """The `//! flags:` line with NEW_ROOT in place of OLD_ROOT, given once."""
    match = FLAGS.match(line)
    if not match:
        return line
    words = match.group("words").split()
    if OLD_ROOT not in words and NEW_ROOT not in words:
        return line
    pairs = []
    i = 0
    while i < len(words):
        if words[i] == "-I" and i + 1 < len(words):
            pairs.append(("-I", NEW_ROOT if words[i + 1] == OLD_ROOT else words[i + 1]))
            i += 2
        else:
            pairs.append((words[i],))
            i += 1
    kept = []
    for pair in pairs:
        if pair == ("-I", NEW_ROOT) and pair in kept:
            continue
        kept.append(pair)
    text = " ".join(word for pair in kept for word in pair)
    return "%s %s%s" % (match.group("lead"), text, match.group("end"))


def rewrite_text(text, segments, dirs):
    """The text of one file with each short compiler import in the long spelling."""
    lines = text.splitlines(keepends=True)
    imports = set(header_imports(lines))
    out = []
    for index, line in enumerate(lines):
        if line.startswith("//! flags:"):
            out.append(rewrite_flags(line))
            continue
        match = IMPORT.match(line) if index in imports else None
        if match and is_compiler_import(match.group("path"), segments, dirs):
            line = "%s%s.%s%s" % (match.group("lead"), LIBRARY, match.group("path"),
                                  match.group("rest"))
        out.append(line)
    return "".join(out)


def collect(root):
    paths = set()
    for pattern in SOURCE_GLOBS:
        paths.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(paths)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="repository root (default: the parent of tools/)")
    parser.add_argument("--check", action="store_true",
                        help="write nothing; exit 1 when a file would change")
    parser.add_argument("paths", nargs="*", type=Path, help="the files to rewrite")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    segments = library_segments(root)
    if LIBRARY in segments:
        sys.exit("long_imports: %s/%s.ft or %s/%s/ exists, so a long import is ambiguous"
                 % (LIBRARY_ROOT, LIBRARY, LIBRARY_ROOT, LIBRARY))
    paths = [p.resolve() for p in args.paths] if args.paths else collect(root)
    changed = []
    for path in paths:
        text = path.read_text()
        new = rewrite_text(text, segments, local_dirs(root, path))
        if new != text:
            changed.append(path.relative_to(root).as_posix())
            if not args.check:
                path.write_text(new)
    verb = "would change" if args.check else "changed"
    for name in changed:
        print("%s %s" % (verb, name))
    print("long_imports: %d of %d files %s" % (len(changed), len(paths), verb))
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
