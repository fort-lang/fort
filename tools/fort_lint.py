#!/usr/bin/env python3
"""Check names, line widths and the audit rows of D9.8 in project fort sources.

The tool reads `fort --index` output instead of parsing fort again. It checks each imported
file once and reports problems as `<file>:<line>:<col>: <message>`.

The rows of D9.8: each `extern fn` of a trusted module has a declaration row, the call rows of
each function of a trusted module count each call of an `extern fn`, of a boundary function and
through a function value, and a name of a boundary function goes only to C (row_problems).
A call row that cites `the gives: of <callee>` or `the writes: of <callee>` names a callee that
the body calls, a fort function of a trusted module whose row holds a line with that word
(citation_problems). Text that starts a citation and that the lint cannot read as one is a
problem too, and its message names that text. A function that is no boundary function gives no
`mut` global whose type reaches a byte pointer to a call of C (mut_global_to_c).

The lint does not read the sources of a row. It does not check a bound, a length or a NUL. A
row that claims the bound of the whole array for `&g[0][1][0]` of a `char@[1][2] g` passes. A
reviewer reads that row against rule 2. The lint sees a `mut` global only by its name, anywhere
between the brackets of the call. The name can be an argument, or stand inside a cast, a nested
call, an index, or a struct or array literal. The lint does not see a value that a local copies
from a `mut` global first. It does not see a value that another function takes from a `mut`
global and returns or stores, as `strlen(get())` for `fn get() char* { return name; }`. It does
not see a length or a NUL of a `mut` global whose type reaches no byte pointer. It does not see
a `mut` global between the brackets of a call through a function pointer that C gave. It does
see a `mut` global between the brackets of a call whose callee holds a boundary function as a
value, as `(first ? take : other)(name)`.

One function reads the callee of a call, through each group around it (call_callee), and each
check takes the callee from it: `(strlen)(name)` is a call of `strlen` for each check, and its
call row names the callee `(strlen)`. A citation names its callee in the same forms, so the
same function reads it. A keyword is the callee of no call (KEYWORDS).

A boundary function is found by the types of its parameters in the source, with the structs that
the index records resolve, so the answer is the checker's own for each struct and each name
(Rows.reaches).

Use `fort_lint.py --fort build/<Host>/<preset>/fort [--target linux|darwin] [-I dir ...]
[file.ft ...]`.
Without file arguments, the tool checks the source sets of the target except SKIPPED. The
target selects `std/<target>/**/*.ft`: a standard root declares one libc, so each target's
modules are checked under the root of that target. Each set reads nested directories, so
`src/fort/fir/flow.ft` gets the checks of `src/fort/ast.ft`. A file of `std/` checks as a module
`std.<name>` of the standard root of its target, which symbolic links to the files of `std/`
make, so a list of files may name one module of `std/` or the modules of both targets. A module
that both targets share checks under the root of the target.

The files of `test/lang/run` and `test/lang/programs` get the checks of D9.8 only: a test
exercises the language, so the names, the width and the rules of notes/style.md 3.1 do not
apply to it (ROWS_ONLY). Each such file is indexed with the `--cfg` options of the `//! flags:`
directive of its test (lang_options).

Source is UTF-8 (D2.1). The lint reports the first byte of a file that is no UTF-8 text, and
makes each other check of that file too (source_text).

The naming checks of notes/style.md 3.1 apply to a file of `src/fort`, `src/lsp` and `std`
by its path. The prefix check applies in each directory of those roots. The function and
glossary checks apply to a nested module only: a file in a subdirectory of `src/fort`,
`src/lsp` or `std`, outside the target directories. The flat modules keep their names until
they move.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Pair each source group with the search roots that resolve its imports.
# Language server modules use the `src` root, so their paths keep the `lsp.` prefix.
# A module of src/fort imports its siblings as `fort.<path>`, as the library `fort` spells
# them (D9.11), so it takes the root `src` too, flat or nested. The first matching glob gives a
# file its roots.
# STD_COMMON skips the directories of TARGETS; the target glob adds the one of the target.
STD_COMMON = "std/**/*.ft"
STD_TARGET = "std/linux/**/*.ft"
SOURCE_SETS = (
    (STD_COMMON, ()),
    (STD_TARGET, ()),
    ("src/fort/**/*.ft", ("src",)),
    ("src/lsp/**/*.ft", ("src",)),
    ("test/fort/**/*.ft", ("src", "test/fort/support")),
    ("test/lsp/**/*.ft", ("src", "test/fort/support")),
    ("test/library/**/*.ft", ("src", "test/library/chain")),
    ("test/tty/**/*.ft", ()),
    ("test/lang/run/**/*.ft", ()),
    ("test/lang/programs/**/*.ft", ()),
)
# The trees whose files get the checks of D9.8 and the diagnostics of the compiler only. A
# language test exercises the language rather than the conventions (notes/testing.md 8).
ROWS_ONLY = ("test/lang/",)
SOURCE_GLOBS = tuple(glob for glob, _ in SOURCE_SETS)
TARGETS = ("linux", "darwin")


def source_sets(target):
    """SOURCE_SETS with `std/linux/**/*.ft` replaced by the modules of `target`."""
    return tuple(("std/%s/**/*.ft" % target, inc) if glob == STD_TARGET else (glob, inc)
                 for glob, inc in SOURCE_SETS)

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
# The naming rules of notes/style.md 3.1.
# PREFIX_ROOTS are the source roots whose directories get the file prefix check.
PREFIX_ROOTS = ("src/fort", "src/lsp", "std")
# A prefix that PREFIX_LIMIT files of one directory share asks for a directory.
PREFIX_LIMIT = 3
# The flat groups that a later change moves into directories, as (directory, prefix).
LEGACY_PREFIXES = (("src/fort", "fir"),)
# NAMING_ROOTS are the roots whose nested modules get the function and glossary checks.
# A flat module of `src/lsp`, `lsp.server`, is no nested module: its four `create` functions
# keep their names until it moves.
NAMING_ROOTS = ("src/fort", "src/lsp", "std")
# Each word that the glossary bans, and the word to use.
GLOSSARY = {
    "create": "make",
    "destroy": "free",
    "deinit": "free",
    "lookup": "find",
    "locate": "find",
    "search": "find",
    "cost": "work",
    "price": "work",
    "spend": "charge",
}
STYLE = "notes/style.md"
# An `extern fn` keeps the foreign C symbol. A builtin keeps its language-defined name.
EXEMPT_KINDS = ("extern fn", "builtin")
# A variable cannot use its type name. A field can use it.
SHADOW_KINDS = ("global", "local", "parameter")


def in_target_directory(root, path):
    """Whether the path is in `std/<target>/` for a target of TARGETS."""
    parts = path.relative_to(root).parts
    return len(parts) > 2 and parts[0] == "std" and parts[1] in TARGETS


def collect(root, globs):
    """Return the sorted list of files under root matching any of the globs.

    STD_COMMON matches no file of a target directory.
    """
    files = set()
    for pattern in globs:
        for path in root.glob(pattern):
            if not path.is_file():
                continue
            if pattern == STD_COMMON and in_target_directory(root, path):
                continue
            files.add(path)
    return sorted(files)


def glob_base(pattern):
    """The directory part of a glob before its first wildcard: `std` for `std/**/*.ft`."""
    parts = []
    for part in pattern.split("/"):
        if "*" in part:
            break
        parts.append(part)
    return "/".join(parts)


def default_file_set(root, sets=SOURCE_SETS):
    """Return the default (path, include roots) pairs, sorted by path.

    A file matched by two globs takes the roots of the first matching glob. No two globs of
    SOURCE_SETS overlap today: STD_COMMON skips the target directories. Thus, table order
    defines the set when two globs overlap. Filesystem order does not.
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


def module_segments(relative):
    """The segments of a nested module path below its naming root, or None.

    `src/fort/fir/flow.ft` gives `fir`, `flow` and `std/linux/sock/tcp.ft` gives `sock`,
    `tcp`, since std/<target>/ stands for std/. `src/lsp/a/b.ft` gives `lsp`, `a`, `b`. A
    flat module or a file outside NAMING_ROOTS gives None.
    """
    parts = Path(relative).with_suffix("").parts
    for naming_root in NAMING_ROOTS:
        base = tuple(naming_root.split("/"))
        if parts[: len(base)] != base:
            continue
        rest = parts[len(base):]
        if naming_root == "std" and len(rest) > 1 and rest[0] in TARGETS:
            rest = rest[1:]
        if len(rest) < 2:
            return None
        # A module of src/lsp is `lsp.<path>` through the search root `src`.
        return ("lsp",) + rest if naming_root == "src/lsp" else rest
    return None


