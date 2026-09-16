#!/usr/bin/env python3
"""Run the fort language tests (spec/toolchain.md 7, decisions D14.4 and D14.5).

Every test is a `.ft` file whose expected behavior is encoded in `//!`
directives; a directory containing `main.ft` is one multi-file test. Each test
is compiled with `fort` from `test/lang` as the working directory, run in a
fresh temporary directory, and judged against its directives. `xfail.txt`
lists the tests the compiler cannot pass yet (a listed test that passes is an
XPASS and fails the run); `bootstrap-unsupported.txt` lists the tests that use
features the C bootstrap deliberately lacks, which must be rejected with the
bootstrap diagnostic. `--lint` validates the directives without a compiler and
`--verify-ir` runs the LLVM verifier over the module of every test that
compiles. `--check-json` is a mode of its own: it runs `fort --check --json`
over every fail test and holds the document of D20.2 against the text form of
D14.2 instead of judging the test; a test with an `index.json` beside it is
selected too, and its golden identifier index is held against the `"symbols"`
of a `--index` run (D20.3).

Standard library only; Python 3.12.
"""

import argparse
import concurrent.futures
import dataclasses
import json
import os
import re
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# The areas of D14.4, in the order the decision lists them.
AREAS = (
    "lexical constants operators casts mutability ownership declarations control switch "
    "defer functions structs enums arrays spans strings pointers globals builtins errors "
    "modes modules ffi stdlib"
).split()

KINDS = ("run", "fail")
RUN_ONLY = ("args", "link", "stdin", "stdout", "exit", "abort", "signal")
FAIL_ONLY = ("error", "error-any")
# Directives that take no text, the ones that take text, and the ones that
# may be repeated (D14.5).
BLOCKS = ("stdin", "stdout")
WITH_TEXT = ("flags", "args", "link", "exit", "signal", "stderr", "error", "error-any")
REPEATABLE = ("link", "stderr", "error-any")
HEADER_MARKERS = ("//!", "//<", "//|")

XFAIL_NAME = "xfail.txt"
UNSUPPORTED_NAME = "bootstrap-unsupported.txt"
UNSUPPORTED_MESSAGE = "not supported by the bootstrap compiler"
# `--cc` must be a clang and its target is named on the command line (D14.1).
DEFAULT_CC = "clang"
DEFAULT_TARGET = "x86_64-linux-gnu"
DEFAULT_OPT = "opt-18"
DEFAULT_QEMU_LD_PREFIX = "/usr/x86_64-linux-gnu"
DEFAULT_TIMEOUT = 60.0
# qemu-user reports a fatal signal on the program's stderr ("qemu: uncaught
# target signal 6 (Abort) - core dumped"); native execution prints nothing.
QEMU_NOTICE_PREFIX = b"qemu: uncaught target signal"
MAX_EXIT = 255
# The signals `//! signal:` may name, by their POSIX name without the `SIG`
# prefix; `//! abort` is the older spelling of `signal: ABRT`. A number is not
# accepted, since the set is normative (toolchain.md 7.3).
SIGNALS = {
    "ABRT": signal.SIGABRT,
    "BUS": signal.SIGBUS,
    "FPE": signal.SIGFPE,
    "ILL": signal.SIGILL,
    "SEGV": signal.SIGSEGV,
    "TRAP": signal.SIGTRAP,
}
VERDICTS = ("PASS", "FAIL", "XFAIL", "XPASS", "ERROR")

# The document of `fort --check --json` (D20.2): its keys, the keys of a
# diagnostic and of a note, and the version this harness reads.
DOCUMENT_KEYS = ("version", "files", "diagnostics", "symbols")
DIAGNOSTIC_KEYS = ("file", "line", "col", "end_line", "end_col", "severity", "message", "notes")
NOTE_KEYS = ("file", "line", "col", "end_line", "end_col", "message")
# One record of the identifier index and the range nested in its "decl" (D20.3).
SYMBOL_KEYS = (
    "file",
    "line",
    "col",
    "end_line",
    "end_col",
    "name",
    "kind",
    "type",
    "is_decl",
    "decl",
)
RANGE_KEYS = ("file", "line", "col", "end_line", "end_col")
# The kinds of D20.3, spelled as a diagnostic spells them.
SYMBOL_KINDS = (
    "module",
    "fn",
    "extern fn",
    "struct",
    "enum",
    "enum member",
    "field",
    "constant",
    "global",
    "local",
    "parameter",
    "builtin",
)
DOCUMENT_VERSION = 1
# The golden identifier index of a test: the "symbols" of its `--index` run,
# one record per line, beside the test it is about (D20.3).
INDEX_NAME = "index.json"

DIRECTIVE_RE = re.compile(r"^//! ([a-z][a-z-]*)(:(.*))?$")
ANNOTATION_RE = re.compile(r"^(.*?\S)\s*//! error:(.*)$")
DIAGNOSTIC_RE = re.compile(r"^(.+):(\d+):(\d+): error: (.*)$")
REPORT_RE = re.compile(r"^(.+):(\d+):(\d+): (error|note): (.*)$")

# The lexer reports a position rather than a range, since the bytes it
# rejected are not a token (D14.2, D20.4), so its diagnostics are the ones
# whose range may be empty. These are the messages of `fail` and `fail_text`
# in src/bootstrap/lexer.c, by the part of each that never varies; a lexer
# message that is not here fails the range check of a fail test, which is the
# reminder to add it or to give the diagnostic a range.
LEXICAL_MESSAGES = (
    "block comments are not supported",
    "'_' must stand between two digits",
    "is a reserved word",
    "decimal literal may not start with '0'",
    "float literals have no suffix",
    "is not a float literal",
    "integer literal too large",
    "integer literals have no suffix",
    "invalid digit ",
    "literal needs at least one digit",
    "unknown escape ",
    "'\\' followed by byte ",
    "'\\x' needs exactly two hex digits",
    "unterminated char literal",
    "char literal holds exactly one character",
    "non-ASCII byte in char literal",
    "control character in char literal",
    "unterminated string literal",
    "non-ASCII byte outside a string literal or comment",
    "unexpected character ",
    "unexpected byte ",
)
NUMBERED_RE = re.compile(r"^(\d{3})_([a-z0-9_]+)$")
NAME_RE = re.compile(r"^[a-z0-9_]+$")


# ---- data ---------------------------------------------------------------------


@dataclasses.dataclass
class Test:
    """One language test: a single `.ft` file or a directory with `main.ft`.

    Paths are relative to the corpus root (test/lang) with forward slashes;
    `path` names the directory for a multi-file test and `entry` the file
    handed to the compiler (D14.4).
    """

    path: str
    entry: str
    expected_kind: str  # the kind the directory implies: run under run/ and programs/
    kind: str = ""  # the kind of the first line
    flags: list = dataclasses.field(default_factory=list)
    args: list = dataclasses.field(default_factory=list)
    links: list = dataclasses.field(default_factory=list)
    stdin: bytes = b""
    stdout: bytes = b""
    exit: int = 0
    abort: bool = False
    signal_name: str = ""  # a key of SIGNALS, or "" when the program must not die
    stderr: list = dataclasses.field(default_factory=list)
    errors: list = dataclasses.field(default_factory=list)  # (file, line, substring)
    error_any: list = dataclasses.field(default_factory=list)
    problems: list = dataclasses.field(default_factory=list)
    golden_index: bool = False  # an index.json beside it: the golden of D20.3

    @property
    def multi(self):
        return not self.path.endswith(".ft")


@dataclasses.dataclass
class Proc:
    """The outcome of one child process."""

    argv: list
    returncode: int  # negative for a signal, as subprocess reports it
    stdout: bytes = b""
    stderr: bytes = b""
    timed_out: bool = False
    started: bool = True  # False when the executable could not be launched


