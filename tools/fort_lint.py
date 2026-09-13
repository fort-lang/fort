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

A file that imports a module of the compiler's own tree needs the search
roots that module lives under, which `fort` takes as `-I` (D9.2): a
`test/fort/<x>_test.ft` imports `containers` from `src/fort` and its fixtures
from `test/fort/support`, so without them every such file reports `module
'containers' not found` and its names go unchecked. SOURCE_SETS pairs each
default glob with the roots its files need, and `-I` on the command line adds
roots for the files named there.

One run of `fort --index` indexes the whole import closure of the file it
names, and each record carries the file it came from, so one run judges every
file of the set that the closure holds. The tool takes the first file it has
not judged as the next entry. A file that no closure reaches becomes an entry
itself, so every file of the set is judged.

Usage: fort_lint.py --fort build/<preset>/fort [-I dir ...] [file.ft ...].
With no file it checks the globs of SOURCE_SETS, less SKIPPED. Problems print as
<file>:<line>:<col>: <message> and the exit status is 1 when it reported
anything.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# The default file set: one glob per group of sources, with the module search
# roots (`-I`, D9.2) a file of that group needs to resolve its imports. The
# standard library resolves through the copy beside the compiler and needs
# none; a `test/fort` test imports the compiler's modules from `src/fort` and
# its shared fixtures from `test/fort/support` (the `-I ../../src/fort -I
# support` its own directives carry, spelled from the repository root, which is
# this tool's working directory). `test/fort/support/*.ft` is in the set too:
# it is fort the project wrote and D1.4 reaches it like any other.
SOURCE_SETS = (
    ("std/*.ft", ()),
    ("src/fort/*.ft", ()),
    ("test/fort/*.ft", ("src/fort", "test/fort/support")),
    ("test/fort/support/*.ft", ("src/fort", "test/fort/support")),
)
SOURCE_GLOBS = tuple(glob for glob, _ in SOURCE_SETS)

# The sources the default set leaves out, with the reason. `std/rt_float.ft`
# and `std/math.ft` hold floats, which the C bootstrap rejects (D18.1, T-042),
# and the ctest `fort_lint` runs this tool with that compiler; the ctest
# `fort_lint_float` runs it with stage2 over exactly these files, so nothing
# goes unlinted. A file named on the command line is linted whatever this tuple
# says, which is how that second ctest reaches them.
SKIPPED = ("std/math.ft", "std/rt_float.ft")
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


def default_file_set(root, sets=SOURCE_SETS):
    """Return the default (path, include roots) pairs, sorted by path.

    A file matched by two globs -- `test/fort/*.ft` and the support glob do not
    overlap today, but a future pair could -- takes the roots of the first glob
    that matched it, so the set is a function of the table's order and not of
    the filesystem's.
    """
    skipped = {(root / name).resolve() for name in SKIPPED}
    chosen = {}
    for pattern, includes in sets:
        for path in collect(root, (pattern,)):
            if path.resolve() in skipped:
                continue
            chosen.setdefault(path, tuple(includes))
    return [(path, chosen[path]) for path in sorted(chosen)]


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


def real_path(root, recorded, cache):
    """The real path of a file name a record or the file set spells.

    An import resolves to the copy of the library beside the compiler, so the
    closure of one module holds records of files under build/<preset>/std as
    well. Real paths make a record that spells the same file differently equal
    to it. The cache holds one entry per distinct name: one run names 33000
    records over 40 files, and os.path.realpath is a syscall for each component
    of each name. The cache is keyed on the recorded name alone, so one cache
    serves one root; every caller passes the root it built the cache with.
    """
    resolved = cache.get(recorded)
    if resolved is None:
        resolved = os.path.realpath(os.path.join(root, recorded))
        cache[recorded] = resolved
    return resolved


def same_file(root, recorded, wanted):
    """Whether an index record's file is the file being linted.

    No production path calls this since T-095, which reads the records of a
    whole closure through group_records instead. It stays as the one-pair form
    of the comparison, and test/fort_lint_test.py tests it.
    """
    cache = {}
    return real_path(root, recorded, cache) == real_path(root, wanted, cache)


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


def group_records(document, root, cache):
    """The document's records, in a dict keyed by the real path of their file.

    One run indexes the whole import closure (D20.3) and every record names the
    file it came from, so one pass over the records serves every file of the
    closure. The lint reads the dict once per file instead of reading the whole
    record list once per file.
    """
    buckets = {}
    for record in document.get("symbols", []):
        buckets.setdefault(real_path(root, record["file"], cache), []).append(record)
    return buckets


def file_problems(records, diagnostics, has_source_text=True):
    """The problems one run reports about one file, sorted by position.

    A file the checker rejected is still indexed for everything that resolved
    (D20.3), so the rules run over those records too: one broken module must
    not blind the lint for itself or for every module that imports it. What the
    diagnostics do switch off is the empty-index guard, since a syntax or
    lexical error stops the file before the checker (D14.2) and leaves no
    record at all.
    """
    problems = list(diagnostics)
    seen = set()
    found = []
    for record in records:
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
    if not records and not problems and has_source_text:
        # The guard against a silent pass: a file whose records the filter
        # above never matched would be linted by nothing at all. A file that
        # declares nothing (an empty module, a stub of comments) is not that
        # case, and a file with a diagnostic has its reason already.
        problems.append((1, 1, "fort --index reported no identifier in this file"))
    return problems