def glossary_problem(kind, name):
    """The first word of a name that the glossary bans, as a problem, or None.

    A word ends at `_` or, in a module path, at `.`.
    """
    for word in re.split(r"[_.]", name.lower()):
        if word in GLOSSARY:
            return "%s '%s' uses the word '%s'; the glossary says '%s' (%s)" % (
                kind, name, word, GLOSSARY[word], STYLE)
    return None


def repeat_problem(kind, name, segments):
    """The problem of a function whose first word is a word of its module path, or None.

    Each segment splits at `_`, so `heap_free` in `owner/heap_state.ft` repeats `heap`.
    A doer module may start a function with its verb (`parser.parse_place`): the verb is not
    the module word, so the check passes it.
    """
    if kind != "fn":
        return None
    first = name.split("_")[0]
    words = {word for segment in segments for word in segment.split("_")}
    if first not in words:
        return None
    return "fn '%s' repeats the module word '%s' (%s)" % (name, first, STYLE)


def record_problems(record, segments=None):
    """The problems of one index record, in order; a use carries none.

    `segments` is the module path of a nested module (module_segments). It turns on the
    function and glossary checks of notes/style.md 3.1.
    """
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
    if segments is not None and kind not in EXEMPT_KINDS and kind != "module":
        # A `module` record names an imported module, which is judged by its own path.
        repeat = repeat_problem(kind, name, segments)
        if repeat is not None:
            problems.append(repeat)
        word = glossary_problem(kind, name)
        if word is not None:
            problems.append(word)
    return problems


def module_name_problem(path):
    """Return a module filename problem, or None."""
    stem = Path(path).stem
    if LOWER_CASE.match(stem):
        return None
    return "module '%s' is not lower_case (D1.4)" % stem


def prefix_problem(root, relative):
    """The problem of a module whose first word PREFIX_LIMIT files of its directory share.

    `src/fort/owner_state.ft` beside `owner_region.ft` and `owner_flow.ft` asks for the
    directory `src/fort/owner/`. A file named as the prefix, `owner.ft`, does not count.
    """
    path = Path(relative)
    directory = path.parent.as_posix()
    if not any(directory == r or directory.startswith(r + "/") for r in PREFIX_ROOTS):
        return None
    if "_" not in path.stem:
        return None
    prefix = path.stem.split("_")[0]
    if (directory, prefix) in LEGACY_PREFIXES:
        return None
    shared = [p for p in (Path(root) / directory).glob(prefix + "_*.ft") if p.is_file()]
    if len(shared) < PREFIX_LIMIT:
        return None
    return ("module '%s' shares the prefix '%s_' with %d other files of %s; put them in the "
            "directory %s/%s/ (%s)" % (path.stem, prefix, len(shared) - 1, directory,
                                       directory, prefix, STYLE))


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
    module closure also has records below build/<Host>/<preset>/std. Real paths make
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


def file_problems(records, diagnostics, has_source_text=True, segments=None, names=True):
    """The problems one run reports about one file, sorted by position.

    `names` False leaves out the problems of the records, the names of D1.4, for a file of
    ROWS_ONLY.

    A rejected file can still contain resolved index records. Check those records.
    Diagnostics disable the empty-index guard because an early error can produce no records.
    `segments` is the module path of a nested module, as record_problems takes it.
    """
    problems = list(diagnostics)
    seen = set()
    found = []
    for record in records if names else ():
        for message in record_problems(record, segments):
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
        # case. And a file with a diagnostic has its reason already.
        problems.append((1, 1, "fort --index reported no identifier in this file"))
    return problems


def document_problems(document, root, relative, has_source_text=True):
    """The problems one document reports about one file, sorted.

    The one-file form of file_problems, for a caller that holds a document and
    asks about a single file.
    """
    cache = {}
    records = group_records(document, root, cache).get(real_path(root, relative, cache), [])
    return file_problems(records, diagnostic_problems(document), has_source_text,
                         module_segments(relative))


def index_document(fort, root, relative, includes=(), std_dir=None, options=()):
    """Run `fort --index` over one file and return (document, error). `options` are more
    arguments of the compiler, the `--cfg` options of a language test (lang_options).

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
    command.extend(options)
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


def source_text(data):
    """The text of the bytes `data` of one file, and the problem of its first byte that is no
    UTF-8 text, or None (D2.1).

    The decoder gives U+FFFD for each such byte, so the text has each line of the file and the
    other checks read it. The column of the problem is a byte column, as a column of the index
    is (D20.3). A line end of the text is `\n`, also where the file holds `\r\n` or `\r`, as a
    read in text mode gives it.
    """
    problem = None
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        at = error.start
        line = data.count(b"\n", 0, at) + 1
        col = at - data.rfind(b"\n", 0, at)
        problem = (line, col, "byte 0x%02x is no UTF-8 text: fort source is UTF-8 (D2.1)"
                   % data[at])
        text = data.decode("utf-8", errors="replace")
    return text.replace("\r\n", "\n").replace("\r", "\n"), problem


def rows_only(relative):
    """Whether the file `relative` gets the checks of D9.8 only (ROWS_ONLY)."""
    name = Path(relative).as_posix()
    return any(name.startswith(prefix) for prefix in ROWS_ONLY)


# The marks of the lines of the header of a language test (test/lang/run_tests.py).
HEADER_MARKERS = ("//!", "//<", "//|")
# The directive of a language test that gives the compiler its options (spec/toolchain.md 7.2).
FLAGS_DIRECTIVE = re.compile(r"\A//! flags:(.*)\Z")


def lang_options(root, relative):
    """The `--cfg` options of the `//! flags:` directive of the test of the file `relative` of
    ROWS_ONLY, as a tuple of arguments, or (). A module of a directory test,
    `run/modules/<name>/dep.ft` or a module in a directory below it, takes the directive of the
    nearest `main.ft` above it. The header is the leading run of `//!`, `//<` and `//|` lines, as
    the harness reads it (test/lang/run_tests.py). A key that the options do not set stops the
    compile of its `$if` (D21.1), so the index needs them. A byte of the file that is no UTF-8
    text changes no directive: the lint reports it for a file that it checks (source_text)."""
    if not rows_only(relative):
        return ()
    path = Path(root) / relative
    top = Path(root) / "test" / "lang"
    folder = path.parent
    while folder != top and top in folder.parents:
        if (folder / "main.ft").is_file():
            path = folder / "main.ft"
            break
        folder = folder.parent
    options = []
    for line in source_text(path.read_bytes())[0].split("\n"):
        if line[:3] not in HEADER_MARKERS:
            break
        found = FLAGS_DIRECTIVE.match(line)
        if found is None:
            continue
        words = found.group(1).split()
        for at, word in enumerate(words):
            if word == "--cfg" and at + 1 < len(words):
                options.extend((word, words[at + 1]))
    return tuple(options)


def text_problems(relative, text, root=None):
    """The problems of the file's path and text: its module name, its prefix and its width.

    The prefix check reads the directory of the file under `root`. Without a root it is off.
    """
    problems = []
    name = module_name_problem(relative)
    if name is not None:
        problems.append((1, 1, name))
    if root is not None:
        prefix = prefix_problem(root, relative)
        if prefix is not None:
            problems.append((1, 1, prefix))
    segments = module_segments(relative)
    if segments is not None:
        word = glossary_problem("module", ".".join(segments))
        if word is not None:
            problems.append((1, 1, word))
    problems.extend(width_problems(text))
    return problems


# ---- the audit rows of D9.8 ------------------------------------------------------------------

# The five points of A7 (spec/ownership-design.md 5.1) that a declaration row gives, each once.
ROW_WORDS = ("gives:", "writes:", "keeps:", "calls:", "frees:")
# The first line of a declaration row: `// C: <the symbol>, <the source of its contract>.`
DECLARATION_ROW = re.compile(r"\A// C: ([A-Za-z_][A-Za-z0-9_]*), \S")
# A line that starts a declaration row, whatever it names.
DECLARATION_ROW_START = re.compile(r"\A// C:")
# The first line of a call row: `// C call <the callee>: ...`, or `// C call <the callee> x<N>:`
# for N calls of one callee that hold for one reason. The callee is the call as the body spells
# it, its module path included and its white space left out: `libc.write`, `str.from_cstr`,
# `fail_begin`, or the place of a function value, `cmp`, `table[i+1]`, `pick()` or
# `z->introspect->enumerator`.
CALL_ROW = re.compile(r"\A// C call (\S+?)(?: x(\d+))?:(?:\s|\Z)")
# A line that starts a call row, whatever it holds after the words.
CALL_ROW_START = re.compile(r"\A// C call\b")
# The first item `import extern;` that makes a module trusted (D9.3, D9.8).
TRUST_FORM = re.compile(r"\A\s*import\s+extern\s*;")
# The kinds of an index record whose value can be a function pointer that a call calls.
VALUE_KINDS = ("local", "parameter", "global", "field", "constant")
# The base types whose pointer is a byte pointer (D9.8): a pointer to one of them, any marks.
BYTE_BASES = ("char", "u8", "void")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
IDENTIFIER_CHAR = re.compile(r"[A-Za-z0-9_]")
# The 41 keywords and the 10 reserved words of spec/grammar.md (D2.4). No one of them is a name,
# so no one is the callee of a call: `if (`, `return (`, `cast(`, `sizeof(` and `fn (` open no
# argument list of a callee. `call_callee` reads this list, and a test holds it equal to the
# grammar.
KEYWORDS = frozenset((
    "as bool break case cast char continue default defer do else enum extern f32 f64 false fn "
    "for i8 i16 i32 i64 if import mut new noreturn null own return sizeof string struct switch "
    "true u8 u16 u32 u64 void while "
    "async await const match pub priv trait type union yield").split())
# The keywords of that list that can stand before a group that a body calls, and are no part of
# the callee: `return (k)(x)` and `defer (k)(x)` call `(k)`. No other keyword stands before such
# a group: `cast`, `sizeof` and `new` take a list of their own, and a block follows the list of
# `if`, `while`, `for` and `switch`.
STATEMENT_KEYWORDS = ("return", "defer")
ROWS = "D9.8"


class Source:
    """The text of one fort file, with the bytes of each comment and literal blanked.

    The file reads as Latin-1, so one character is one byte and an index record's byte column
    (D20.3) is a column of the text. `code` has the length of `text`: each byte of a comment, a
    string literal or a char literal is a space, and each newline stays, so a scan of `code`
    meets only the tokens of fort code and each offset keeps its line.
    """

    def __init__(self, text):
        self.text = text
        self.lines = text.split("\n")
        self.starts = [0]
        for line in self.lines[:-1]:
            self.starts.append(self.starts[-1] + len(line) + 1)
        self.code = blank_comments_and_literals(text)

    def offset(self, line, col):
        """The offset of a 1-based line and byte column."""
        return self.starts[line - 1] + col - 1

    def line_of(self, offset):
        """The 1-based line that holds `offset`."""
        low, high = 0, len(self.starts) - 1
        while low < high:
            middle = (low + high + 1) // 2
            if self.starts[middle] <= offset:
                low = middle
            else:
                high = middle - 1
        return low + 1

    def is_trusted_form(self):
        """Whether the first item is `import extern;`."""
        return TRUST_FORM.match(self.code) is not None


def blank_comments_and_literals(text):
    """`text` with the bytes of each `//` comment and each string or char literal as spaces."""
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            end = min(j + 1, n)
            for k in range(i, end):
                if out[k] != "\n":
                    out[k] = " "
            i = end
            continue
        i += 1
    return "".join(out)


def matching_close(code, open_at):
    """The offset of the bracket that closes the one at `open_at`, or -1."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    stack = []
    for i in range(open_at, len(code)):
        c = code[i]
        if c in pairs:
            stack.append(pairs[c])
        elif c in ")]}":
            if not stack or stack.pop() != c:
                return -1
            if not stack:
                return i
    return -1