@dataclasses.dataclass
class Diagnostic:
    """One `error:` or `note:` line of D14.2, or one record of a document."""

    file: str
    line: int
    column: int
    message: str
    severity: str = "error"

    def text(self):
        """The line D14.2 prints for it."""
        return "%s:%d:%d: %s: %s" % (self.file, self.line, self.column, self.severity, self.message)


@dataclasses.dataclass
class Result:
    test: Test
    verdict: str
    reason: str = ""
    procs: list = dataclasses.field(default_factory=list)
    workdir: str = ""

    def line(self):
        suffix = ": " + self.reason if self.reason else ""
        return "%s %s%s" % (self.verdict, self.test.path, suffix)


# ---- directives (D14.5) -------------------------------------------------------------


def parse_main(root, test, text):
    """Fill `test` from the directives of its entry file and validate them.

    The header is the leading run of `//!`, `//<` and `//|` lines; the rest of
    the file is scanned for `//! error:` annotations. Every problem is
    appended to `test.problems` as `<file>:<line>: <message>`.
    """
    lines = text.split("\n")
    header_end = 0
    while header_end < len(lines) and lines[header_end][:3] in HEADER_MARKERS:
        header_end += 1

    def problem(lineno, message):
        test.problems.append("%s:%d: %s" % (test.entry, lineno, message))

    if header_end == 0 or lines[0] not in ("//! run", "//! fail"):
        problem(1, "first line must be '//! run' or '//! fail'")
        return
    test.kind = lines[0][4:]
    if test.kind != test.expected_kind:
        problem(1, "'%s' test in a %s directory" % (test.kind, test.expected_kind))
    seen = set()
    block = None  # the stdin: or stdout: directive the //< or //| lines belong to
    stdin_lines = []
    stdout_lines = []
    for lineno in range(2, header_end + 1):
        line = lines[lineno - 1]
        marker = line[:3]
        if marker in ("//<", "//|"):
            if len(line) > 3 and line[3] != " ":
                problem(lineno, "'%s' must be followed by a space" % marker)
            elif marker == "//<" and block == "stdin":
                stdin_lines.append(line[4:])
            elif marker == "//|" and block == "stdout":
                stdout_lines.append(line[4:])
            else:
                problem(
                    lineno,
                    "'%s' line outside a '%s:' block"
                    % (marker, "stdin" if marker == "//<" else "stdout"),
                )
            continue
        block = None
        match = DIRECTIVE_RE.match(line)
        if not match:
            problem(lineno, "malformed directive '%s'" % line)
            continue
        name, has_colon, value = (
            match.group(1),
            match.group(2) is not None,
            (match.group(3) or "").strip(),
        )
        if name in KINDS:
            problem(lineno, "'%s' is only allowed on the first line" % name)
        elif name == "error":
            problem(lineno, "'error' annotates a code line and cannot appear in the header")
        elif name in BLOCKS:
            if not has_colon or value:
                problem(lineno, "'%s:' takes no text on its line" % name)
            elif name in seen:
                problem(lineno, "duplicate '%s:' directive" % name)
            elif test.kind != "run":
                problem(lineno, "'%s' is only allowed in run tests" % name)
            else:
                seen.add(name)
                block = name
        elif name == "abort":
            if has_colon or value:
                problem(lineno, "'abort' takes no text")
            elif name in seen:
                problem(lineno, "duplicate 'abort' directive")
            elif test.kind != "run":
                problem(lineno, "'abort' is only allowed in run tests")
            else:
                seen.add(name)
                test.abort = True
        elif name in WITH_TEXT:
            if not has_colon or not value:
                problem(lineno, "'%s:' needs text" % name)
            elif name not in REPEATABLE and name in seen:
                problem(lineno, "duplicate '%s:' directive" % name)
            elif name in RUN_ONLY and test.kind != "run":
                problem(lineno, "'%s' is only allowed in run tests" % name)
            elif name in FAIL_ONLY and test.kind != "fail":
                problem(lineno, "'%s' is only allowed in fail tests" % name)
            else:
                seen.add(name)
                _apply_text_directive(root, test, name, value, problem, lineno)
        else:
            problem(lineno, "unknown directive '%s'" % name)
    # A run test states one outcome: a status, SIGABRT, or a named signal.
    chosen = [name for name in ("exit", "abort", "signal") if name in seen]
    for index, first in enumerate(chosen):
        for second in chosen[index + 1 :]:
            problem(1, "'%s' and '%s' are mutually exclusive" % (first, second))
    test.stdin = b"".join(s.encode() + b"\n" for s in stdin_lines)
    test.stdout = b"".join(s.encode() + b"\n" for s in stdout_lines)
    parse_body(test, test.entry, lines[header_end:], header_end + 1)


def _apply_text_directive(root, test, name, value, problem, lineno):
    if name == "flags":
        test.flags = value.split()
    elif name == "args":
        test.args = value.split()
    elif name == "link":
        test.links.append(value)
        if not (root / value).is_file():
            problem(lineno, "link: file not found: %s" % value)
    elif name == "exit":
        if not value.isdigit() or int(value) > MAX_EXIT:
            problem(lineno, "exit: expected a status between 0 and %d" % MAX_EXIT)
        else:
            test.exit = int(value)
    elif name == "signal":
        # The set of toolchain.md 7.3, and nothing else: a number is not portable
        # across the harness's native and qemu paths.
        if value not in SIGNALS:
            problem(lineno, "signal: expected one of %s" % ", ".join(sorted(SIGNALS)))
        else:
            test.signal_name = value
    elif name == "stderr":
        test.stderr.append(value)
    elif name == "error-any":
        test.error_any.append(value)


def parse_body(test, file, lines, first_lineno):
    """Collect the `//! error:` annotations from the code lines of `file`.

    `file` is relative to the corpus root. Sibling modules of a multi-file test
    carry no directives (D14.5), so a `//!` at the start of a line is a problem
    there; in the entry file it is a directive after the header.
    """
    sibling = file != test.entry

    def problem(lineno, message):
        test.problems.append("%s:%d: %s" % (file, lineno, message))

    for lineno, line in enumerate(lines, first_lineno):
        stripped = line.lstrip()
        if stripped[:3] == "//!":
            problem(
                lineno,
                "directives belong in main.ft only" if sibling else "directive after the header",
            )
        elif stripped[:3] in ("//|", "//<"):
            problem(lineno, "'%s' line outside the header" % stripped[:3])
        elif "//!" in line:
            match = ANNOTATION_RE.match(line)
            substring = match.group(2).strip() if match else ""
            if not match:
                problem(lineno, "only '//! error:' may follow code on a line")
            elif "//!" in substring:
                problem(lineno, "more than one annotation on the line")
            elif not substring:
                problem(lineno, "'error:' needs text")
            elif test.kind != "fail":
                problem(lineno, "'error' is only allowed in fail tests")
            else:
                test.errors.append((file, lineno, substring))


def load_test(root, test):
    """Parse the entry file and, for a multi-file test, every sibling `.ft`."""
    entry = root / test.entry
    parse_main(root, test, entry.read_text(encoding="utf-8", errors="replace"))
    if test.multi:
        for sibling in sorted((root / test.path).rglob("*.ft")):
            if sibling != entry:
                rel = sibling.relative_to(root).as_posix()
                text = sibling.read_text(encoding="utf-8", errors="replace")
                parse_body(test, rel, text.split("\n"), 1)
    if test.kind == "fail" and not test.errors and not test.error_any:
        test.problems.append("%s:1: fail test without an 'error' or 'error-any'" % test.entry)
    return test


