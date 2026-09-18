#!/usr/bin/env python3
"""Check names and line widths in project fort sources.

The tool reads `fort --index` output instead of parsing fort again. It checks each imported
file once and reports problems as `<file>:<line>:<col>: <message>`.

Use `fort_lint.py --fort build/<preset>/fort [-I dir ...] [file.ft ...]`.
Without file arguments, the tool checks SOURCE_SETS except SKIPPED.
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# Pair each source group with the search roots that resolve its imports.
# Language server modules use the `src` root, so their paths keep the `lsp.` prefix.
SOURCE_SETS = (
    ("std/*.ft", ()),
    ("std/linux/*.ft", ()),
    ("src/fort/*.ft", ()),
    ("src/lsp/*.ft", ("src", "src/fort")),
    ("test/fort/*.ft", ("src", "src/fort", "test/fort/support")),
    ("test/fort/support/*.ft", ("src", "src/fort", "test/fort/support")),
)
SOURCE_GLOBS = tuple(glob for glob, _ in SOURCE_SETS)

# A file named on the command line is checked even when this tuple excludes it.
SKIPPED = ()
MAX_COLUMNS = 100

LOWER_CASE = re.compile(r"\A[a-z_][a-z0-9_]*\Z")
UPPER_CASE = re.compile(r"\A[A-Z_][A-Z0-9_]*\Z")
# The head of a declared type is a possibly
# qualified name, before any `mut`, `own`, `*`, `@` or `[`.
TYPE_HEAD = re.compile(r"\A[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*")

# These identifier index kinds use project names. `constant` is a module constant, so it
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
# An `extern fn` keeps the foreign C symbol. A builtin keeps its language-defined name.
EXEMPT_KINDS = ("extern fn", "builtin")
# A variable cannot use its type name. A field can use it.
SHADOW_KINDS = ("global", "local", "parameter")


def collect(root, globs):
    """Return the sorted list of files under root matching any of the globs."""
    files = set()
    for pattern in globs:
        files.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(files)


def default_file_set(root, sets=SOURCE_SETS):
    """Return the default (path, include roots) pairs, sorted by path.

    A file matched by two globs takes the roots of the first matching glob.
    `test/fort/*.ft` and the support glob do not overlap, but a future pair
    could overlap. Thus, table order defines the set. Filesystem order does not.
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
    """Return a declared name's case problem, or None."""
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
    # Report an unknown kind instead of silently exempting it.
    return "kind '%s' of '%s' is in no table of tools/fort_lint.py" % (kind, name)


def type_base_name(text):
    """The name of the type a declaration spells, or None when it names none.

    `str_buf mut*` and `strbuf.str_buf` both name `str_buf`.
    A function type, module, or struct declaration names no variable type here.
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
    """Return a module filename problem, or None."""
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
    """Return whether the file contains fort source text."""
    for line in text.split("\n"):
        stripped = line.strip()
        if stripped and not stripped.startswith("//"):
            return True
    return False


def real_path(root, recorded, cache):
    """The real path of a file name a record or the file set spells.

    An import resolves to the library copy beside the compiler. Thus, one
    module closure also has records below build/<preset>/std. Real paths make
    different spellings of one file equal. The cache holds one entry for each
    distinct name. One run names 33000 records across 40 files, and
    os.path.realpath calls the system for each name component. The recorded
    name is the cache key, so one cache serves one root. Each caller passes the
    root that built the cache.
    """
    resolved = cache.get(recorded)
    if resolved is None:
        resolved = os.path.realpath(os.path.join(root, recorded))
        cache[recorded] = resolved
    return resolved


def same_file(root, recorded, wanted):
    """Whether an index record's file is the file being linted.

    Production code groups a complete closure through `group_records`.
    """
    cache = {}
    return real_path(root, recorded, cache) == real_path(root, wanted, cache)


def diagnostic_problems(document):
    """The checker's own diagnostics, in their own file order.

    An error can be in an imported module, so each error carries its position.
    The lint reports it against the requested file's head. It sorts errors by
    position, not by line text. Thus, a module with twenty errors lists them in
    file order.
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

    One run indexes a full import closure. Each record names its source file.
    """
    buckets = {}
    for record in document.get("symbols", []):
        buckets.setdefault(real_path(root, record["file"], cache), []).append(record)
    return buckets


def file_problems(records, diagnostics, has_source_text=True):
    """The problems one run reports about one file, sorted by position.

    A rejected file can still contain resolved index records. Check those records.
    Diagnostics disable the empty-index guard because an early error can produce no records.
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
    # The index already uses position order within a file, but the
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

    The compiler exits 0 for a clean file and 1 for reported diagnostics.
    Another status or malformed output indicates a broken environment.
    """
    command = [str(fort), "--index"]
    if std_dir is not None:
        # A compiler outside the build tree needs an explicit standard directory.
        command.extend(["--std-dir", str(std_dir)])
    for include in includes:
        # The compiler takes one search root per `-I`. They are spelled
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
        # Status 1 means "reported a diagnostic". Without one the run
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

    `std_dir` applies to the complete run, not one entry. It comes from the
    command line. It names the standard library when the build does not put it
    beside the compiler. The ctest `fort_lint_float` runs stage2 this way.
    Therefore, each entry uses the same value. It cannot change the comparison
    between two files' search roots.

    One `fort --index` run indexes the full import closure and names each record's file.
    Thus, the run judges each file of the
    set that its closure holds. The loop takes the first file it has not judged
    as the next entry and judges the closure with it. This avoids checking the
    same closure once for each member.

    A failed run costs its entry the index, but not checks that read only text.
    Each other file remains for a separate run. The run's diagnostics go to
    each file that it judges. Thus, a broken module names itself once in each
    closure file. This reports at least as much as one run per file. The sort
    is stable and uses position. Two problems at one position keep rule order.
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
                # that closure. Thus, a run judges a file only when the roots
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

    A mistyped glob, renamed directory, or empty `std/` could make the lint
    inspect nothing. It would print "0 file(s), no violation" and exit 0.
    Therefore, the complete set must not be empty. A glob with an existing
    directory must match a file. A glob whose directory does not yet exist is
    valid.
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