def keyword_before(code, offset, word):
    """The offset of the last `word` token that starts before `offset`, or -1."""
    for found in reversed(list(re.finditer(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])" % word,
                                           code[:offset]))):
        return found.start()
    return -1


def comment_run(source, line):
    """The `//` lines directly above the 1-based `line`, top first, as (line, text) pairs.

    The run stops at a blank line, at a line of code, at `///` text and at a harness directive
    (`//!`, `//|`), so a row stands below the `///` text of its item (notes/style.md 1.2).
    """
    run = []
    number = line - 1
    while number >= 1:
        stripped = source.lines[number - 1].strip()
        if not stripped.startswith("//") or stripped[:3] in ("///", "//!", "//|"):
            break
        run.append((number, stripped))
        number -= 1
    run.reverse()
    return run


def type_tokens(code, start, end):
    """The tokens of the type text `code[start:end]`, as (text, offset) pairs."""
    tokens = []
    i = start
    while i < end:
        c = code[i]
        if c.isspace():
            i += 1
            continue
        word = IDENTIFIER.match(code, i)
        if word is not None and word.end() <= end:
            tokens.append((word.group(0), i))
            i = word.end()
            continue
        if c == "[":
            close = matching_close(code, i)
            if close < 0 or close >= end:
                close = end - 1
            tokens.append(("[]", i))
            i = close + 1
            continue
        tokens.append((c, i))
        i += 1
    return tokens


class TypeParser:
    """Reads one type of grammar.md 2 from its tokens into nested dicts.

    A type is {"base": base, "suffixes": ["*" | "@" | "[]", ...]}. A base is {"group": type},
    {"fn": [type, ...], "result": type}, or {"name": text, "at": offset of its last identifier}.
    The marks `own` and `mut` read as nothing, because reach reads no mark (D9.8).
    """

    def __init__(self, tokens):
        self.tokens = tokens
        self.at = 0

    def peek(self):
        return self.tokens[self.at][0] if self.at < len(self.tokens) else None

    def take(self):
        token = self.tokens[self.at]
        self.at += 1
        return token

    def marks(self):
        while self.peek() in ("own", "mut"):
            self.take()

    def parse_type(self):
        base = self.parse_base()
        self.marks()
        suffixes = []
        while self.peek() in ("*", "@", "[]"):
            suffixes.append(self.take()[0])
            self.marks()
        return {"base": base, "suffixes": suffixes}

    def parse_base(self):
        token = self.peek()
        if token is None:
            return {"name": "", "at": -1}
        if token == "(":
            self.take()
            inner = self.parse_type()
            if self.peek() == ")":
                self.take()
            return {"group": inner}
        if token == "fn" and self.at + 1 < len(self.tokens) and self.tokens[self.at + 1][0] == "(":
            self.take()
            self.take()
            params = []
            while self.peek() not in (")", None):
                params.append(self.parse_type())
                if self.peek() == ",":
                    self.take()
            if self.peek() == ")":
                self.take()
            return {"fn": params, "result": self.parse_type()}
        text, at = self.take()
        if not IDENTIFIER.fullmatch(text):
            return {"name": "", "at": -1}
        while self.peek() == "." and self.at + 1 < len(self.tokens):
            self.take()
            text, at = self.take()
        return {"name": text, "at": at}


def type_start(code, name_at, stops):
    """The offset after the delimiter that starts the type in front of the name at `name_at`.

    A parameter type starts after `(` or `,` of its list, a field type after `{` or `;` of its
    struct. The scan goes back over brackets of the type itself, such as the list of a function
    pointer type.
    """
    depth = 0
    i = name_at - 1
    while i >= 0:
        c = code[i]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth == 0:
                return i + 1
            depth -= 1
        elif c in stops and depth == 0:
            return i + 1
        i -= 1
    return 0