# ---- discovery (D14.4) ----------------------------------------------------------------


def numbering_problems(where, names):
    """The gap and duplicate problems of the `NNN_`-prefixed `names`, from 001 up."""
    problems = []
    numbers = {}
    for name in names:
        match = NUMBERED_RE.match(name)
        if match:
            numbers.setdefault(int(match.group(1)), []).append(name)
    expected = 1
    for number in sorted(numbers):
        holders = numbers[number]
        if len(holders) > 1:
            problems.append("%s: duplicate number %03d: %s" % (where, number, ", ".join(holders)))
        if number != expected:
            problems.append(
                "%s: gap in numbering: expected %03d, found %03d" % (where, expected, number)
            )
        expected = number + 1
    return problems


def _discover_dir(root, directory, expected_kind, numbered, tests, problems):
    """Register the tests directly under `directory` (an area or programs/).

    Directory tests (`<name>/main.ft`) exist only under `modules` (D14.4).
    """
    names = []
    for entry in sorted(directory.iterdir()):
        rel = entry.relative_to(root).as_posix()
        if entry.is_dir():
            if directory.name != "modules":
                problems.append("%s: directory test outside modules" % rel)
            elif not (entry / "main.ft").is_file():
                problems.append("%s: directory without main.ft" % rel)
            elif not (NUMBERED_RE.match(entry.name) or NAME_RE.match(entry.name)):
                problems.append("%s: bad test name" % rel)
            else:
                names.append(entry.name)
                test = Test(rel, rel + "/main.ft", expected_kind)
                # The golden identifier index of D20.3 lives beside the test's
                # sources, so it is the one file there that is not fort.
                test.golden_index = (entry / INDEX_NAME).is_file()
                tests.append(test)
                for extra in sorted(entry.rglob("*")):
                    if not extra.is_file() or extra.suffix == ".ft":
                        continue
                    if extra.parent == entry and extra.name == INDEX_NAME:
                        problems.extend(golden_index_problems(root, extra))
                        continue
                    problems.append("%s: unexpected file" % extra.relative_to(root).as_posix())
        elif entry.suffix == ".ft":
            valid = NUMBERED_RE.match(entry.stem) if numbered else NAME_RE.match(entry.stem)
            if not valid:
                problems.append("%s: bad test name" % rel)
            else:
                names.append(entry.stem)
                tests.append(Test(rel, rel, expected_kind))
        else:
            problems.append("%s: unexpected file" % rel)
    if numbered:
        problems.extend(numbering_problems(directory.relative_to(root).as_posix(), names))


def discover(root):
    """Return (tests, problems) for the corpus under `root`, tests in path order."""
    tests = []
    problems = []
    for top, expected_kind in (("run", "run"), ("fail", "fail")):
        top_dir = root / top
        if not top_dir.is_dir():
            continue
        for area_dir in sorted(top_dir.iterdir()):
            rel = area_dir.relative_to(root).as_posix()
            if not area_dir.is_dir():
                problems.append("%s: unexpected file" % rel)
            elif area_dir.name not in AREAS:
                problems.append("%s: unknown area '%s'" % (rel, area_dir.name))
            else:
                _discover_dir(root, area_dir, expected_kind, True, tests, problems)
    programs = root / "programs"
    if programs.is_dir():
        _discover_dir(root, programs, "run", False, tests, problems)
    _discover_module_tests(root, tests, problems)
    _report_misplaced_tests(root, problems)
    tests.sort(key=lambda t: t.path)
    return tests, problems


# The directories of a corpus whose contents `discover` walks by name.
WALKED_TOPS = ("run", "fail", "programs")


def _report_misplaced_tests(root, problems):
    """Report a `<x>_test.ft` in a directory discovery does not walk.

    Everything below `root` other than `run`, `fail` and `programs` is invisible
    to discovery -- `test/fort/support`, which holds the code several module
    tests share, is the only such directory today -- so a test dropped there
    would run nowhere and say nothing, which is the hole the root's `bad test
    name` rule closes one directory up (T-079).
    """
    for entry in sorted(root.rglob("*.ft")):
        rel = entry.relative_to(root)
        if len(rel.parts) == 1 or rel.parts[0] in WALKED_TOPS:
            continue
        if entry.stem.endswith("_test"):
            problems.append("%s: test outside the root of the corpus" % rel.as_posix())


def _discover_module_tests(root, tests, problems):
    """Register the `<x>_test.ft` files directly under `root` as run tests.

    That is the shape of `test/fort`, where a test is a program importing a
    module of the self-hosted compiler with `-I` rather than an area of the
    language corpus (D14.4 areas do not describe the compiler's own modules),
    so a test is named after the module it exercises instead of numbered.
    `test/lang` holds no such file, so nothing there changes.
    """
    for entry in sorted(root.glob("*.ft")):
        rel = entry.relative_to(root).as_posix()
        # A `.ft` at the root that is not a test is a typo nothing would ever
        # run, as `containers_tets.ft` would be, so it is a problem rather
        # than a file to skip.
        if not NAME_RE.match(entry.stem) or not entry.stem.endswith("_test"):
            problems.append("%s: bad test name" % rel)
            continue
        tests.append(Test(rel, rel, "run"))


def load_expectations(path):
    """The path prefixes listed in `path` (None: none); `#` comments and blanks skipped."""
    prefixes = []
    if path is None:
        return prefixes
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if line:
                prefixes.append(line)
    return prefixes


def listed(test, prefixes):
    return any(test.path.startswith(p) for p in prefixes)


def expectation_problems(label, prefixes, tests):
    """Every prefix of an expectation list must select at least one test."""
    return [
        "%s: '%s' matches no test" % (label, p)
        for p in prefixes
        if not any(listed(t, [p]) for t in tests)
    ]


def select(tests, filters):
    """Positional filters select the tests whose path contains any of them."""
    if not filters:
        return tests
    return [t for t in tests if any(f in t.path for f in filters)]


def exclude_exact(tests, paths):
    """Remove only tests whose complete corpus path occurs in `paths`."""
    return [test for test in tests if test.path not in paths]


# ---- judging --------------------------------------------------------------------------


def parse_diagnostics(stderr, root):
    """The D14.2 error lines of a compiler's stderr, in order."""
    diagnostics = []
    for raw in stderr.decode("utf-8", errors="replace").split("\n"):
        match = DIAGNOSTIC_RE.match(raw)
        if match:
            file = _normalize_file(match.group(1), root)
            diagnostics.append(
                Diagnostic(file, int(match.group(2)), int(match.group(3)), match.group(4))
            )
    return diagnostics


def parse_reports(stderr, root):
    """Every `error:` and `note:` line of a compiler's stderr, in order (D14.2).

    A note belongs to the error before it, which is what the document nests
    (D20.2), so the two forms are compared as one sequence.
    """
    reports = []
    for raw in stderr.decode("utf-8", errors="replace").split("\n"):
        match = REPORT_RE.match(raw)
        if match:
            reports.append(
                Diagnostic(
                    _normalize_file(match.group(1), root),
                    int(match.group(2)),
                    int(match.group(3)),
                    match.group(5),
                    match.group(4),
                )
            )
    return reports


def _normalize_file(file, root):
    """Diagnostic paths are relative to the corpus root (D14.4); absolute ones are mapped back."""
    path = Path(file)
    if path.is_absolute():
        try:
            return path.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            return path.as_posix()
    return os.path.normpath(file).replace(os.sep, "/")


def _first_line(data, limit=200):
    text = data.decode("utf-8", errors="backslashreplace").strip()
    return text.split("\n", 1)[0][:limit]