def document_problems(document, root, relative, has_source_text=True):
    """The problems one document reports about one file, sorted.

    The one-file form of file_problems, for a caller that holds a document and
    asks about a single file.
    """
    cache = {}
    records = group_records(document, root, cache).get(real_path(root, relative, cache), [])
    return file_problems(records, diagnostic_problems(document), has_source_text)


def index_document(fort, root, relative, includes=(), std_dir=None):
    """Run `fort --index` over one file and return (document, error).

    The compiler exits 0 with an empty diagnostics array on a clean file and 1
    when it reported one (D20.2); any other status, or output that is not a
    document, is a broken environment rather than a lint verdict.
    """
    command = [str(fort), "--index"]
    if std_dir is not None:
        # A compiler that is not the one the build put the library beside needs
        # the directory named, as the driver's --std-dir does (toolchain.md 1).
        command.extend(["--std-dir", str(std_dir)])
    for include in includes:
        # The compiler takes one search root per `-I` (D9.2); they are spelled
        # relative to the working directory, which is the repository root.
        command.extend(["-I", str(include)])
    command.append(str(relative))
    proc = subprocess.run(
        command,
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


def text_problems(relative, text):
    """The problems the file's own text carries: its module name and its width."""
    problems = []
    name = module_name_problem(relative)
    if name is not None:
        problems.append((1, 1, name))
    problems.extend(width_problems(text))
    return problems


def lint_files(fort, root, files, std_dir=None):
    """Return the formatted problems of each file, and the number of runs.

    `std_dir` is the whole run's, never one entry's: it comes from the command
    line and names the standard library for a compiler the build did not put it
    beside (the ctest `fort_lint_float` runs stage2 that way). It is therefore
    the same for every entry and cannot change the rule below, which compares
    the search roots of two files.

    One `fort --index` run indexes the whole import closure of its entry and
    names the file of every record (D20.3), so the run judges every file of the
    set that its closure holds. The loop takes the first file it has not judged
    as the next entry and judges the closure with it. One run per file instead
    re-checks each closure once per member, which is O(n^2) checker work: the
    default set of 166 files took 166 runs and 46.9 s under the debug preset,
    and took 141 runs and 14.9 s this way (T-095, which measured it). The set
    is 175 files and 149 runs today, since T-041 added two sources.

    A run that fails costs its entry the index, not the checks that read only
    the text, and leaves every other file of the set for a run of its own. The
    run's diagnostics go to every file the run judges, so a broken module names
    itself once in each file of its closure; that reports more than one run per
    file did, never less. The sort is by position and stable, so two problems at
    one position keep the order the rules produced them in.
    """
    cache = {}
    entries = []
    for path, includes in files:
        relative = os.path.relpath(str(path), str(root))
        text = Path(path).read_text(encoding="utf-8")
        entries.append((relative, tuple(includes), real_path(root, relative, cache), text))
    reports = [None] * len(entries)
    runs = 0
    for index, (relative, includes, _, _) in enumerate(entries):
        if reports[index] is not None:
            continue
        document, error = index_document(fort, root, relative, includes, std_dir)
        runs += 1
        if error is not None:
            reports[index] = [(1, 1, error)]
            continue
        buckets = group_records(document, root, cache)
        diagnostics = diagnostic_problems(document)
        for other in range(index, len(entries)):
            if reports[other] is not None:
                continue
            records = buckets.get(entries[other][2])
            if other != index and (not records or entries[other][1] != includes):
                # The whole design rests on one fact: a file's records depend on
                # that file's own import closure and on nothing wider, so the
                # records of a file in this document are the records its own run
                # would give. The search roots are the one thing that can change
                # that closure (D9.2), so a run judges a file only when the roots
                # agree. The default set loses no run to the rule: std/ resolves
                # through the copy beside the compiler, which is another file,
                # and the test/fort tests carry the roots of test/fort/support.
                continue
            source = has_source_text(entries[other][3])
            reports[other] = file_problems(records or [], diagnostics, source)
    lines = []
    for index, (relative, _, _, text) in enumerate(entries):
        problems = text_problems(relative, text) + reports[index]
        problems.sort(key=lambda problem: (problem[0], problem[1]))
        lines.append(
            ["%s:%d:%d: %s" % (relative, line, col, message) for line, col, message in problems]
        )
    return lines, runs


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
        "--std-dir",
        default=None,
        help="the standard library directory (default: the compiler's own)",
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: the parent of tools/)",
    )
    parser.add_argument(
        "-I",
        "--include",
        action="append",
        default=[],
        metavar="DIR",
        help="a module search root to pass to fort (D9.2); repeatable",
    )
    parser.add_argument("paths", nargs="*", type=Path, help="the files to check")
    args = parser.parse_args(argv)

    if args.paths:
        files = [(path, tuple(args.include)) for path in args.paths]
        empty = []
    else:
        files = default_file_set(args.root)
        for index, (path, includes) in enumerate(files):
            files[index] = (path, tuple(args.include) + includes)
        empty = empty_set_problems(args.root, [path for path, _ in files])
    if empty:
        for problem in empty:
            print(problem, file=sys.stderr)
        return 1

    problems = []
    files_with_problems = 0
    reports, _ = lint_files(args.fort, args.root, files, args.std_dir)
    for found in reports:
        if found:
            files_with_problems += 1
        problems.extend(found)
    for problem in problems:
        print(problem)
    if problems:
        sys.stdout.flush()
        print(
            "fort_lint: %d problem(s) in %d of %d file(s)"
            % (len(problems), files_with_problems, len(files)),
            file=sys.stderr,
        )
        return 1
    print("fort_lint: %d file(s), no violation of D1.4" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