class Rows:
    """The facts of the audit rows of one `fort --index` document: which module is trusted,
    which function is a boundary function (D9.8), and the text of each file.

    `buckets` holds the records of the document by the real path of their file. The memos of
    texts, trust and reach are kept across documents in `memo`, because one file gives the same
    records in each closure of one target.
    """

    def __init__(self, root, buckets, cache, std_roots, memo):
        self.root = root
        self.buckets = buckets
        self.cache = cache
        self.std_roots = std_roots
        self.memo = memo
        self.at_position = {}

    def source(self, path):
        sources = self.memo.setdefault("sources", {})
        if path not in sources:
            try:
                with open(path, encoding="latin-1") as handle:
                    sources[path] = Source(handle.read())
            except OSError:
                sources[path] = None
        return sources[path]

    def trusted(self, path):
        """Whether the module of the file `path` is trusted (D9.8): a file of a standard library
        root, or a file whose first item is `import extern;`."""
        trust = self.memo.setdefault("trust", {})
        if path not in trust:
            inside = any(path.startswith(root + os.sep) for root in self.std_roots)
            source = self.source(path)
            trust[path] = inside or (source is not None and source.is_trusted_form())
        return trust[path]

    def records_at(self, path, line, col):
        """The records of the file `path` whose name starts at line and col."""
        table = self.at_position.get(path)
        if table is None:
            table = {}
            for record in self.buckets.get(path, []):
                table.setdefault((record["line"], record["col"]), []).append(record)
            self.at_position[path] = table
        return table.get((line, col), [])

    def declared(self, path, kind):
        """The offsets of the declaring records of `kind` in the file `path`, in order, with the
        records."""
        table = self.memo.setdefault("declared", {})
        key = (path, kind)
        if key not in table:
            source = self.source(path)
            table[key] = [(source.offset(r["line"], r["col"]), r)
                          for r in self.buckets.get(path, [])
                          if r["kind"] == kind and r.get("is_decl")]
        return table[key]

    def decl_key(self, record):
        """The real path, line and column of the declaration of a record, or None."""
        decl = record.get("decl")
        if not decl or not decl.get("file"):
            return None
        return (real_path(self.root, decl["file"], self.cache), decl["line"], decl["col"])

    def type_at(self, path, name_at, stops):
        """The parsed type in front of the declared name at offset `name_at` of `path`."""
        source = self.source(path)
        start = type_start(source.code, name_at, stops)
        return TypeParser(type_tokens(source.code, start, name_at)).parse_type()

    def struct_names(self, path, ty):
        """The declaration keys of the structs that the parsed type `ty` names, at any depth."""
        source = self.source(path)
        keys = []
        todo = [ty]
        while todo:
            item = todo.pop()
            base = item["base"]
            if "group" in base:
                todo.append(base["group"])
            elif "fn" in base:
                todo.extend(base["fn"])
                todo.append(base["result"])
            elif base["at"] >= 0:
                line = source.line_of(base["at"])
                col = base["at"] - source.starts[line - 1] + 1
                for record in self.records_at(path, line, col):
                    if record["kind"] == "struct":
                        key = self.decl_key(record)
                        if key is not None:
                            keys.append(key)
        return keys

    def struct_facts(self, key):
        """(whether a field type is a byte pointer, the keys of the structs that the fields
        name) of the struct declared at `key`."""
        facts = self.memo.setdefault("structs", {})
        if key in facts:
            return facts[key]
        path, line, col = key
        source = self.source(path)
        direct, names = False, []
        if source is not None:
            name_at = source.offset(line, col)
            open_at = source.code.find("{", name_at)
            close = matching_close(source.code, open_at) if open_at >= 0 else -1
            for at, _ in self.declared(path, "field"):
                if not open_at < at < close:
                    continue
                ty = self.type_at(path, at, "{;")
                direct = direct or type_is_byte_pointer(ty)
                names.extend(self.struct_names(path, ty))
        facts[key] = (direct, names)
        return facts[key]

    def reaches(self, path, ty):
        """Whether the parsed type `ty` of the file `path` reaches a pointer to `char`, `u8` or
        `void` (D9.8): itself, or through a pointee, an element, a field, or a parameter or the
        result of a function pointer type, at any depth."""
        if type_is_byte_pointer(ty):
            return True
        seen = set()
        todo = list(self.struct_names(path, ty))
        while todo:
            key = todo.pop()
            if key in seen:
                continue
            seen.add(key)
            direct, names = self.struct_facts(key)
            if direct:
                return True
            todo.extend(names)
        return False

    def is_boundary(self, key):
        """Whether the fort function declared at `key` is a boundary function (D9.8): a function
        of a trusted module with a parameter whose type reaches a byte pointer."""
        answers = self.memo.setdefault("boundary", {})
        if key in answers:
            return answers[key]
        path, line, col = key
        answer = False
        source = self.source(path)
        if source is not None and self.trusted(path) and any(
                r["kind"] == "fn" and r.get("is_decl") for r in self.records_at(path, line, col)):
            name_at = source.offset(line, col)
            body = source.code.find("{", name_at)
            for at, _ in self.declared(path, "parameter"):
                if name_at < at < body and self.reaches(path, self.type_at(path, at, "(,")):
                    answer = True
                    break
        answers[key] = answer
        return answer


def type_is_byte_pointer(ty):
    """Whether the parsed type is itself a pointer to `char`, `u8` or `void`, or holds one as a
    pointee, an element, or a parameter or the result of a function pointer type, with no
    struct between (D9.8). A base `char`, `u8` or `void` whose first suffix is `*` is one:
    `char*`, `u8 mut*`, `void* own`, `char*@` and `u8**`. `u8@`, `char[4]` and `char[4]*` are
    none, because the pointer, if any, points at a span or an array."""
    todo = [ty]
    while todo:
        item = todo.pop()
        base = item["base"]
        first = item["suffixes"][0] if item["suffixes"] else None
        plain = base
        while "group" in plain and not plain["group"]["suffixes"]:
            plain = plain["group"]["base"]
        if first == "*" and plain.get("name") in BYTE_BASES:
            return True
        if "group" in base:
            todo.append(base["group"])
        elif "fn" in base:
            todo.extend(base["fn"])
            todo.append(base["result"])
    return False


def spelled_callee(code, start):
    """The qualified name that ends with the name token at `start`, with no white space:
    `libc.write` for `write`, also when the source writes `libc . write`."""
    begin = start
    while True:
        dot, dot_at = prev_char(code, begin)
        if dot != ".":
            break
        word = re.search(r"[A-Za-z_][A-Za-z0-9_]*\s*\Z", code[:dot_at])
        if word is None:
            break
        begin = word.start()
    name = IDENTIFIER.match(code, start).group(0)
    return begin, re.sub(r"\s+", "", code[begin:start]) + name


class Callee:
    """The callee of one call, as `call_callee` reads it.

    `text` is the callee as the body spells it, with each group around it and with no white
    space: `libc.write`, `(strlen)`, `table[0]`, `pick()`. A call row names it. `groups` is the
    count of the groups around the whole callee. `name` is the callee inside those groups, with
    no white space: `libc.strlen` for `((libc.strlen))`. A citation names it. `name_at` is the
    offset of the last name of the callee, where the index holds its record, or -1 when the
    callee ends with no name: an element, the result of a call or another expression.
    `begin` and `end` are the offsets of the callee in the code, its groups included.
    """

    def __init__(self, text, groups, name, name_at, begin, end):
        self.text = text
        self.groups = groups
        self.name = name
        self.name_at = name_at
        self.begin = begin
        self.end = end


def callee_span(code, end):
    """The offsets (start, stop) of the place that ends before `end`, white space left out: a
    name with its module path or its members (`h.on`, `z->introspect->enumerator`), an element
    (`table[0]`), a call (`pick()`) or a group (`(k)`). A `(` starts a group when no name, `)`
    or `]` stands before it, or a statement keyword does. The offsets are equal when no place
    ends there. Only `call_callee` calls this function."""
    j = end - 1
    while j >= 0 and code[j].isspace():
        j -= 1
    stop = j + 1
    while j >= 0:
        c = code[j]
        if c in ")]":
            j = matching_open(code, j) - 1
        elif c.isalnum() or c == "_":
            while j >= 0 and (code[j].isalnum() or code[j] == "_"):
                j -= 1
        else:
            break
        k = j
        while k >= 0 and code[k].isspace():
            k -= 1
        if k >= 0 and code[k] == "." and not (k >= 1 and code[k - 1] == "."):
            j = k - 1
        elif k >= 1 and code[k - 1:k + 1] == "->":
            j = k - 2
        elif c in ")]" and k >= 0 and code[k] in ")]":
            j = k
        elif c in ")]" and k >= 0 and (code[k].isalnum() or code[k] == "_"):
            word = re.search(r"[A-Za-z_][A-Za-z0-9_]*\Z", code[:k + 1])
            if word is not None and word.group(0) in STATEMENT_KEYWORDS:
                break
            j = k
        else:
            break
        while j >= 0 and code[j].isspace():
            j -= 1
    start = j + 1
    while start < stop and code[start].isspace():
        start += 1
    return start, stop