def _describe_status(returncode):
    if returncode < 0:
        # Name the signal the way a `signal:` directive spells it, so the two
        # sides of a failure message can be compared by eye.
        for name, number in SIGNALS.items():
            if number == -returncode:
                return "SIG" + name
        return "signal %d" % -returncode
    return "exit %d" % returncode


def judge_compile(proc, expect_success):
    """Return (verdict, reason) for the compiler step, or (None, "") when it is as expected.

    Exit 0 and 1 are the compiler's answer about the test (D14.1); 2 (usage,
    toolchain or internal error), a signal or a timeout are not, so they are
    reported as ERROR.
    """
    if proc.timed_out:
        return "ERROR", "compiler timed out"
    if not proc.started:
        return "ERROR", "compiler failed to start: " + _first_line(proc.stderr)
    if proc.returncode == 0:
        if expect_success:
            return None, ""
        return "FAIL", "compiled successfully, expected exit 1"
    if proc.returncode == 1:
        if not expect_success:
            return None, ""
        detail = _first_line(proc.stderr)
        return "FAIL", "compiler exited 1" + (": " + detail if detail else "")
    if proc.returncode < 0:
        return "ERROR", "compiler killed by signal %d" % -proc.returncode
    detail = _first_line(proc.stderr)
    return "ERROR", "compiler exited %d" % proc.returncode + (": " + detail if detail else "")


def _stderr_problems(expected, stderr):
    text = stderr.decode("utf-8", errors="replace")
    return ["stderr lacks '%s'" % s for s in expected if s not in text]


def drop_qemu_notice(stderr):
    """The program's stderr without the lines qemu-user adds when a signal kills it."""
    lines = stderr.split(b"\n")
    kept = [line for line in lines if not line.startswith(QEMU_NOTICE_PREFIX)]
    return b"\n".join(kept)


def _split_output(data):
    """The lines of `data`; an unterminated last line counts as a line."""
    parts = data.decode("utf-8", errors="backslashreplace").split("\n")
    return parts if parts[-1] else parts[:-1]


def _stdout_problems(expected, actual):
    """Describe the first difference between the expected and actual stdout (byte exact)."""
    if expected == actual:
        return []
    want = _split_output(expected)
    got = _split_output(actual)
    for i, (w, g) in enumerate(zip(want, got), 1):
        if w != g:
            return ["stdout line %d: expected %r, got %r" % (i, w, g)]
    if len(got) < len(want):
        return ["stdout ends after line %d, expected %r" % (len(got), want[len(got)])]
    if len(got) > len(want):
        return ["stdout line %d unexpected: %r" % (len(want) + 1, got[len(want)])]
    return ["stdout lacks the final newline"]


def judge_fail(test, compile_proc, root=ROOT):
    """Judge a fail test: exit 1 and only annotated diagnostics (D14.5)."""
    verdict, reason = judge_compile(compile_proc, False)
    if verdict:
        return verdict, reason
    diagnostics = parse_diagnostics(compile_proc.stderr, root)
    if not diagnostics:
        return "FAIL", "compiler exited 1 without diagnostics"
    problems = []
    annotated = {(f, line) for f, line, _ in test.errors}
    for file, line, substring in test.errors:
        on_line = [d for d in diagnostics if d.file == file and d.line == line]
        if not on_line:
            problems.append("no diagnostic on %s:%d (expected '%s')" % (file, line, substring))
        elif not any(substring in d.message for d in on_line):
            problems.append(
                "diagnostic on %s:%d lacks '%s': %s" % (file, line, substring, on_line[0].message)
            )
    for substring in test.error_any:
        if not any(substring in d.message for d in diagnostics):
            problems.append("no diagnostic containing '%s'" % substring)
    for d in diagnostics:
        if (d.file, d.line) not in annotated and not any(s in d.message for s in test.error_any):
            problems.append("unannotated diagnostic %s:%d: %s" % (d.file, d.line, d.message))
    problems.extend(_stderr_problems(test.stderr, compile_proc.stderr))
    if problems:
        return "FAIL", "; ".join(problems)
    return "PASS", ""


def judge_run(test, compile_proc, link_proc, run_proc):
    """Judge a run test: compiler exit 0, then stdout, status and stderr (D14.5).

    A failure of the harness's own link step or of launching the program is
    the toolchain's, not the test's, so it is an ERROR like a compiler exit 2
    (D14.1); a program that times out is a FAIL.

    The `stderr:` substrings are looked for after dropping the notice qemu-user
    appends when a signal kills the program, so an `abort` or `signal:` test
    sees the same stderr under qemu as natively.
    """
    verdict, reason = judge_compile(compile_proc, True)
    if verdict:
        return verdict, reason
    if link_proc is not None and link_proc.returncode != 0:
        if link_proc.timed_out:
            return "ERROR", "link timed out"
        return "ERROR", "link failed: %s" % _first_line(link_proc.stderr)
    if not run_proc.started:
        return "ERROR", "program failed to start: " + _first_line(run_proc.stderr)
    if run_proc.timed_out:
        return "FAIL", "program timed out"
    problems = _stdout_problems(test.stdout, run_proc.stdout)
    # `abort` is the spelling of `signal: ABRT` (toolchain.md 7.3).
    wanted = "ABRT" if test.abort else test.signal_name
    if wanted:
        if run_proc.returncode != -SIGNALS[wanted]:
            problems.append(
                "expected SIG%s, got %s" % (wanted, _describe_status(run_proc.returncode))
            )
    elif run_proc.returncode != test.exit:
        problems.append(
            "expected exit %d, got %s" % (test.exit, _describe_status(run_proc.returncode))
        )
    problems.extend(_stderr_problems(test.stderr, drop_qemu_notice(run_proc.stderr)))
    if problems:
        return "FAIL", "; ".join(problems)
    return "PASS", ""


def owns_file(test, file):
    """The file belongs to the test: the test itself, or one of its directory."""
    if file in (test.entry, test.path):
        return True
    return test.multi and file.startswith(test.path + "/")


def judge_unsupported(test, compile_proc, root=ROOT):
    """A test the compiler's list names must be refused, and refused by it.

    Refused: exit 1 with at least one diagnostic, never exit 0 (toolchain.md
    7.3). By it: at least one of those diagnostics must stand in a file of the
    test itself, or must carry UNSUPPORTED_MESSAGE.

    Why two shapes and not one (T-131). Until T-131 the C bootstrap grew a
    `not supported by the bootstrap compiler` diagnostic for each feature it
    lacks, because it compiled the whole repository and a listed test had to
    say why it was refused. It compiles pin 0's tree now, so a feature added
    after pin 0 reaches it as a plain syntax error from its lexer or its
    parser, with no such words. The test's own file is what says the refusal
    is about the test: a compiler that reports nothing, or that reports only
    about a module of the library, passes neither shape and fails.

    The second shape is not the first with more words. A listed test that
    spells no unsupported form of its own is refused inside the library's
    closure -- run/stdlib/096_math_limits.ft imports std.math, whose `?:` the
    C bootstrap refuses -- so its diagnostics name `std/math.ft` and never the
    test. Nine such diagnostics, measured on 2026-09-14, and no diagnostic in
    the test. The first shape alone would fail it.
    """
    verdict, reason = judge_compile(compile_proc, False)
    if verdict:
        return verdict, reason
    diagnostics = parse_diagnostics(compile_proc.stderr, root)
    if not diagnostics:
        return "FAIL", "compiler exited 1 without diagnostics"
    if any(UNSUPPORTED_MESSAGE in d.message for d in diagnostics):
        return "PASS", ""
    if any(owns_file(test, d.file) for d in diagnostics):
        return "PASS", ""
    return "FAIL", "no diagnostic in %s and none containing '%s'" % (
        test.path,
        UNSUPPORTED_MESSAGE,
    )


