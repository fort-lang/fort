#!/usr/bin/env python3
"""Run the FIR tests under test/fir.

Every test is a `.fir` file: a header of `//!` directives and `//|` lines,
then a function in the textual form of spec/fir.md 13. The harness runs
`fort --fir-test <file>` from the corpus root. The compiler checks the
prelude, parses the text, runs the passes that the `//! pass:` directives
name, in order, and writes the module. The harness compares the result with
the expectation of the test. `--bless` writes the `//! expect:` block of each
selected test from the compiler's output.

Standard library only; Python 3.12.
"""

import argparse
import difflib
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent

# The directives: the ones that start a block of `//|` lines, the ones that
# take text, and the ones that state the outcome of a test.
BLOCKS = ("prelude", "expect")
WITH_TEXT = ("pass", "error", "panic")
REPEATABLE = ("error", "pass")
OUTCOMES = ("expect", "error", "panic")
HEADER_MARKERS = ("//!", "//|")
DIRECTIVE_RE = re.compile(r"^//! ([a-z]+)(:)?(.*)$")
DEFAULT_TIMEOUT = 60.0
# The status of a process that SIGABRT ended, which is how a verifier panic ends.
SIGABRT_STATUS = -6


class Test:
    """One `.fir` file and what its header expects."""

    def __init__(self, rel):
        self.rel = rel
        self.lines = []
        self.header_end = 0
        self.expect = None
        self.expect_line = 0
        self.errors = []
        self.panic = None
        self.problems = []


def parse_header(test, text):
    """Fill `test` from the header of `text` and record each problem of it."""
    test.lines = text.split("\n")
    lines = test.lines
    end = 0
    while end < len(lines) and lines[end][:3] in HEADER_MARKERS:
        end += 1
    test.header_end = end

    def problem(lineno, message):
        test.problems.append("%s:%d: %s" % (test.rel, lineno, message))

    seen = {}
    block = None
    blocks = {}
    for lineno in range(1, end + 1):
        line = lines[lineno - 1]
        if line.startswith("//|"):
            if len(line) > 3 and line[3] != " ":
                problem(lineno, "'//|' must be followed by a space")
            elif block is None:
                problem(lineno, "'//|' line outside a 'prelude:' or 'expect:' block")
            else:
                blocks[block].append(line[4:])
            continue
        block = None
        match = DIRECTIVE_RE.match(line)
        if not match:
            problem(lineno, "malformed directive '%s'" % line)
            continue
        name, has_colon, value = match.group(1), match.group(2), match.group(3).strip()
        if name in seen and name not in REPEATABLE:
            problem(lineno, "duplicate '%s' directive" % name)
        elif name in BLOCKS:
            if not has_colon or value:
                problem(lineno, "'%s:' takes no text on its line" % name)
            else:
                block = name
                blocks[name] = []
                if name == "expect":
                    test.expect_line = lineno
        elif name in WITH_TEXT:
            if not has_colon or not value:
                problem(lineno, "'%s:' needs text" % name)
            elif name == "error":
                test.errors.append(value)
            elif name == "panic":
                test.panic = value
        else:
            problem(lineno, "unknown directive '%s'" % name)
        seen[name] = lineno
    if "pass" not in seen:
        problem(1, "the test has no 'pass:' directive")
    stated = [name for name in OUTCOMES if name in seen]
    if len(stated) != 1:
        problem(1, "the test states one of 'expect:', 'error:' and 'panic:'")
    if "expect" in blocks:
        test.expect = "".join(line + "\n" for line in blocks["expect"])


def discover(root, filters):
    """The tests under `root` whose path holds one of `filters`, in path order."""
    tests = []
    for path in sorted(root.rglob("*.fir")):
        rel = path.relative_to(root).as_posix()
        if filters and not any(f in rel for f in filters):
            continue
        test = Test(rel)
        parse_header(test, path.read_text())
        tests.append(test)
    return tests


def run_fort(args, test):
    """Run `fort --fir-test` over the test; return its status, stdout and stderr."""
    argv = [args.fort, "--fir-test", test.rel]
    if args.std_dir:
        argv[1:1] = ["--std-dir", args.std_dir]
    try:
        done = subprocess.run(
            argv, cwd=args.root, capture_output=True, text=True, timeout=args.timeout
        )
    except subprocess.TimeoutExpired:
        return None, "", "timed out after %g s" % args.timeout
    except OSError as e:
        return None, "", "%s: %s" % (argv[0], e)
    return done.returncode, done.stdout, done.stderr