def call_callee(code, open_at):
    """The callee of the call whose `(` stands at `open_at`, read through each group around
    it: `strlen`, `(strlen)` and `((libc.strlen))` name one callee. The result is None when the
    `(` opens no argument list: it opens a group, it follows the list of a function type, or
    a keyword or a compiler form stands before it. A keyword is no name (KEYWORDS), so `if (`,
    `return (`, `sizeof(` and `$cfg(` call nothing, also in a group, `(if)(`, and after a path,
    `m.if(`. The result of a `cast` is a callee: `cast(f, T)(x)` calls `cast(f,T)`.

    This function is the one reader of a callee. Each place of the lint that needs the callee
    of a call takes it from here, at the `(` of the call, or through `named_call` from the
    name. So each place reads the same forms (D9.8, "The lint"), and a test holds the places.
    """
    begin, end = callee_span(code, open_at)
    if begin == end:
        return None
    if code[end - 1] == ")":
        word = re.search(r"[A-Za-z_][A-Za-z0-9_]*\s*\Z", code[:matching_open(code, end - 1)])
        if word is not None and word.group(0).strip() == "fn":
            # `fn () (void)`: the list of a function type and a grouped result
            return None
    whole = (begin, end)
    groups = 0
    while code[begin] == "(" and matching_close(code, begin) == end - 1:
        groups += 1
        begin, end = begin + 1, end - 1
        while begin < end and code[begin].isspace():
            begin += 1
        while begin < end and code[end - 1].isspace():
            end -= 1
    name_at = -1
    if begin < end and IDENTIFIER_CHAR.match(code[end - 1]) and (
            groups == 0 or callee_span(code, end)[0] == begin):
        name_at = end - 1
        while name_at > begin and IDENTIFIER_CHAR.match(code[name_at - 1]):
            name_at -= 1
        if code[name_at:end] in KEYWORDS:
            return None
        if name_at > 0 and code[name_at - 1] == "$":
            # `$cfg(key)` and `$if (...)` are compiler forms (D21.1, D21.2) and no call.
            return None
    return Callee(re.sub(r"\s+", "", code[whole[0]:whole[1]]), groups,
                  re.sub(r"\s+", "", code[begin:end]), name_at, whole[0], whole[1])


def named_call(code, start):
    """The callee of the call whose callee is the name at `start`, plain or in each group
    around it, or None when that name is the callee of no call. The name is the last name of
    the callee, after its module path: `strlen` of `(libc.strlen)(p)`. The scan forward only
    finds the `(`; `call_callee` reads the callee there and decides."""
    name = IDENTIFIER.match(code, start)
    if name is None:
        return None
    after, after_at = next_char(code, name.end())
    while after == ")":
        after, after_at = next_char(code, after_at + 1)
    if after != "(":
        return None
    callee = call_callee(code, after_at)
    if callee is None or callee.name_at != start:
        return None
    return callee


def matching_open(code, close_at):
    """The offset of the bracket that opens the one that closes at `close_at`, or 0."""
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for i in range(close_at, -1, -1):
        c = code[i]
        if c in pairs:
            stack.append(pairs[c])
        elif c in "([{":
            if not stack or stack.pop() != c:
                return 0
            if not stack:
                return i
    return 0


def value_calls(rows, path, source, body_open, body_close):
    """The callees of the calls through a function value in `code[body_open:body_close]`: a
    call whose callee is a local, a parameter, a global, a field or a constant of a function
    type, an element (`table[0](...)`), or the result of a call or a group (`pick()(...)`,
    `(k)(...)`). The lint does not tell a function pointer that C gave from one of fort code
    (D9.8), so each such call needs a call row."""
    code = source.code
    found = []
    for at in range(body_open + 1, body_close):
        if code[at] != "(":
            continue
        callee = call_callee(code, at)
        if callee is None:
            continue
        if callee.groups > 0 or callee.name_at < 0:
            found.append(callee.text)
            continue
        line = source.line_of(callee.name_at)
        col = callee.name_at - source.starts[line - 1] + 1
        for record in rows.records_at(path, line, col):
            if record["kind"] in VALUE_KINDS and is_function_type(record.get("type")):
                found.append(callee.text)
                break
    return found


def is_function_type(text):
    """Whether a type that a record spells is a function type, grouped or not."""
    return (text or "").lstrip("(").startswith("fn ")


def next_char(code, offset):
    """The first character at or after `offset` that is no white space, and its offset."""
    i = offset
    while i < len(code) and code[i].isspace():
        i += 1
    return (code[i], i) if i < len(code) else ("", i)


def prev_char(code, offset):
    """The last character before `offset` that is no white space, and its offset."""
    i = offset - 1
    while i >= 0 and code[i].isspace():
        i -= 1
    return (code[i], i) if i >= 0 else ("", -1)


def call_open(code, before):
    """The offset of the `(` of the call whose argument list holds `before`, an offset just
    after a `(` or a `,`, or -1 when no bracket opens it."""
    depth = 0
    i = before - 1
    while i >= 0:
        c = code[i]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            if depth == 0:
                return i if c == "(" else -1
            depth -= 1
        elif c == ";" and depth == 0:
            return -1
        i -= 1
    return -1


def goes_to_c(rows, path, source, start, end):
    """Whether the name at `start`..`end` of `source` stands as the callee of a call, or as a
    whole argument of a call whose callee is an `extern fn` or a function pointer (D9.8). A
    callee in a group is that callee here too: `(keep)(0, size)` gives `size` to `keep`. An
    argument in a group is that argument: `take_cb((takep))` gives what `take_cb(takep)` gives."""
    code = source.code
    if named_call(code, start) is not None:
        return True
    after, after_at = next_char(code, end)
    begin, _ = spelled_callee(code, start)
    before, before_at = prev_char(code, begin)
    while True:
        if before not in "(," or after not in ",)" or not before:
            return False
        open_at = before_at if before == "(" else call_open(code, before_at)
        if open_at < 0:
            return False
        callee = call_callee(code, open_at)
        if callee is not None or before != "(" or after != ")":
            break
        # The `(` opens a group around the name, and no argument list: look around the group.
        after, after_at = next_char(code, after_at + 1)
        before, before_at = prev_char(code, before_at)
    if callee is None:
        return False
    if callee.name_at < 0:
        # An element, the result of a call or another expression: a function pointer.
        return True
    line = source.line_of(callee.name_at)
    col = callee.name_at - source.starts[line - 1] + 1
    for record in rows.records_at(path, line, col):
        kind = record["kind"]
        if kind == "extern fn":
            return True
        if kind in VALUE_KINDS and is_function_type(record.get("type")):
            return True
    return False


def in_import(source, offset):
    """Whether `offset` lies in an import declaration: its statement starts with `import`."""
    start = max(source.code.rfind(c, 0, offset) for c in ";{}") + 1
    return re.match(r"\s*import\s", source.code[start:offset]) is not None


def declaration_row_problems(source, record):
    """The problems of the declaration row of one `extern fn` of a trusted module (D9.8)."""
    name = record["name"]
    at = source.offset(record["line"], record["col"])
    start = keyword_before(source.code, at, "extern")
    line = source.line_of(start if start >= 0 else at)
    run = comment_run(source, line)
    firsts = [i for i, (_, text) in enumerate(run) if DECLARATION_ROW_START.match(text)]
    where = (record["line"], record["col"])
    if not firsts:
        return [where + ("extern '%s' has no declaration row: write '// C: %s, <the source of "
                         "its contract>.' and the five points of A7 above it (%s)"
                         % (name, name, ROWS),)]
    if len(firsts) > 1:
        return [(run[firsts[1]][0], 1, "extern '%s' has %d declaration rows; give it one (%s)"
                 % (name, len(firsts), ROWS))]
    row = run[firsts[0]:]
    problems = []
    first = DECLARATION_ROW.match(row[0][1])
    if first is None or first.group(1) != name:
        problems.append((row[0][0], 1, "the declaration row of extern '%s' names %s and not '%s' "
                         "(%s)" % (name, "'%s'" % first.group(1) if first else "no symbol", name,
                                   ROWS)))
    text = " ".join(item[2:] for _, item in row)
    for word in ROW_WORDS:
        count = len(re.findall(r"(?<![A-Za-z0-9_])%s" % re.escape(word), text, re.IGNORECASE))
        if count != 1:
            problems.append((row[0][0], 1, "the declaration row of extern '%s' gives '%s' %d "
                             "times; give each point of A7 once (%s)"
                             % (name, word, count, ROWS)))
    return problems