# ---- the check document (D20.2) -------------------------------------------------------


def source_lines(root, file, cache):
    """The lines of a file of the closure as byte strings, or None.

    Columns are byte columns (D14.2), so the file is read as bytes; a file
    ending in a newline yields a last, empty line, which is where the end of
    the file is.
    """
    if file not in cache:
        try:
            cache[file] = (root / file).read_bytes().split(b"\n")
        except OSError:
            cache[file] = None
    return cache[file]


def empty_range_is_allowed(record, lines):
    """Whether an empty range is a position the compiler has no extent for.

    Three are: the 1:1 of an error without a position in the file (D14.2), the
    end of the file, where the token has no bytes, and a lexical error, whose
    message says so, since the lexer reports the position of bytes that are
    not a token (D14.2, D20.4). Every other diagnostic covers the tokens it is
    about, so its range is non-empty.
    """
    if (record["line"], record["col"]) == (1, 1):
        return True
    if record["line"] == len(lines) and record["col"] == len(lines[-1]) + 1:
        return True
    # A record of the index has no message and no other empty range: a name
    # token has bytes (D20.3).
    return any(text in record.get("message", "") for text in LEXICAL_MESSAGES)


def range_problems(where, record, lines):
    """The problems of one range of D20.4: ordered, inside its file, non-empty.

    The start is inclusive and the end exclusive, so a column one past the last
    byte of its line is in range.
    """
    problems = []
    start = (record["line"], record["col"])
    end = (record["end_line"], record["end_col"])
    if end < start:
        problems.append("%s: range %d:%d-%d:%d ends before it starts" % ((where,) + start + end))
    for what, (line, col) in (("start", start), ("end", end)):
        if line < 1 or line > len(lines):
            problems.append("%s: %s line %d is outside the file" % (where, what, line))
        elif col < 1 or col > len(lines[line - 1]) + 1:
            problems.append("%s: %s column %d is outside line %d" % (where, what, col, line))
    if not problems and start == end and not empty_range_is_allowed(record, lines):
        problems.append("%s: empty range at %d:%d" % ((where,) + start))
    return problems


def _keys_problem(where, record, keys):
    if sorted(record) != sorted(keys):
        return ["%s: keys are %s, expected %s" % (where, sorted(record), sorted(keys))]
    return []


def _record_problems(where, record, keys, root, files, cache):
    """The shape and the range of one diagnostic or note (D20.2, D20.4)."""
    problems = _keys_problem(where, record, keys)
    if problems:
        return problems
    return _range_problems(where, record, root, files, cache)


def render_index(symbols):
    """The golden form of the `"symbols"` array: one record per line (D20.3).

    The keys keep the document's order, so a golden file is a diff of the index
    and never of a formatter.
    """
    return "".join(json.dumps(record, separators=(", ", ": ")) + "\n" for record in symbols)


def golden_index_problems(root, path):
    """The lint of a golden index: one record per line, each of the shape of D20.3.

    A record is also held against `render_index`, so a golden file that was
    hand-edited into another spelling of the same records fails here rather
    than in the run it is compared byte for byte in.
    """
    where = path.relative_to(root).as_posix()
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as e:
        return ["%s: cannot be read: %s" % (where, e)]
    problems = []
    lines = text.splitlines()
    if not lines:
        problems.append("%s: no records" % where)
    for i, line in enumerate(lines, start=1):
        try:
            record = json.loads(line)
        except ValueError as e:
            problems.append("%s:%d: not one JSON record: %s" % (where, i, e))
            continue
        if not isinstance(record, dict):
            problems.append("%s:%d: not an object" % (where, i))
            continue
        found = _keys_problem("%s:%d" % (where, i), record, SYMBOL_KEYS)
        if found:
            problems.extend(found)
        elif record["kind"] not in SYMBOL_KINDS:
            problems.append("%s:%d: kind is %r" % (where, i, record["kind"]))
        elif render_index([record]) != line + "\n":
            problems.append("%s:%d: not spelled as the harness writes a record" % (where, i))
    return problems


def _range_problems(where, record, root, files, cache):
    """One range of D20.4: in a file of the closure, inside it and ordered."""
    problems = []
    file = _normalize_file(record["file"], root)
    if file not in files:
        problems.append("%s: '%s' is not in \"files\"" % (where, file))
    lines = source_lines(root, file, cache)
    if lines is None:
        problems.append("%s: '%s' cannot be read" % (where, file))
    else:
        problems.extend(range_problems(where, record, lines))
    return problems


def symbol_problems(where, record, root, files, cache):
    """The problems of one record of the identifier index (D20.3).

    Its shape, its kind, its type, its own range, and its declaration:
    `"type"` is a string or null, `"decl"` is a range in the closure, it is
    null only for a builtin, and on an occurrence that declares the name it is
    that occurrence's own range -- except on an `as` alias, which declares a
    name here for a declaration that stands in another file (D9.3).
    """
    problems = _keys_problem(where, record, SYMBOL_KEYS)
    if problems:
        return problems
    if record["kind"] not in SYMBOL_KINDS:
        problems.append("%s: kind is %r" % (where, record["kind"]))
    if not isinstance(record["is_decl"], bool):
        problems.append("%s: is_decl is %r" % (where, record["is_decl"]))
    if record["type"] is not None and not isinstance(record["type"], str):
        # The type is absent only when the declaration failed to check (D20.3).
        problems.append("%s: type is %r" % (where, record["type"]))
    problems.extend(_range_problems(where, record, root, files, cache))
    decl = record["decl"]
    if decl is None:
        # No source declares a builtin, and everything else is declared
        # somewhere (D12.2, D20.3).
        if record["kind"] != "builtin":
            problems.append('%s: only a builtin has a null "decl"' % where)
        return problems
    found = _keys_problem("%s.decl" % where, decl, RANGE_KEYS)
    if found:
        return problems + found
    problems.extend(_range_problems("%s.decl" % where, decl, root, files, cache))
    same_range = [decl[k] for k in RANGE_KEYS] == [record[k] for k in RANGE_KEYS]
    elsewhere = _normalize_file(decl["file"], root) != _normalize_file(record["file"], root)
    if record["is_decl"] and not same_range and not elsewhere:
        # A declaration is its own "decl"; only an alias declares a name here
        # for something declared in another file (D9.3, D20.3).
        problems.append('%s: the declaration\'s "decl" is not its own range' % where)
    return problems


def document_problems(doc, root, cache, indexed=False):
    """The problems of one document of D20.2: its shape, its files and its ranges."""
    if not isinstance(doc, dict):
        return ["the document is not an object"]
    problems = _keys_problem("document", doc, DOCUMENT_KEYS)
    if problems:
        return problems
    if doc["version"] != DOCUMENT_VERSION:
        problems.append("version is %r, expected %d" % (doc["version"], DOCUMENT_VERSION))
    files = [_normalize_file(f, root) for f in doc["files"]]
    for file in files:
        if source_lines(root, file, cache) is None:
            problems.append("files: '%s' cannot be read" % file)
    if not indexed:
        # The index is filled by --index alone and is a member of every
        # document either way (D20.2, D20.3).
        if doc["symbols"] != []:
            problems.append("symbols is not empty: %r" % (doc["symbols"],))
    else:
        for i, record in enumerate(doc["symbols"]):
            problems.extend(symbol_problems("symbols[%d]" % i, record, root, files, cache))
    after_error = False
    for i, diagnostic in enumerate(doc["diagnostics"]):
        where = "diagnostics[%d]" % i
        found = _record_problems(where, diagnostic, DIAGNOSTIC_KEYS, root, files, cache)
        problems.extend(found)
        if found:
            continue
        if diagnostic["severity"] not in ("error", "note"):
            problems.append("%s: severity is %r" % (where, diagnostic["severity"]))
        if diagnostic["severity"] == "note" and after_error:
            # A note belongs to the error before it and is nested in its
            # "notes", never listed beside it (D20.2, D14.2).
            problems.append('%s: a note after an error must be nested in its "notes"' % where)
        after_error = after_error or diagnostic["severity"] == "error"
        for j, note in enumerate(diagnostic["notes"]):
            problems.extend(
                _record_problems("%s.notes[%d]" % (where, j), note, NOTE_KEYS, root, files, cache)
            )
    return problems