def judge(test, status, stdout, stderr):
    """`(verdict, detail)`: PASS, FAIL with the reason, or ERROR for a harness error.

    Exit 2 is a usage, toolchain or internal error of the compiler, and a pass
    that the compiler does not know is one; the harness reports it as an ERROR.
    """
    if status is None or status == 2:
        return "ERROR", stderr.strip() or "exit %s" % status
    if test.panic is not None:
        if status != SIGABRT_STATUS:
            return "FAIL", "expected a panic, got exit %d" % status
        if test.panic not in stderr:
            return "FAIL", "stderr does not hold '%s'" % test.panic
        return "PASS", ""
    if status < 0:
        return "FAIL", "the compiler died by signal %d\n%s" % (-status, stderr)
    if test.errors:
        if status != 1:
            return "FAIL", "expected exit 1, got exit %d" % status
        missing = [e for e in test.errors if e not in stderr]
        if missing:
            return "FAIL", "stderr does not hold '%s'\n%s" % (missing[0], stderr)
        return "PASS", ""
    if status != 0:
        return "FAIL", "expected exit 0, got exit %d\n%s" % (status, stderr)
    if stdout != test.expect:
        diff = difflib.unified_diff(
            test.expect.splitlines(keepends=True),
            stdout.splitlines(keepends=True),
            fromfile="expected",
            tofile="actual",
        )
        return "FAIL", "".join(diff)
    return "PASS", ""


def bless(root, test, stdout):
    """Replace the `//! expect:` block of the test with `stdout`."""
    lines = test.lines
    start = test.expect_line
    end = start
    while end < test.header_end and lines[end].startswith("//|"):
        end += 1
    block = ["//|" if line == "" else "//| " + line for line in stdout.split("\n")[:-1]]
    lines[start:end] = block
    (root / test.rel).write_text("\n".join(lines))


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "filters", nargs="*", help="run only the tests whose path contains a filter"
    )
    parser.add_argument(
        "--fort",
        default=os.environ.get(
            "FORT", str(ROOT.parent.parent / "build" / platform.system() / "debug" / "fort")
        ),
        help="the compiler (default: $FORT or build/<Host>/debug/fort)",
    )
    parser.add_argument(
        "--std-dir",
        default=os.environ.get("FORT_STD_DIR"),
        help="passed as --std-dir (default: $FORT_STD_DIR or std beside the compiler)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help="seconds per test (default: %(default)s)",
    )
    parser.add_argument(
        "--bless",
        action="store_true",
        help="write the 'expect:' block of each selected test from the compiler's output",
    )
    parser.add_argument("--list", action="store_true", help="list the selected tests and exit")
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="show the output of each failure"
    )
    parser.add_argument("--root", default=str(ROOT), help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    args.fort = os.path.abspath(args.fort)
    if args.std_dir:
        args.std_dir = os.path.abspath(args.std_dir)
    return args


def main(argv=None):
    args = parse_args(argv)
    root = Path(args.root).resolve()
    args.root = str(root)
    tests = discover(root, args.filters)
    if not tests:
        print("run_tests.py: no test is selected")
        return 1
    if args.list:
        for test in tests:
            print(test.rel)
        return 0
    problems = [p for test in tests for p in test.problems]
    if problems:
        for p in problems:
            print(p)
        print("run_tests.py: %d problem(s) in the directives" % len(problems))
        return 1
    counts = {"PASS": 0, "FAIL": 0, "ERROR": 0}
    for test in tests:
        status, stdout, stderr = run_fort(args, test)
        if args.bless and test.expect is not None and status == 0:
            bless(root, test, stdout)
            test.expect = stdout
        verdict, detail = judge(test, status, stdout, stderr)
        counts[verdict] += 1
        if verdict != "PASS":
            print("%s %s" % (verdict, test.rel))
            if detail:
                print(detail.rstrip("\n"))
        elif args.verbose:
            print("PASS %s" % test.rel)
    print(
        "run_tests.py: %d tests: %d passed, %d failed, %d errors"
        % (len(tests), counts["PASS"], counts["FAIL"], counts["ERROR"])
    )
    return 0 if counts["PASS"] == len(tests) else 1


if __name__ == "__main__":
    sys.exit(main())