def call_rows(run):
    """The call rows of a comment run, as (line, callee, count) triples. A row runs from its
    `// C call` line to the next one or to the end of the run. A row that does not parse has the
    callee None."""
    rows = []
    for line, text in run:
        if not CALL_ROW_START.match(text):
            continue
        found = CALL_ROW.match(text)
        if found is None:
            rows.append((line, None, 0))
            continue
        rows.append((line, found.group(1), int(found.group(2) or "1")))
    return rows


def text_calls(code, callee, start, end):
    """The count of the calls in `code[start:end]` whose callee, as the body spells it, is
    `callee`: each `(` there that `call_callee` reads as that callee. So `x.libc.write(` and
    `p->write(` are no calls of `write`, and `pick (k)(x)` is no call of `(k)`. This count only
    places the rows: the count of the index is the authority (function_row_problems)."""
    count = 0
    for at in range(start, end):
        if code[at] == "(":
            found = call_callee(code, at)
            if found is not None and found.text == callee:
                count += 1
    return count


def function_row_problems(rows, path, source, record, calls, values=()):
    """The problems of the call rows of one function of a trusted module (D9.8).

    `calls` maps the callee of each call of an `extern fn`, of a boundary function and through
    a function value in the body to its count on this target: the calls that the index records,
    and the calls through a value that the brackets of the body give. That count is the
    authority, so a callee whose text the lint counts lower is an error. The rows of a callee
    count its calls in the text of the body, so a call in a `$if` branch of another target
    counts too. `values` holds the callees that are function values.
    """
    name = record["name"]
    at = source.offset(record["line"], record["col"])
    start = keyword_before(source.code, at, "fn")
    run = comment_run(source, source.line_of(start if start >= 0 else at))
    body_open = source.code.find("{", at)
    body_close = matching_close(source.code, body_open) if body_open >= 0 else -1
    counted = {}
    first_line = {}
    problems = []
    for line, callee, count in call_rows(run):
        if callee is None or count < 1:
            problems.append((line, 1, "a call row of fn '%s' does not read as '// C call "
                             "<the callee>: ...' or '// C call <the callee> x<N>: ...' with N "
                             "above 0 (%s)" % (name, ROWS)))
            continue
        counted[callee] = counted.get(callee, 0) + count
        first_line.setdefault(callee, line)
    for callee in sorted(set(calls) | set(counted)):
        held = text_calls(source.code, callee, body_open, body_close + 1)
        rowed = counted.get(callee, 0)
        index = calls.get(callee, 0)
        if index > held:
            # A form of the text that the lint does not read: the count of the index holds.
            source_of_count = ("the calls through a value that the brackets of the body give"
                               if callee in values else "the index of the compiler")
            problems.append((record["line"], record["col"], "fn '%s' has %d call(s) of '%s' by "
                             "%s, and the lint finds %d in its text: write the callee in a "
                             "plain form, so that the rows count each call (%s)"
                             % (name, index, callee, source_of_count, held, ROWS)))
        elif rowed == 0 and callee in values:
            problems.append((record["line"], record["col"], "fn '%s' has %d call(s) through the "
                             "function value '%s' and no call row for it: write '// C call %s: "
                             "...' above it, with what the value is (%s)"
                             % (name, held, callee, callee, ROWS)))
        elif rowed == 0:
            problems.append((record["line"], record["col"], "fn '%s' has %d call(s) of '%s' and "
                             "no call row for it: write '// C call %s: ...' above it (%s)"
                             % (name, held, callee, callee, ROWS)))
        elif rowed != held:
            problems.append((first_line[callee], 1, "the call rows of fn '%s' count %d call(s) "
                             "of '%s', and its body holds %d (%s)"
                             % (name, rowed, callee, held, ROWS)))
    return problems


# A citation in a call row of the word of the row of another trusted function (D9.8 rule 3):
# `the gives: of rt.alloc` or `the writes: of rt.decimal_text`. A sentence can start with `The`.
# One space stands between the words, and `the` and its word stand on one line. A line wrap can
# take the place of the space after the colon and of the space after `of`. The callee follows.
CITATION = re.compile(r"[Tt]he (gives|writes):[ \n]of[ \n]")
# Text that starts a citation: `the gives:`, `the gives of` and `gives: of`, and the same with
# `writes`, in each case. White space and quote characters can stand between the words, as in
# "the `gives:` of". Each such text reads as a citation or it is a problem, so no text that a
# reviewer takes for a citation passes with no check. A `_` of the pattern is one such character.
CITATION_GAP = "[\\s`'\"]"
CITATION_START = re.compile(
    r"(?i)\bthe_+(?:gives|writes)_*:|\bthe_+(?:gives|writes)_+of\b|\b(?:gives|writes)_*:_*of\b"
    .replace("_", CITATION_GAP))
# The callee of a citation with no group: a name with its module path.
CITED_NAME = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*")
# The start of a number. A number is no name, so `(0)` cites no callee, as `0` cites none.
CITED_NUMBER = re.compile(r"[0-9]")
# The text where the callee of a citation stands, for a message: up to the next white space.
CITED_TEXT = re.compile(r"\S*")


def cited_callee(text, at):
    """The callee that a citation names at `at` of the text of a row, with no group and no white
    space, or None when no callee stands there. A citation names the callee as the body spells
    it, so `call_callee` reads it as it reads the callee of a call: `rt.alloc`, `(rt.alloc)` and
    `(( rt . alloc ))` name `rt.alloc`. A keyword, a number and a group that holds no one name
    are no callee."""
    if text[at:at + 1] == "(":
        close = matching_close(text, at)
        if close < 0:
            return None
        spelled = text[at:close + 1]
    else:
        found = CITED_NAME.match(text, at)
        if found is None:
            return None
        spelled = found.group(0)
    callee = call_callee(spelled + "(", len(spelled))
    if callee is None or callee.name_at < 0:
        return None
    if CITED_NUMBER.match(callee.name) is not None:
        return None
    return callee.name


def unread_citation(text, start, stop, reads):
    """The words of the message for text that starts a citation and that the lint cannot read:
    the text from `start` to `stop`. When the text reads to the end of `the gives: of`
    (`reads`), the words hold the text where the callee stands too, up to the next white space.
    A line end in the text is a space in the message, and the message says that a line end is
    there."""
    if reads:
        stop = CITED_TEXT.match(text, stop).end()
    found = text[start:stop].strip()
    words = "'%s'" % " ".join(found.split())
    return words + " with a line end" if "\n" in found else words


def cut_callee(text, at, callee, called):
    """The callee that the body calls and that a line end cuts in a citation, or None. The
    citation names `callee` at `at` of the text of a row, as `maker` of `the gives: of maker.`
    with `good` at the start of the next line: a citation holds its callee on one line, so the
    reader takes `maker`. `called` holds each callee that the body calls."""
    end = at + len(callee)
    if text[at:end] != callee or text[end:end + 2] != ".\n":
        return None
    rest = CITED_NAME.match(text, end + 2)
    if rest is None:
        return None
    whole = callee + "." + rest.group(0)
    return whole if whole in called else None


def citation_problems(rows, source, record, called, externs):
    """The problems of the citations in the call rows of one function of a trusted module: each
    cited callee is called in the body (`called` maps the callee as the body spells it to the key
    of its declaration), it is a fort function of a trusted module, and the row of that function,
    its `//` lines directly above it, holds a line that starts with the cited word (D9.8 rule 3).
    `externs` holds each `extern fn` that the body calls, as the body spells it: the citation
    form names no extern, and the message says so. Text that starts a citation and does not
    read as one is a problem: the lint reads each citation or reports it."""
    at = source.offset(record["line"], record["col"])
    start = keyword_before(source.code, at, "fn")
    run = comment_run(source, source.line_of(start if start >= 0 else at))
    problems = []
    # A citation can wrap, so the lines join with a line end, and each keeps its first offset.
    joined = ""
    starts = []
    for line, text in run:
        starts.append((len(joined), line))
        joined += text[2:].strip() + "\n"
    for start in CITATION_START.finditer(joined):
        line = [number for offset, number in starts if offset <= start.start()][-1]
        found = CITATION.match(joined, start.start())
        callee = cited_callee(joined, found.end()) if found is not None else None
        if callee is None:
            reads = found is not None
            stop = found.end() if reads else start.end()
            problems.append((line, 1, "a call row of fn '%s' starts a citation that the lint "
                             "cannot read, %s: write 'the gives: of <the callee>' or 'the "
                             "writes: of <the callee>', with the callee a name as the body "
                             "spells it (rule 3) (%s)"
                             % (record["name"],
                                unread_citation(joined, start.start(), stop, reads), ROWS)))
            continue
        word = found.group(1) + ":"
        if callee in externs:
            problems.append((line, 1, "a call row of fn '%s' cites the %s of '%s', which is an "
                             "`extern fn`: a citation names a fort function of a trusted module "
                             "(rule 3) (%s)" % (record["name"], word, callee, ROWS)))
            continue
        key = called.get(callee)
        if key is None:
            whole = cut_callee(joined, found.end(), callee, called)
            cut = "" if whole is None else (": a line end stands inside the callee '%s', and a "
                                            "citation holds its callee on one line" % whole)
            problems.append((line, 1, "a call row of fn '%s' cites the %s of '%s', which its "
                             "body does not call%s (%s)"
                             % (record["name"], word, callee, cut, ROWS)))
            continue
        if not rows.trusted(key[0]):
            problems.append((line, 1, "a call row of fn '%s' cites the %s of '%s', a function "
                             "of a module that is not trusted: no audit holds its row (rule 3) "
                             "(%s)" % (record["name"], word, callee, ROWS)))
            continue
        if not cited_word_stands(rows, key, word):
            problems.append((line, 1, "a call row of fn '%s' cites the %s of '%s', and the row "
                             "of '%s' has no line '// %s ...' (%s)"
                             % (record["name"], word, callee, callee, word, ROWS)))
    return problems