def _report_of(record, root, severity):
    return Diagnostic(
        _normalize_file(record["file"], root),
        record["line"],
        record["col"],
        record["message"],
        severity,
    )


def document_reports(doc, root):
    """The document's records in the order the text form prints them (D20.2).

    Each diagnostic comes first and the notes nested in it follow, which is
    where the text form of D14.2 puts them.
    """
    reports = []
    for diagnostic in doc["diagnostics"]:
        reports.append(_report_of(diagnostic, root, diagnostic["severity"]))
        for note in diagnostic["notes"]:
            reports.append(_report_of(note, root, "note"))
    return reports


def sequence_problems(from_text, from_json):
    """Where the document and the text form differ, as sequences (D20.2).

    The document lists its diagnostics in the order they were reported, which
    is the order of the text form, so a reordered or a duplicated record is a
    difference and not just a missing one.
    """
    problems = []
    for i, (text, document) in enumerate(zip(from_text, from_json)):
        if text != document:
            problems.append(
                "report %d is '%s' in the text form and '%s' in the document"
                % (i + 1, text.text(), document.text())
            )
            return problems
    for text in from_text[len(from_json) :]:
        problems.append("only in the text form: %s" % text.text())
    for document in from_json[len(from_text) :]:
        problems.append("only in the document: %s" % document.text())
    return problems


def _answer_problem(proc, label):
    """(verdict, reason) when `proc` is not an answer of D14.1: exit 0 or 1."""
    if proc.timed_out:
        return "ERROR", "%s: compiler timed out" % label
    if not proc.started:
        return "ERROR", "%s: compiler failed to start: %s" % (label, _first_line(proc.stderr))
    if proc.returncode < 0:
        return "ERROR", "%s: compiler killed by signal %d" % (label, -proc.returncode)
    if proc.returncode > 1:
        detail = _first_line(proc.stderr)
        return "ERROR", "%s: compiler exited %d%s" % (
            label,
            proc.returncode,
            ": " + detail if detail else "",
        )
    return None, ""


def files_problems(test, files):
    """What `"files"` must hold whatever the run reported (D20.2).

    It lists every file the compiler read, so the entry file is always among
    them; every other file a diagnostic is about is checked with that
    diagnostic. A module of the test that the walk never reached is
    legitimately absent: an import rejected before its file is opened, as in
    `fail/modules/003_late_import`, leaves the sibling unread.
    """
    if test.entry in files:
        return []
    return ["files: the entry '%s' is missing" % test.entry]


def index_of_the_test(test, symbols):
    """The records of `symbols` that are about the test's own files (D20.3).

    The index covers every module of the closure that was checked, and every
    closure holds `std.rt` and what it imports (D9.10), so a run over any test
    answers with the whole standard library's records too. Those cannot stand
    in a golden: their file names are the `--std-dir` the run was given, which
    is a build directory and differs between machines. The golden therefore
    holds the records of the files under the test's own directory, which is
    what the test is about, and the library's records are dropped here.
    """
    prefix = test.path + "/"
    return [record for record in symbols if str(record.get("file", "")).startswith(prefix)]


def golden_index_diff(test, symbols, root):
    """The first line on which the index differs from the test's golden (D20.3).

    The golden is the `"symbols"` of the run over the test's own files, one
    record per line, so a mismatch names the line and shows both spellings of
    it.
    """
    path = root / test.path / INDEX_NAME
    try:
        want = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as e:
        return ["%s: cannot be read: %s" % (INDEX_NAME, e)]
    got = render_index(index_of_the_test(test, symbols))
    if got == want:
        return []
    want_lines, got_lines = want.splitlines(), got.splitlines()
    for i in range(max(len(want_lines), len(got_lines))):
        wanted = want_lines[i] if i < len(want_lines) else "(no record)"
        found = got_lines[i] if i < len(got_lines) else "(no record)"
        if wanted != found:
            return ["%s:%d: expected %s, got %s" % (INDEX_NAME, i + 1, wanted, found)]
    return ["%s: differs from the index" % INDEX_NAME]


