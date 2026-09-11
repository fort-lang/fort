#!/usr/bin/env python3
"""Check the identifier conventions of D1.4 in the project's fort sources.

The names are not the compiler's business (D1.4 says so: "not enforced by the
compiler"), but they are the project's, and until now nothing but review held
`std/*.ft` and `src/fort/*.ft` to them. This tool reuses the compiler's own
front end rather than tokenizing fort a second time: `fort --index file.ft`
emits one record per resolved identifier occurrence with its kind, its type
and whether it is the declaration (D20.2, D20.3), so the lint reads the
checker's answer about what a name denotes instead of guessing it from text.
A second tokenizer is a second thing that can disagree with the language.

The rules, all from D1.4:
  * a module-level constant is UPPER_CASE;
  * every other name the project chooses -- module, function, struct, enum,
    enum member, field, global, local, parameter and the `as` alias of an
    import -- is lower_case with underscores;
  * a variable never takes its type's name (`point p`, never `point point`),
    while a field may (`node* node;`), since fields live in no namespace a
    type could occupy (D7.9).
An `extern fn` is exempt from the first two: its name is the C symbol's, fixed
by the library it binds, and renaming it would break the link.

It also holds a line to 100 columns, which is the house style of every other
language in the repository (D1.3 for markdown, ColumnLimit in .clang-format,
ruff for Python) rather than a decision about fort; MAX_COLUMNS is the one
place to change if that reading is wrong.

Usage: fort_lint.py --fort build/<preset>/fort [file.ft ...]. With no file it
checks std/*.ft and src/fort/*.ft. Problems print as <file>:<line>:<col>:
<message> and the exit status is 1 when it reported anything.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

SOURCE_GLOBS = ("std/*.ft", "src/fort/*.ft")
MAX_COLUMNS = 100

LOWER_CASE = re.compile(r"\A[a-z_][a-z0-9_]*\Z")
UPPER_CASE = re.compile(r"\A[A-Z_][A-Z0-9_]*\Z")
# The head of a type as a declaration spells it (D5.2, D5.3): a possibly
# qualified name, before any `mut`, `own`, `*`, `@` or `[`.
TYPE_HEAD = re.compile(r"\A[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*")

# The kinds of D20.3 whose spelling the project chooses, and the case each one
# takes. `constant` is a module-level constant and nothing else (D7.10), so it
# is the only UPPER_CASE kind; `extern fn` and `builtin` are named elsewhere
# and appear in neither table.
LOWER_KINDS = (
    "module",
    "fn",
    "struct",
    "enum",
    "enum member",
    "field",
    "global",
    "local",
    "parameter",
)
UPPER_KINDS = ("constant",)
# The kinds D1.4 does not reach. An `extern fn` carries the name of the C
# symbol the foreign library chose, which renaming would unlink; a `builtin` is
# named by the language (D12). Together with the two tables above these are
# every kind of D20.3, which test/fort_lint_test.py holds against the decision
# log so a thirteenth kind cannot be exempted by being forgotten.
EXEMPT_KINDS = ("extern fn", "builtin")
# A variable never takes its type's name; a field may (D1.4, D7.9).
SHADOW_KINDS = ("global", "local", "parameter")


def collect(root, globs):
    """Return the sorted list of files under root matching any of the globs."""
    files = set()
    for pattern in globs:
        files.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(files)


def name_problem(kind, name):
    """The case violation of one declared name, or None (D1.4)."""
    if kind in UPPER_KINDS:
        if not UPPER_CASE.match(name):
            return "%s '%s' is not UPPER_CASE (D1.4)" % (kind, name)
        return None
    if kind in LOWER_KINDS:
        if not LOWER_CASE.match(name):
            return "%s '%s' is not lower_case (D1.4)" % (kind, name)
        return None
    if kind in EXEMPT_KINDS:
        return None
    # A kind the tables do not know is reported rather than exempted: the index
    # gained one (D20.3) and this file has not caught up.
    return "kind '%s' of '%s' is in no table of tools/fort_lint.py" % (kind, name)


def type_base_name(text):
    """The name of the type a declaration spells, or None when it names none.

    `str_buf mut*` is `str_buf` and `strbuf.str_buf` is `str_buf`, since what
    D1.4 forbids is the type's own name and the module qualifier is a binding
    a local may shadow (D7.9). A function type (`fn i32(i32)`) names nothing,
    and neither does the empty type of a module or a struct name.
    """
    if not text:
        return None
    head = TYPE_HEAD.match(text.strip())
    if head is None:
        return None
    parts = head.group(0).split(".")
    if parts[0] == "fn":
        return None
    return parts[-1]


def shadow_problem(kind, name, type_text):
    """The violation of "a variable never takes its type's name", or None."""
    if kind not in SHADOW_KINDS:
        return None
    if name != type_base_name(type_text):
        return None
    return "%s '%s' takes the name of its type (D1.4)" % (kind, name)


def record_problems(record):
    """The problems of one index record, in order; a use carries none."""
    if not record.get("is_decl"):
        return []
    kind = record["kind"]
    name = record["name"]
    problems = []
    case = name_problem(kind, name)
    if case is not None:
        problems.append(case)
    shadow = shadow_problem(kind, name, record.get("type"))
    if shadow is not None:
        problems.append(shadow)
    return problems


def module_name_problem(path):
    """A module is named by its file, so the stem carries the rule (D9.1, D1.4)."""
    stem = Path(path).stem
    if LOWER_CASE.match(stem):
        return None
    return "module '%s' is not lower_case (D1.4)" % stem


def width_problems(text):
    """Every line over MAX_COLUMNS, as (line, col, message)."""
    problems = []
    for lineno, line in enumerate(text.split("\n"), start=1):
        if len(line) > MAX_COLUMNS:
            problems.append(
                (
                    lineno,
                    MAX_COLUMNS + 1,
                    "line is %d columns, over the %d of the house style" % (len(line), MAX_COLUMNS),
                )
            )
    return problems


def has_source_text(text):
    """Whether the file holds anything but blank lines and `//` comments (D2.2)."""
    for line in text.split("\n"):
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            return True
    return False


def same_file(root, recorded, wanted):
    """Whether an index record's file is the file being linted.

    An import resolves to the copy of the library beside the compiler, so the
    closure of one module holds records of files under build/<preset>/std as
    well; each is linted when it is the entry itself. Comparing real paths
    keeps a record that spells the same file differently from being dropped.
    """
    return os.path.realpath(os.path.join(root, recorded)) == os.path.realpath(
        os.path.join(root, wanted)
    )


def diagnostic_problems(document):
    """The checker's own diagnostics, in their own file order.

    The error may sit in an imported module, so each carries its own position
    and the lint reports it against the head of the file it was asked about.
    They are sorted by the position they name rather than by the text of the
    line, so a module with twenty errors lists them in file order.
    """
    diagnostics = document.get("diagnostics", [])
    keyed = sorted(
        (
            (d.get("file", ""), d.get("line", 1), d.get("col", 1), d.get("message", ""))
            for d in diagnostics
        )
    )
    return [(1, 1, "fort could not check this closure: %s:%d:%d: %s" % row) for row in keyed]


def document_problems(document, root, relative, has_source_text=True):
    """The problems the index document reports about one file, sorted.

    A file the checker rejected is still indexed for everything that resolved
    (D20.3), so the rules run over those records too: one broken module must
    not blind the lint for itself or for every module that imports it. What the
    diagnostics do switch off is the empty-index guard, since a syntax or
    lexical error stops the file before the checker (D14.2) and leaves no
    record at all.
    """
    problems = diagnostic_problems(document)
    seen = set()
    records = 0
    found = []
    for record in document.get("symbols", []):
        if not same_file(root, record["file"], relative):
            continue
        records += 1
        for message in record_problems(record):
            key = (record["line"], record["col"], message)
            if key in seen:
                continue
            seen.add(key)
            found.append(key)
    # The index is already in position order within a file (D20.3), but the
    # tool must not depend on that to report in it.
    found.sort(key=lambda problem: (problem[0], problem[1]))
    problems.extend(found)
    if records == 0 and not problems and has_source_text:
        # The guard against a silent pass: a file whose records the filter
        # above never matched would be linted by nothing at all. A file that
        # declares nothing (an empty module, a stub of comments) is not that
        # case, and a file with a diagnostic has its reason already.
        problems.append((1, 1, "fort --index reported no identifier in this file"))
    return problems


def index_document(fort, root, relative):
    """Run `fort --index` over one file and return (document, error).

    The compiler exits 0 with an empty diagnostics array on a clean file and 1
    when it reported one (D20.2); any other status, or output that is not a
    document, is a broken environment rather than a lint verdict.
    """
    proc = subprocess.run(
        [str(fort), "--index", str(relative)],
        cwd=str(root),
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode not in (0, 1):
        return None, "fort exited with status %d: %s" % (proc.returncode, proc.stderr.strip())
    try:
        document = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        return None, "fort --index wrote no document (%s)" % error
    if proc.returncode == 1 and not document.get("diagnostics"):
        # Status 1 means "reported a diagnostic" (D20.2). Without one the run
        # died of something else -- a sanitizer report, say -- and its index
        # says nothing about the file.
        return (
            None,
            "fort exited with status 1 but reported no diagnostic: %s" % proc.stderr.strip(),
        )
    return document, None


def lint_file(fort, root, path):
    """Return the formatted problems of one file, in source order.

    A broken environment costs the file its index, not the checks that read
    only its text: the width and module-name problems already computed are
    reported beside the failure rather than thrown away. The sort is by
    position and stable, so two problems at one position keep the order the
    rules produced them in.
    """
    relative = os.path.relpath(str(path), str(root))
    problems = []
    name = module_name_problem(path)
    if name is not None:
        problems.append((1, 1, name))
    text = Path(path).read_text(encoding="utf-8")
    problems.extend(width_problems(text))
    document, error = index_document(fort, root, relative)
    if error is not None:
        problems.append((1, 1, error))
    else:
        problems.extend(document_problems(document, root, relative, has_source_text(text)))
    problems.sort(key=lambda problem: (problem[0], problem[1]))
    return ["%s:%d:%d: %s" % (relative, line, col, message) for line, col, message in problems]


def empty_set_problems(root, paths):
    """The reasons the default file set is not the one this tool means to check.

    A mistyped glob, a renamed directory or a `std/` emptied by a bad merge
    would otherwise make the whole lint vacuous: it would print "0 file(s), no
    violation" and exit 0. So the set as a whole must not be empty, and a glob
    whose directory exists must match something; a glob whose directory does
    not exist yet (`src/fort/*.ft` before Phase B fills it) is not a mistake.
    """
    problems = []
    if not paths:
        problems.append("fort_lint: no fort source matched %s" % ", ".join(SOURCE_GLOBS))
        return problems
    for pattern in SOURCE_GLOBS:
        directory = root / os.path.dirname(pattern)
        if not directory.is_dir():
            continue
        if not collect(root, (pattern,)):
            problems.append("fort_lint: %s exists but no file matched %s" % (directory, pattern))
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler binary to read the index from")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: the parent of tools/)",
    )
    parser.add_argument("paths", nargs="*", type=Path, help="the files to check")
    args = parser.parse_args(argv)

    if args.paths:
        paths = args.paths
        empty = []
    else:
        paths = collect(args.root, SOURCE_GLOBS)
        empty = empty_set_problems(args.root, paths)
    if empty:
        for problem in empty:
            print(problem, file=sys.stderr)
        return 1

    problems = []
    files_with_problems = 0
    for path in paths:
        found = lint_file(args.fort, args.root, path)
        if found:
            files_with_problems += 1
        problems.extend(found)
    for problem in problems:
        print(problem)
    if problems:
        sys.stdout.flush()
        print(
            "fort_lint: %d problem(s) in %d of %d file(s)"
            % (len(problems), files_with_problems, len(paths)),
            file=sys.stderr,
        )
        return 1
    print("fort_lint: %d file(s), no violation of D1.4" % len(paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())