def cited_word_stands(rows, key, word):
    """Whether the row of the fort function declared at `key`, its `//` lines directly above it,
    holds a line that starts with `word`."""
    path, line, col = key
    source = rows.source(path)
    if source is None:
        return False
    at = source.offset(line, col)
    start = keyword_before(source.code, at, "fn")
    run = comment_run(source, source.line_of(start if start >= 0 else at))
    return any(text.startswith("// " + word) for _, text in run)


def enclosing_opens(code, start, stop):
    """The offsets of the `(` of each call whose argument list holds the offset `start`, inner
    first, up to the statement and not before `stop`. An open `[` of an index and an open `{` of
    a struct or array literal do not end the search: the offset is still inside the call that
    holds them. The `{` of a block does not end it either, and each `(` before a block is closed,
    so a block adds no call."""
    opens = []
    depth = 0
    i = start - 1
    while i > stop:
        c = code[i]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            if depth > 0:
                depth -= 1
            elif c == "(":
                opens.append(i)
        elif c == ";" and depth == 0:
            # The stop bounds the work to one statement. It changes no verdict.
            break
        i -= 1
    return opens


def mut_global_to_c(rows, path, source, use, stop):
    """Whether the use record `use` of a global names a `mut` global whose type reaches a byte
    pointer, anywhere between the brackets of a call of an `extern fn` or of a boundary
    function."""
    # The index names a `mut` global `global` and a module constant `constant`, so each record of
    # kind `global` is a `mut` global.
    key = rows.decl_key(use)
    if key is None:
        return False
    decl_path, line, col = key
    decl_source = rows.source(decl_path)
    if decl_source is None:
        return False
    if not rows.reaches(decl_path, rows.type_at(decl_path, decl_source.offset(line, col), ";{}")):
        return False
    code = source.code
    for open_at in enclosing_opens(code, source.offset(use["line"], use["col"]), stop):
        # A callee in a group, `(strlen)(name)` or `((libc.memcmp))(...)`, is that callee.
        callee = call_callee(code, open_at)
        if callee is None:
            continue
        # A callee that is no name can hold a boundary function as a value, as
        # `(first ? take : other)(...)`, and the call can reach it: each name in the callee
        # counts. The callee of a call inside the callee does not: the call is through its
        # result, a function pointer, which the lint does not tell from one of fort code (D9.8).
        for name in IDENTIFIER.finditer(code, callee.begin, callee.end):
            if name.start() != callee.name_at and named_call(code, name.start()) is not None:
                continue
            at_line = source.line_of(name.start())
            at_col = name.start() - source.starts[at_line - 1] + 1
            for record in rows.records_at(path, at_line, at_col):
                if record["kind"] == "extern fn":
                    return True
                target = rows.decl_key(record) if record["kind"] == "fn" else None
                if target is not None and rows.is_boundary(target):
                    return True
    return False


def row_problems(rows, path, records):
    """The problems of D9.8 that the records of the file `path` show, as (line, col, message).

    In a trusted module: each `extern fn` has its declaration row, and the call rows of each
    function count each call of an `extern fn`, of a boundary function and through a function
    value in its body. In each
    module: a name of a boundary function stands only as the callee of a call or as an argument
    of a call whose callee is an `extern fn` or a function pointer.
    """
    source = rows.source(path)
    if source is None:
        return []
    trusted = rows.trusted(path)
    problems = []
    functions = []
    uses = []
    globals_used = []
    fn_calls = []
    extern_calls = []
    for record in records:
        if record.get("is_decl"):
            if rows.decl_key(record) != (path, record["line"], record["col"]):
                # An `as` alias of an import declares a name here and binds a declaration
                # elsewhere (D20.3): it is no function and no extern of this module.
                continue
            if trusted and record["kind"] == "extern fn":
                problems.extend(declaration_row_problems(source, record))
            elif trusted and record["kind"] == "fn":
                at = source.offset(record["line"], record["col"])
                body_open = source.code.find("{", at)
                body_close = matching_close(source.code, body_open) if body_open >= 0 else -1
                functions.append((body_open, body_close, record, {}))
            continue
        if record["kind"] == "global":
            globals_used.append(record)
        if record["kind"] == "extern fn":
            uses.append((record, False))
            start = source.offset(record["line"], record["col"])
            callee = named_call(source.code, start)
            if callee is not None:
                extern_calls.append((start, callee.name))
        elif record["kind"] == "fn":
            key = rows.decl_key(record)
            start = source.offset(record["line"], record["col"])
            callee = named_call(source.code, start)
            if key is not None and callee is not None:
                fn_calls.append((start, callee.name, key))
            if key is not None and rows.is_boundary(key):
                uses.append((record, True))
    for record, boundary in uses:
        start = source.offset(record["line"], record["col"])
        end = start + len(record["name"])
        if boundary and not in_import(source, start) and not goes_to_c(rows, path, source,
                                                                       start, end):
            problems.append((record["line"], record["col"], "boundary function '%s' goes here as "
                             "a value, and not to C: name it only as the callee of a call or as "
                             "an argument of a call of an extern fn or a function pointer (%s)"
                             % (spelled_callee(source.code, start)[1], ROWS)))
            continue
        callee = named_call(source.code, start)
        if callee is None or callee.groups > 0:
            # A callee in a group counts below, as the function value that the brackets give.
            continue
        for body_open, body_close, _, calls in functions:
            if body_open < start < body_close:
                calls[callee.text] = calls.get(callee.text, 0) + 1
                break
    for body_open, body_close, record, calls in functions:
        values = set()
        if body_open >= 0 and body_close > body_open:
            for callee in value_calls(rows, path, source, body_open, body_close):
                calls[callee] = calls.get(callee, 0) + 1
                values.add(callee)
        problems.extend(function_row_problems(rows, path, source, record, calls, values))
        called = {callee: key for at, callee, key in fn_calls if body_open < at < body_close}
        externs = {callee for at, callee in extern_calls if body_open < at < body_close}
        problems.extend(citation_problems(rows, source, record, called, externs))
        if not rows.is_boundary((path, record["line"], record["col"])):
            for use in globals_used:
                at = source.offset(use["line"], use["col"])
                if body_open < at < body_close and mut_global_to_c(rows, path, source, use,
                                                                    body_open):
                    problems.append((use["line"], use["col"], "fn '%s' gives the `mut` global "
                                     "'%s', whose type reaches a byte pointer, to a call of C: "
                                     "a value from it is a parameter source (rule 1), which "
                                     "stands only in a boundary function (%s)"
                                     % (record["name"], use["name"], ROWS)))
    return problems


def std_module(relative):
    """The module path of a file of `std/` in the standard root of its target, or None.

    `std/str.ft` gives `std.str`, `std/linux/os.ft` gives `std.os` and `std/digest/sha256.ft`
    gives `std.digest.sha256`.
    """
    parts = Path(relative).parts
    if len(parts) < 2 or parts[0] != "std" or not parts[-1].endswith(".ft"):
        return None
    rest = list(parts[1:])
    if len(rest) > 1 and rest[0] in TARGETS:
        rest = rest[1:]
    rest[-1] = rest[-1][: -len(".ft")]
    return ".".join(["std"] + rest)


