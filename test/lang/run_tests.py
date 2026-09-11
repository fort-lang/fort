#!/usr/bin/env python3
"""Run the fort language tests (notes/toolchain.md 7, decisions D14.4 and D14.5).

Every test is a `.ft` file whose expected behavior is encoded in `//!`
directives; a directory containing `main.ft` is one multi-file test. Each test
is compiled with `fort` from `test/lang` as the working directory, run in a
fresh temporary directory, and judged against its directives. `xfail.txt`
lists the tests the compiler cannot pass yet (a listed test that passes is an
XPASS and fails the run); `bootstrap-unsupported.txt` lists the tests that use
features the C bootstrap deliberately lacks, which must be rejected with the
bootstrap diagnostic. `--lint` validates the directives without a compiler and
`--verify-ir` runs the LLVM verifier over the module of every test that
compiles.

Standard library only; Python 3.12.
"""

import argparse
import concurrent.futures
import dataclasses
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
RUN_ONLY = ("args", "link", "stdin", "stdout", "exit", "abort")
FAIL_ONLY = ("error", "error-any")
# Directives that take no text, the ones that take text, and the ones that
# may be repeated (D14.5).
BLOCKS = ("stdin", "stdout")
WITH_TEXT = ("flags", "args", "link", "exit", "stderr", "error", "error-any")
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
VERDICTS = ("PASS", "FAIL", "XFAIL", "XPASS", "ERROR")

DIRECTIVE_RE = re.compile(r"^//! ([a-z][a-z-]*)(:(.*))?$")
ANNOTATION_RE = re.compile(r"^(.*?\S)\s*//! error:(.*)$")
DIAGNOSTIC_RE = re.compile(r"^(.+):(\d+):(\d+): error: (.*)$")
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
    stderr: list = dataclasses.field(default_factory=list)
    errors: list = dataclasses.field(default_factory=list)  # (file, line, substring)
    error_any: list = dataclasses.field(default_factory=list)
    problems: list = dataclasses.field(default_factory=list)

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
    file: str
    line: int
    column: int
    message: str


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
    if "exit" in seen and test.abort:
        problem(1, "'exit' and 'abort' are mutually exclusive")
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
                tests.append(Test(rel, rel + "/main.ft", expected_kind))
                for extra in sorted(entry.rglob("*")):
                    if extra.is_file() and extra.suffix != ".ft":
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
    tests.sort(key=lambda t: t.path)
    return tests, problems


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
    appends when a signal kills the program, so an `abort` test sees the same
    stderr under qemu as natively.
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
    if test.abort:
        if run_proc.returncode != -signal.SIGABRT:
            problems.append("expected SIGABRT, got %s" % _describe_status(run_proc.returncode))
    elif run_proc.returncode != test.exit:
        problems.append(
            "expected exit %d, got %s" % (test.exit, _describe_status(run_proc.returncode))
        )
    problems.extend(_stderr_problems(test.stderr, drop_qemu_notice(run_proc.stderr)))
    if problems:
        return "FAIL", "; ".join(problems)
    return "PASS", ""


def judge_unsupported(compile_proc, root=ROOT):
    """A bootstrap-unsupported test must be rejected with the bootstrap diagnostic."""
    verdict, reason = judge_compile(compile_proc, False)
    if verdict:
        return verdict, reason
    diagnostics = parse_diagnostics(compile_proc.stderr, root)
    if not diagnostics:
        return "FAIL", "compiler exited 1 without diagnostics"
    if not any(UNSUPPORTED_MESSAGE in d.message for d in diagnostics):
        return "FAIL", "no diagnostic containing '%s'" % UNSUPPORTED_MESSAGE
    return "PASS", ""


def apply_expectations(verdict, reason, xfail):
    """Map a verdict through xfail.txt: a listed failure is expected, a listed pass is not.

    ERROR is covered too: a listed test is one the compiler cannot handle yet,
    however it fails (the stage1 scaffold exits 2 on every input).
    """
    if not xfail:
        return verdict, reason
    if verdict == "PASS":
        return "XPASS", "listed in %s but passed" % XFAIL_NAME
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
    runner: list = dataclasses.field(default_factory=list)
    timeout: float = DEFAULT_TIMEOUT
    keep: bool = False
    verbose: bool = False


def child_env(workdir):
    env = dict(os.environ)
    env["TMPDIR"] = workdir
    env["LC_ALL"] = "C"
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


def link_command(config, test, prog, obj):
    """`<cc> --target=<triple> -o prog prog.o <link: files> fort_rt.o` for a test with helpers.

    `--cc` is a clang and names its target on the command line (D14.1, D14.3),
    so the harness spells the triple here exactly as the compiler does.
    """
    argv = [config.cc, "--target=" + config.target, "-o", prog, obj]
    argv.extend(str(config.root / link) for link in test.links)
    argv.append(os.path.join(config.std_dir, "fort_rt.o"))
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


def execute(config, test, unsupported):
    """Compile, link and run one test in a fresh temporary directory; return its Result.

    The compiler runs with the corpus root as working directory (D14.4) and
    `TMPDIR` pointed at the temporary directory; the program runs inside it.
    """
    if test.problems:
        return Result(test, "ERROR", "invalid directives: " + test.problems[0])
    workdir = tempfile.mkdtemp(prefix="fort-lang-")
    env = child_env(workdir)
    procs = []
    try:
        prog = os.path.join(workdir, "prog")
        if unsupported or test.kind == "fail":
            proc = run_process(
                compile_command(config, test, prog), config.root, env, config.timeout
            )
            procs.append(proc)
            if unsupported:
                verdict, reason = judge_unsupported(proc, config.root)
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
    selected = select(tests, args.filters)
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
            result.verdict, result.reason = apply_expectations(
                result.verdict, result.reason, listed(test, xfail)
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