def judge_check_json(test, text_proc, json_proc, root=ROOT):
    """Judge one test's `--check --json` run against its own text form.

    The document must be the only thing on stdout and have the shape of D20.2,
    every range of D20.4 must lie inside its file, `"files"` must name the
    entry and every file a diagnostic is about, and the two
    runs must agree on the exit status and, record for record and in order, on
    the errors and notes the text form of D14.2 prints. A run that exits 2 is
    not an answer (D14.1) and must leave stdout empty, which is how a client
    tells a crash from a verdict (D20.2). A test with a golden index was run
    with `--index`, so its `"symbols"` must match that file byte for byte
    (D20.3).
    """
    document_verdict, document_reason = _answer_problem(json_proc, "--json")
    if document_verdict and json_proc.stdout:
        # Exit 2 with a document on stdout is the one contract a client cannot
        # work around, so it is a failure and not an error (D20.2).
        return "FAIL", "stdout is not empty after %s" % _describe_status(json_proc.returncode)
    for proc, label in ((text_proc, "text"), (json_proc, "--json")):
        verdict, reason = _answer_problem(proc, label)
        if verdict:
            return verdict, reason
    problems = []
    if json_proc.returncode != text_proc.returncode:
        problems.append(
            "--json exited %d, the text form %d" % (json_proc.returncode, text_proc.returncode)
        )
    if json_proc.stderr.strip():
        problems.append("--json wrote to stderr: " + _first_line(json_proc.stderr))
    try:
        doc = json.loads(json_proc.stdout.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as e:
        return "FAIL", "; ".join(problems + ["stdout is not one JSON document: %s" % e])
    problems.extend(document_problems(doc, root, {}, test.golden_index))
    if problems:
        return "FAIL", "; ".join(problems)
    problems.extend(files_problems(test, [_normalize_file(f, root) for f in doc["files"]]))
    problems.extend(
        sequence_problems(parse_reports(text_proc.stderr, root), document_reports(doc, root))
    )
    if test.golden_index:
        problems.extend(golden_index_diff(test, doc["symbols"], root))
    if problems:
        return "FAIL", "; ".join(problems)
    return "PASS", ""


def apply_expectations(verdict, reason, xfail, label=XFAIL_NAME):
    """Map a verdict through the expectation list: a listed failure is expected, a pass is not.

    ERROR is covered too: a listed test is one the compiler cannot handle yet,
    however it fails (the stage1 scaffold exits 2 on every input). `label` is
    the list in use, since a run may be given another one with --xfail (stage2
    has its own, xfail-stage2.txt) and an XPASS must name the file to edit.
    """
    if not xfail:
        return verdict, reason
    if verdict == "PASS":
        return "XPASS", "listed in %s but passed" % label
    return "XFAIL", reason


# ---- execution ----------------------------------------------------------------------


@dataclasses.dataclass
class Config:
    root: Path
    fort: str
    std_dir: str
    cc: str = DEFAULT_CC
    target: str = DEFAULT_TARGET
    opt: str = DEFAULT_OPT
    verify_ir: bool = False
    check_json: bool = False
    runner: list = dataclasses.field(default_factory=list)
    timeout: float = DEFAULT_TIMEOUT
    keep: bool = False
    verbose: bool = False


def child_env(workdir, target):
    env = dict(os.environ)
    env["TMPDIR"] = workdir
    env["LC_ALL"] = "C"
    if re.fullmatch(r"arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+", target):
        env.pop("QEMU_LD_PREFIX", None)
    else:
        env.setdefault("QEMU_LD_PREFIX", DEFAULT_QEMU_LD_PREFIX)
    return env


def run_process(argv, cwd, env, timeout, stdin=b""):
    """Run `argv` in its own process group and capture it.

    A timeout kills the whole group, so a child of the program cannot keep
    the output pipes open, and is recorded in the result.
    """
    try:
        child = subprocess.Popen(
            argv,
            cwd=cwd,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
    except OSError as e:
        return Proc(argv, MAX_EXIT, b"", ("%s: %s" % (argv[0], e)).encode(), started=False)
    try:
        stdout, stderr = child.communicate(stdin, timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout, stderr = child.communicate()
        return Proc(argv, -1, stdout, stderr, timed_out=True)
    return Proc(argv, child.returncode, stdout, stderr)


def compile_command(config, test, output, compile_only=False, emit_ir=False):
    """`fort --cc CC --std-dir D <flags> [-c|-S] -o <output> <entry>` (toolchain.md 7.3).

    `-S` stops after the LLVM IR module (D14.1, D19.1).
    """
    argv = [config.fort, "--cc", config.cc, "--std-dir", config.std_dir]
    argv.extend(test.flags)
    if compile_only:
        argv.append("-c")
    if emit_ir:
        argv.append("-S")
    argv.extend(["-o", output, test.entry])
    return argv


def check_command(config, test, as_json, indexed=False):
    """`fort --check [--json] [--index] <flags> <entry>`: the front end alone (D20.1).

    `-o`, `-c`, `-S` and `--cc` are unused under `--check`, so the command
    carries neither an output nor a compiler; `--json` turns the text
    diagnostics into the one document of D20.2 on stdout, and `--index` fills
    that document's identifier index (D20.3).
    """
    argv = [config.fort, "--check", "--std-dir", config.std_dir]
    if as_json:
        argv.append("--json")
    if indexed:
        argv.append("--index")
    argv.extend(test.flags)
    argv.append(test.entry)
    return argv


def link_command(config, test, prog, obj):
    """`<cc> --target=<triple> -o prog prog.o <link: files>` for a test with helpers.

    The object holds the whole program, the runtime included (D9.10, D13.1),
    so the link takes no object beside the helpers.
    `--cc` is a clang and names its target on the command line (D14.1, D14.3),
    so the harness spells the triple here exactly as the compiler does, and
    `-O1` as well: the compiler builds the fort side at `-O1` (toolchain.md 2),
    and a helper built at `-O0` cannot see a wrong `zeroext`/`signext` on the
    fort side, because an unoptimised callee spills its narrow parameter to a
    stack slot and re-narrows it. At `-O1` the callee keeps the argument under
    the assertion its parameter attribute states and folds the re-narrowing
    away, so the caller's wrong extension reaches the arithmetic and the
    mirror reports it (D9.8, D9.9).
    """
    argv = [config.cc, "--target=" + config.target, "-O1", "-o", prog, obj]
    argv.extend(str(config.root / link) for link in test.links)
    return argv


def verify_command(config, module):
    """`<opt> -passes=verify -disable-output <module>`: the module must verify (D19.1)."""
    return [config.opt, "-passes=verify", "-disable-output", module]


def verify_module(config, test, workdir, env, procs):
    """Emit the test's LLVM IR with `-S` and verify it (`--verify-ir`).

    Every emitted module must pass `opt -passes=verify`, and this harness is
    where that is checked (D19.1). Returns (verdict, reason), or (None, "")
    when the module verifies. A module the verifier rejects is the compiler's
    fault, hence a FAIL; a verifier that cannot be launched, times out or dies
    by a signal is the toolchain's, hence an ERROR, like a compiler exit 2
    (D14.1).
    """
    module = os.path.join(workdir, "prog.ll")
    argv = compile_command(config, test, module, emit_ir=True)
    emit = run_process(argv, config.root, env, config.timeout)
    procs.append(emit)
    verdict, reason = judge_compile(emit, True)
    if verdict:
        return verdict, "-S: " + reason
    proc = run_process(verify_command(config, module), workdir, env, config.timeout)
    procs.append(proc)
    if not proc.started:
        return "ERROR", "the IR verifier failed to start: " + _first_line(proc.stderr)
    if proc.timed_out:
        return "ERROR", "the IR verifier timed out"
    if proc.returncode < 0:
        return "ERROR", "the IR verifier was killed by signal %d" % -proc.returncode
    if proc.returncode != 0:
        return "FAIL", "the IR verifier rejected the module: " + _first_line(proc.stderr)
    return None, ""


def execute_check_json(config, test):
    """Run one test twice under `--check`, once with `--json`, and compare.

    The two runs are the same mode with one option between them, so what they
    report must be the same; `judge_check_json` says how (D20.1, D20.2).
    """
    if test.problems:
        return Result(test, "ERROR", "invalid directives: " + test.problems[0])
    workdir = tempfile.mkdtemp(prefix="fort-check-json-")
    env = child_env(workdir, config.target)
    procs = []
    try:
        text = run_process(check_command(config, test, False), config.root, env, config.timeout)
        procs.append(text)
        argv = check_command(config, test, True, test.golden_index)
        document = run_process(argv, config.root, env, config.timeout)
        procs.append(document)
        verdict, reason = judge_check_json(test, text, document, config.root)
        return Result(test, verdict, reason, procs, workdir)
    finally:
        if not config.keep:
            shutil.rmtree(workdir, ignore_errors=True)


def execute(config, test, unsupported):
    """Compile, link and run one test in a fresh temporary directory; return its Result.

    The compiler runs with the corpus root as working directory (D14.4) and
    `TMPDIR` pointed at the temporary directory; the program runs inside it.
    """
    if config.check_json:
        return execute_check_json(config, test)
    if test.problems:
        return Result(test, "ERROR", "invalid directives: " + test.problems[0])
    workdir = tempfile.mkdtemp(prefix="fort-lang-")
    env = child_env(workdir, config.target)
    procs = []
    try:
        prog = os.path.join(workdir, "prog")
        if unsupported or test.kind == "fail":
            proc = run_process(
                compile_command(config, test, prog), config.root, env, config.timeout
            )
            procs.append(proc)
            if unsupported:
                verdict, reason = judge_unsupported(test, proc, config.root)
            else:
                verdict, reason = judge_fail(test, proc, config.root)
            return Result(test, verdict, reason, procs, workdir)
        link_proc = None
        if test.links:
            obj = os.path.join(workdir, "prog.o")
            argv = compile_command(config, test, obj, compile_only=True)
            compile_proc = run_process(argv, config.root, env, config.timeout)
            procs.append(compile_proc)
            if compile_proc.returncode == 0:
                link_proc = run_process(
                    link_command(config, test, prog, obj), workdir, env, config.timeout
                )
                procs.append(link_proc)
        else:
            argv = compile_command(config, test, prog)
            compile_proc = run_process(argv, config.root, env, config.timeout)
            procs.append(compile_proc)
        linked = link_proc is None or link_proc.returncode == 0
        if config.verify_ir and compile_proc.returncode == 0 and linked:
            verdict, reason = verify_module(config, test, workdir, env, procs)
            if verdict:
                return Result(test, verdict, reason, procs, workdir)
        run_proc = None
        if compile_proc.returncode == 0 and linked:
            argv = config.runner + [prog] + test.args
            run_proc = run_process(argv, workdir, env, config.timeout, test.stdin)
            procs.append(run_proc)
        verdict, reason = judge_run(test, compile_proc, link_proc, run_proc)
        return Result(test, verdict, reason, procs, workdir)
    finally:
        if not config.keep:
            shutil.rmtree(workdir, ignore_errors=True)


def describe(result):
    """The verbose report of a result: every command with its status and output."""
    lines = []
    for proc in result.procs:
        status = "timed out" if proc.timed_out else _describe_status(proc.returncode)
        lines.append("  $ %s  [%s]" % (" ".join(proc.argv), status))
        for label, data in (("stdout", proc.stdout), ("stderr", proc.stderr)):
            text = data.decode("utf-8", errors="backslashreplace")
            if text:
                lines.append("  %s:" % label)
                lines.extend("    " + t for t in text.rstrip("\n").split("\n"))
    if result.workdir and os.path.isdir(result.workdir):
        lines.append("  kept: %s" % result.workdir)
    return "\n".join(lines)


# ---- command line ---------------------------------------------------------------------


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "filters", nargs="*", help="run only the tests whose path contains a filter"
    )
    parser.add_argument(
        "--fort",
        default=os.environ.get("FORT", str(ROOT.parent.parent / "build" / "debug" / "fort")),
        help="the compiler (default: $FORT or build/debug/fort)",
    )
    parser.add_argument(
        "--std-dir",
        default=os.environ.get("FORT_STD_DIR"),
        help="passed as --std-dir (default: $FORT_STD_DIR or std beside the compiler)",
    )
    parser.add_argument(
        "--cc", default=DEFAULT_CC, help="clang for --cc and link: (default: %(default)s)"
    )
    parser.add_argument(
        "--target", default=DEFAULT_TARGET, help="--target= of the link step (default: %(default)s)"
    )
    parser.add_argument(
        "--verify-ir",
        action="store_true",
        help="also emit each compiling test's IR with -S and verify it with opt",
    )
    parser.add_argument(
        "--check-json",
        action="store_true",
        help="instead of running the tests, hold the check document against the text form",
    )
    parser.add_argument("--opt", default=DEFAULT_OPT, help="LLVM opt (default: %(default)s)")
    parser.add_argument(
        "--runner", default="", help="command that runs the programs, e.g. qemu-x86_64"
    )
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="tests run in parallel")
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help="seconds per step (default: %(default)s)",
    )
    parser.add_argument(
        "--xfail", default=None, help="expected-failure list (default: %s)" % XFAIL_NAME
    )
    parser.add_argument("--no-xfail", action="store_true", help="ignore the expected-failure list")
    parser.add_argument(
        "--unsupported",
        default=None,
        help="bootstrap-unsupported list (default: %s)" % UNSUPPORTED_NAME,
    )
    parser.add_argument(
        "--no-unsupported", action="store_true", help="ignore the bootstrap-unsupported list"
    )
    parser.add_argument(
        "--exclude-exact",
        action="append",
        default=[],
        metavar="PATH",
        help="exclude one complete corpus path (repeatable)",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="show the commands and outputs of failures"
    )
    parser.add_argument("--keep", action="store_true", help="keep the temporary directories")
    parser.add_argument("--list", action="store_true", help="list the selected tests and exit")
    parser.add_argument("--lint", action="store_true", help="validate the directives and exit")
    parser.add_argument("--root", default=str(ROOT), help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def expectation_file(given, default):
    """The default list may be absent (no expectations); an explicit one must exist."""
    if given is None:
        return str(default) if default.is_file() else None
    if not os.path.exists(given):
        sys.exit("run_tests.py: error: no such file: %s" % given)
    return given


def load_corpus(root, args):
    """Discover and parse the corpus; return (tests, problems, xfail, unsupported)."""
    tests, problems = discover(root)
    for test in tests:
        load_test(root, test)
        problems.extend(test.problems)
    xfail = []
    if not args.no_xfail:
        path = expectation_file(args.xfail, root / XFAIL_NAME)
        xfail = load_expectations(path)
        problems.extend(expectation_problems(args.xfail or XFAIL_NAME, xfail, tests))
    unsupported = []
    if not args.no_unsupported:
        path = expectation_file(args.unsupported, root / UNSUPPORTED_NAME)
        unsupported = load_expectations(path)
        problems.extend(
            expectation_problems(args.unsupported or UNSUPPORTED_NAME, unsupported, tests)
        )
    return tests, problems, xfail, unsupported


def main(argv=None):
    args = parse_args(argv)
    root = Path(args.root).resolve()
    tests, problems, xfail, unsupported = load_corpus(root, args)
    # The name an XPASS tells the reader to edit: the list actually in use.
    xfail_label = os.path.basename(args.xfail) if args.xfail else XFAIL_NAME
    selected = select(tests, args.filters)
    for path in args.exclude_exact:
        if not any(test.path == path for test in selected):
            problems.append("--exclude-exact: '%s' matches no selected test" % path)
    selected = exclude_exact(selected, args.exclude_exact)
    if args.check_json:
        # The document is compared on the tests that have something to compare:
        # every fail test, which has diagnostics (D20.2), and every test with a
        # golden identifier index beside it (D20.3).
        selected = [t for t in selected if t.kind == "fail" or t.golden_index]
    if args.list:
        for test in selected:
            print(test.path)
        print("%d tests" % len(selected))
        return 0
    if problems:
        for p in problems:
            print("lint: " + p)
        print("run_tests.py: %d problems in %d tests" % (len(problems), len(tests)))
        return 1
    if args.lint:
        print("run_tests.py: %d tests, no problems" % len(tests))
        return 0
    if not selected:
        if args.check_json:
            print("run_tests.py: error: no document test matches %s" % " ".join(args.filters))
        else:
            print("run_tests.py: error: no test matches %s" % " ".join(args.filters))
        return 1
    if not (os.path.isfile(args.fort) and os.access(args.fort, os.X_OK)):
        sys.exit("run_tests.py: error: compiler not found: %s" % args.fort)
    fort = os.path.abspath(args.fort)
    std_dir = args.std_dir or os.path.join(os.path.dirname(fort), "std")
    config = Config(
        root=root,
        fort=fort,
        std_dir=os.path.abspath(std_dir),
        cc=args.cc,
        target=args.target,
        opt=args.opt,
        verify_ir=args.verify_ir,
        check_json=args.check_json,
        runner=args.runner.split(),
        timeout=args.timeout,
        keep=args.keep,
        verbose=args.verbose,
    )
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    counts = dict.fromkeys(VERDICTS, 0)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.j)) as pool:
        futures = [
            pool.submit(execute, config, test, listed(test, unsupported)) for test in selected
        ]
        for test, future in zip(selected, futures):
            result = future.result()
            if not args.check_json:
                # The cross-check is about the two forms of one run, not about
                # whether the compiler can pass the test yet, so xfail.txt does
                # not apply to it.
                result.verdict, result.reason = apply_expectations(
                    result.verdict, result.reason, listed(test, xfail), xfail_label
                )
            counts[result.verdict] += 1
            print(result.line())
            if (config.verbose and result.verdict != "PASS") or (config.keep and result.workdir):
                print(describe(result))
            sys.stdout.flush()
    print(
        "run_tests.py: %d tests: %d passed, %d failed, %d xfail, %d xpass, %d errors"
        % (
            len(selected),
            counts["PASS"],
            counts["FAIL"],
            counts["XFAIL"],
            counts["XPASS"],
            counts["ERROR"],
        )
    )
    return 1 if counts["FAIL"] or counts["XPASS"] or counts["ERROR"] else 0


if __name__ == "__main__":
    sys.exit(main())