def std_target(relative, target):
    """The target whose standard root holds the file `relative` of `std/`: the target of its
    directory, or `target` for a module that both targets share."""
    parts = Path(relative).parts
    if len(parts) > 2 and parts[1] in TARGETS:
        return parts[1]
    return target


def link_std_root(root, target, out):
    """Make `out` the standard root of `target`, as symbolic links to the files of `std/`.

    assemble_std.sh copies each `.ft` of `std/` outside the target directories, and each `.ft` of
    `std/<target>/`, to its path below the root. A link keeps the real path of its file, so the
    real path of a record is the file of `std/`, while the links exist. No path of a module of a
    target directory is the path of a module that both targets share, so no link takes the place
    of another; a second link to one path stops the run with FileExistsError.
    """
    std = (Path(root) / "std").resolve()
    for source in sorted(std.rglob("*.ft")):
        parts = source.relative_to(std).parts
        if len(parts) > 1 and parts[0] in TARGETS:
            if parts[0] != target:
                continue
            parts = parts[1:]
        link = out.joinpath(*parts)
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(source)


def std_documents(fort, root, entries, cache, target):
    """Run `fort --index` over the modules of `std/` among `entries`, as `std.<name>`.

    D6.9 names two functions of `std` by their modules, `str.from_cstr` and `rt.args_init`, so
    a module of `std/` checks as `std.<name>` and not as an entry module named by its file. A
    module of `std/<target>/` checks under the standard root of its target, and a shared module
    under the root of `target`. Each run uses a standard root of symbolic links to each file of
    `std/` that the root of its target holds, so a closure finds each module that it imports.
    One entry module imports each module that the run judges. Returns a list of
    (indexes, records by real path, diagnostics, error), one item for each run.
    """
    groups = {}
    for index, entry in enumerate(entries):
        relative = entry[0]
        module = std_module(relative)
        if module is not None:
            groups.setdefault(std_target(relative, target), []).append((index, module))
    runs = []
    for run_target in sorted(groups):
        members = groups[run_target]
        with tempfile.TemporaryDirectory() as tmp:
            std_root = Path(tmp) / "std"
            link_std_root(root, run_target, std_root)
            entry = Path(tmp) / "lint_std.ft"
            lines = ["import %s;" % module for module in sorted({module for _, module in members})]
            lines.append("fn main() i32 {\n    return 0;\n}")
            entry.write_text("\n".join(lines) + "\n", encoding="utf-8")
            document, error = index_document(fort, root, str(entry), (), std_root)
            indexes = [index for index, _ in members]
            if error is not None:
                runs.append((indexes, {}, [], error))
                continue
            runs.append((indexes, group_records(document, root, cache),
                         diagnostic_problems(document), None))
    return runs


def standard_roots(fort, root, std_dir):
    """The real paths of the standard library directories that a run can load a module from:
    `std/` of the tree, `--std-dir`, $FORT_STD_DIR, and `std` beside the compiler (D9.2). A
    module of one of them is trusted (D9.8)."""
    found = [os.path.join(str(root), "std"), os.path.join(os.path.dirname(str(fort)), "std")]
    for given in (std_dir, os.environ.get("FORT_STD_DIR")):
        if given:
            found.append(os.path.join(str(root), str(given)))
    return tuple(sorted({os.path.realpath(path) for path in found}))


def lint_files(fort, root, files, std_dir=None, target="linux"):
    """Return the formatted problems of each file, and the number of runs.

    A file of `std/` checks as a module of the standard root of its target (std_documents);
    `target` gives the root of a module that both targets share.

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
    memo = {}
    std_roots = standard_roots(fort, root, std_dir)
    entries = []
    for path, includes in files:
        relative = os.path.relpath(str(path), str(root))
        text, no_utf8 = source_text(Path(path).read_bytes())
        # A run judges a file only when its roots and its options agree, so the pair is its key.
        given = (tuple(includes), lang_options(root, relative))
        entries.append((relative, given, real_path(root, relative, cache), text, no_utf8))
    reports = [None] * len(entries)
    runs = 0
    for indexes, buckets, diagnostics, error in std_documents(fort, root, entries, cache, target):
        runs += 1
        for index in indexes:
            relative, _, path, text, _ = entries[index]
            if error is not None:
                reports[index] = [(1, 1, error)]
                continue
            records = buckets.get(path, [])
            reports[index] = file_problems(records, diagnostics, has_source_text(text),
                                           module_segments(relative), not rows_only(relative))
            rows = Rows(root, buckets, cache, std_roots, memo)
            reports[index].extend(row_problems(rows, path, records))
    for index, (relative, given, _, _, _) in enumerate(entries):
        if reports[index] is not None:
            continue
        includes, options = given
        document, error = index_document(fort, root, relative, includes, std_dir, options)
        runs += 1
        if error is not None:
            reports[index] = [(1, 1, error)]
            continue
        buckets = group_records(document, root, cache)
        diagnostics = diagnostic_problems(document)
        rows = Rows(root, buckets, cache, std_roots, memo)
        for other in range(index, len(entries)):
            if reports[other] is not None:
                continue
            records = buckets.get(entries[other][2])
            if other != index and (not records or entries[other][1] != given):
                # The whole design rests on one fact: a file's records depend on
                # that file's own import closure and on nothing wider. So the
                # records of a file in this document are the records its own run
                # would give. The search roots and the `--cfg` options of a language
                # test are the things that can change that closure. Thus, a run judges a
                # file only when the roots and the options agree. Std/ resolves through
                # the copy beside the compiler, which is another file, and the test/fort
                # and test/lsp tests carry the roots of
                # test/fort/support. A module of src/fort carries `-I src`, so a run of a
                # module of src/lsp, which carries the same root, can judge it.
                continue
            source = has_source_text(entries[other][3])
            segments = module_segments(entries[other][0])
            reports[other] = file_problems(records or [], diagnostics, source, segments,
                                           not rows_only(entries[other][0]))
            reports[other].extend(row_problems(rows, entries[other][2], records or []))
    lines = []
    for index, (relative, _, _, text, no_utf8) in enumerate(entries):
        problems = [] if rows_only(relative) else text_problems(relative, text, root)
        if no_utf8 is not None:
            # Each file of the run gets this check, a file of ROWS_ONLY too.
            problems.append(no_utf8)
        problems += reports[index]
        problems.sort(key=lambda problem: (problem[0], problem[1]))
        lines.append(
            ["%s:%d:%d: %s" % (relative, line, col, message) for line, col, message in problems]
        )
    return lines, runs


def empty_set_problems(root, paths, globs=SOURCE_GLOBS):
    """The reasons the default file set is not the one this tool means to check.

    A mistyped glob, renamed directory, or empty `std/` could make the lint
    inspect nothing. It would print "0 file(s), no violation" and exit 0.
    Therefore, the complete set must not be empty. A glob with an existing
    directory must match a file. A glob whose directory does not yet exist is
    valid.
    """
    problems = []
    if not paths:
        problems.append("fort_lint: no fort source matched %s" % ", ".join(globs))
        return problems
    for pattern in globs:
        directory = root / glob_base(pattern)
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
    parser.add_argument(
        "--target",
        choices=TARGETS,
        default="linux",
        help="the target whose std/<target>/**/*.ft the default set holds (default: %(default)s)",
    )
    parser.add_argument("paths", nargs="*", type=Path, help="the files to check")
    args = parser.parse_args(argv)
    # One spelling of the root and of each path: a temporary directory on
    # macOS is a symbolic link (/var -> /private/var), and a relative path
    # between the two spellings climbs to the file system root.
    args.root = args.root.resolve()

    if args.paths:
        files = [(path.resolve(), tuple(args.include)) for path in args.paths]
        empty = []
    else:
        sets = source_sets(args.target)
        files = default_file_set(args.root, sets)
        for index, (path, includes) in enumerate(files):
            files[index] = (path, tuple(args.include) + includes)
        globs = tuple(glob for glob, _ in sets)
        empty = empty_set_problems(args.root, [path for path, _ in files], globs)
    if empty:
        for problem in empty:
            print(problem, file=sys.stderr)
        return 1

    problems = []
    files_with_problems = 0
    reports, _ = lint_files(args.fort, args.root, files, args.std_dir, args.target)
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
    print("fort_lint: %d file(s), no violation of D1.4 or D9.8" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
