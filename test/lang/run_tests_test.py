#!/usr/bin/env python3
"""Unit tests of run_tests.py: directive parsing, lint, judging and an end-to-end run.

The end-to-end tests use a fake `fort` whose behavior is scripted by `//@`
lines in the test files it is given (they are ordinary comments to the
harness), and a fake `cc` that turns `prog.o` into `prog`. Run with
`python3 -m unittest run_tests_test` from this directory.
"""

import contextlib
import io
import os
import shutil
import signal
import stat
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_tests  # noqa: E402

FAKE_FORT = r'''#!/usr/bin/env python3
"""A stand-in compiler: the entry file's //@ lines script its behavior.

//@ exit N          exit with status N (default 0; the program is written only on 0)
//@ stderr TEXT     print TEXT on stderr (a diagnostic, for example)
//@ program LINE    a line of the /bin/sh program written to -o
//@ flag FLAG       exit 2 unless FLAG is on the command line
//@ hang            sleep, to exercise the timeout
"""
import os
import sys
import time

args = sys.argv[1:]
out, entry, i = "a.out", None, 0
while i < len(args):
    a = args[i]
    if a in ("-o", "--cc", "--std-dir", "-I"):
        if a == "-o":
            out = args[i + 1]
        i += 2
        continue
    if not a.startswith("-"):
        entry = a
    i += 1
status, program = 0, ["#!/bin/sh"]
with open(entry) as f:
    for line in f:
        line = line.rstrip("\n")
        if line.startswith("//@ exit "):
            status = int(line[9:])
        elif line.startswith("//@ stderr "):
            sys.stderr.write(line[11:] + "\n")
        elif line.startswith("//@ program "):
            program.append(line[12:])
        elif line.startswith("//@ flag ") and line[9:] not in args:
            sys.stderr.write("fort: error: missing flag %s\n" % line[9:])
            sys.exit(2)
        elif line.startswith("//@ hang"):
            time.sleep(30)
if status == 0:
    with open(out, "w") as f:
        f.write("\n".join(program) + "\n")
    os.chmod(out, 0o755)
sys.exit(status)
'''

FAKE_CC = r'''#!/usr/bin/env python3
"""A stand-in linker: copies the object (a shell script) to -o and logs the other inputs."""
import os
import shutil
import sys

args = sys.argv[1:]
out = args[args.index("-o") + 1]
inputs = [a for i, a in enumerate(args) if not a.startswith("-") and args[i - 1] != "-o"]
shutil.copy(inputs[0], out)
with open(out, "a") as f:
    f.write("echo linked %s\n" % " ".join(os.path.basename(p) for p in inputs[1:]))
os.chmod(out, 0o755)
'''


def write(root, rel, text):
    path = Path(root) / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(text))
    return path


def executable(root, rel, text):
    path = write(root, rel, text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


def parse(root, entry, text, expected_kind="run", path=None):
    test = run_tests.Test(path or entry, entry, expected_kind)
    run_tests.parse_main(Path(root), test, textwrap.dedent(text).lstrip("\n"))
    return test


class TempRoot(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="fort-run-tests-"))
        self.addCleanup(shutil.rmtree, self.root, True)


# ---- directive parsing ----------------------------------------------------------------


class ParseDirectives(TempRoot):
    def test_run_directives(self):
        write(self.root, "ffi/helpers.c", "int x;\n")
        test = parse(
            self.root,
            "run/ffi/001_all.ft",
            """\
            //! run
            //! flags: --release --no-bounds-check
            //! args: a b  c
            //! link: ffi/helpers.c
            //! stdin:
            //< hello
            //<
            //! stdout:
            //| one
            //|
            //| two
            //! exit: 3
            //! stderr: first
            //! stderr: second
            fn i32 main() { return 3; }
            """,
        )
        self.assertEqual(test.problems, [])
        self.assertEqual(test.kind, "run")
        self.assertEqual(test.flags, ["--release", "--no-bounds-check"])
        self.assertEqual(test.args, ["a", "b", "c"])
        self.assertEqual(test.links, ["ffi/helpers.c"])
        self.assertEqual(test.stdin, b"hello\n\n")
        self.assertEqual(test.stdout, b"one\n\ntwo\n")
        self.assertEqual(test.exit, 3)
        self.assertFalse(test.abort)
        self.assertEqual(test.stderr, ["first", "second"])
        self.assertEqual(test.errors, [])

    def test_defaults(self):
        test = parse(self.root, "run/control/001_min.ft", "//! run\nfn i32 main() { return 0; }\n")
        self.assertEqual(test.problems, [])
        self.assertEqual((test.stdin, test.stdout, test.exit, test.abort), (b"", b"", 0, False))
        self.assertEqual((test.flags, test.args, test.links, test.stderr), ([], [], [], []))

    def test_abort(self):
        test = parse(
            self.root,
            "run/errors/001_abort.ft",
            """\
            //! run
            //! abort
            //! stderr: runtime error: index 5 out of range for length 3
            //! stdout:
            //| before
            fn i32 main() { return 0; }
            """,
        )
        self.assertEqual(test.problems, [])
        self.assertTrue(test.abort)
        self.assertEqual(test.stdout, b"before\n")

    def test_fail_annotations(self):
        test = parse(
            self.root,
            "fail/mutability/001_x.ft",
            """\
            //! fail
            //! error-any: circular
            //! stderr: something
            fn i32 main() {
                i32 x = 1;
                x = 2; //! error: immutable
                y = 3;//! error:   assign
                return x;
            }
            """,
            expected_kind="fail",
        )
        self.assertEqual(test.problems, [])
        self.assertEqual(test.kind, "fail")
        self.assertEqual(test.error_any, ["circular"])
        self.assertEqual(test.stderr, ["something"])
        self.assertEqual(
            test.errors,
            [
                ("fail/mutability/001_x.ft", 6, "immutable"),
                ("fail/mutability/001_x.ft", 7, "assign"),
            ],
        )

    def test_trailing_spaces_kept(self):
        test = parse(
            self.root, "run/control/001_x.ft", "//! run\n//! stdout:\n//| a  \n//| \n//|\n"
        )
        self.assertEqual(test.problems, [])
        self.assertEqual(test.stdout, b"a  \n\n\n")

    def test_header_only_file(self):
        test = parse(self.root, "run/control/001_x.ft", "//! run\n//! stdout:\n//| a\n")
        self.assertEqual(test.problems, [])
        self.assertEqual(test.stdout, b"a\n")

    def problems_of(self, text, entry="run/control/001_x.ft", expected_kind="run"):
        return parse(self.root, entry, text, expected_kind).problems

    def test_first_line(self):
        self.assertEqual(
            self.problems_of("fn i32 main() {}\n"),
            ["run/control/001_x.ft:1: first line must be '//! run' or '//! fail'"],
        )
        self.assertEqual(
            self.problems_of("//! stdout:\n//! run\n"),
            ["run/control/001_x.ft:1: first line must be '//! run' or '//! fail'"],
        )
        self.assertEqual(
            self.problems_of(""),
            ["run/control/001_x.ft:1: first line must be '//! run' or '//! fail'"],
        )
        self.assertEqual(
            self.problems_of("//! run \n"),
            ["run/control/001_x.ft:1: first line must be '//! run' or '//! fail'"],
        )

    def test_kind_matches_directory(self):
        self.assertIn(
            "fail/control/001_x.ft:1: 'run' test in a fail directory",
            self.problems_of("//! run\n", "fail/control/001_x.ft", "fail"),
        )

    def test_unknown_and_malformed(self):
        self.assertEqual(
            self.problems_of("//! run\n//! bogus: x\n"),
            ["run/control/001_x.ft:2: unknown directive 'bogus'"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//!stdout:\n"),
            ["run/control/001_x.ft:2: malformed directive '//!stdout:'"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! run\n"),
            ["run/control/001_x.ft:2: 'run' is only allowed on the first line"],
        )

    def test_exit_and_abort(self):
        self.assertEqual(
            self.problems_of("//! run\n//! exit: 1\n//! abort\n"),
            ["run/control/001_x.ft:1: 'exit' and 'abort' are mutually exclusive"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! exit: 256\n"),
            ["run/control/001_x.ft:2: exit: expected a status between 0 and 255"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! exit: -1\n"),
            ["run/control/001_x.ft:2: exit: expected a status between 0 and 255"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! abort: yes\n"),
            ["run/control/001_x.ft:2: 'abort' takes no text"],
        )

    def test_kind_restrictions(self):
        problems = self.problems_of(
            "//! fail\n//! stdout:\n//! args: x\n//! exit: 1\n//! abort\n//! link: a\n//! stdin:\n",
            "fail/control/001_x.ft",
            "fail",
        )
        self.assertEqual(
            problems,
            [
                "fail/control/001_x.ft:2: 'stdout' is only allowed in run tests",
                "fail/control/001_x.ft:3: 'args' is only allowed in run tests",
                "fail/control/001_x.ft:4: 'exit' is only allowed in run tests",
                "fail/control/001_x.ft:5: 'abort' is only allowed in run tests",
                "fail/control/001_x.ft:6: 'link' is only allowed in run tests",
                "fail/control/001_x.ft:7: 'stdin' is only allowed in run tests",
            ],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! error-any: x\nx = 1; //! error: y\n"),
            [
                "run/control/001_x.ft:2: 'error-any' is only allowed in fail tests",
                "run/control/001_x.ft:3: 'error' is only allowed in fail tests",
            ],
        )

    def test_missing_link_file(self):
        self.assertEqual(
            self.problems_of("//! run\n//! link: ffi/nothere.c\n"),
            ["run/control/001_x.ft:2: link: file not found: ffi/nothere.c"],
        )

    def test_output_lines(self):
        self.assertEqual(
            self.problems_of("//! run\n//! stdout:\n//|x\n"),
            ["run/control/001_x.ft:3: '//|' must be followed by a space"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stdin:\n//<x\n"),
            ["run/control/001_x.ft:3: '//<' must be followed by a space"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//| x\n"),
            ["run/control/001_x.ft:2: '//|' line outside a 'stdout:' block"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stdout:\n//< x\n"),
            ["run/control/001_x.ft:3: '//<' line outside a 'stdin:' block"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stdout:\n//| a\n//! exit: 1\n//| b\n"),
            ["run/control/001_x.ft:5: '//|' line outside a 'stdout:' block"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stdout: x\n"),
            ["run/control/001_x.ft:2: 'stdout:' takes no text on its line"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stdout\n"),
            ["run/control/001_x.ft:2: 'stdout:' takes no text on its line"],
        )

    def test_duplicates_and_empty_text(self):
        self.assertEqual(
            self.problems_of("//! run\n//! stdout:\n//! stdout:\n"),
            ["run/control/001_x.ft:3: duplicate 'stdout:' directive"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! exit: 1\n//! exit: 2\n"),
            ["run/control/001_x.ft:3: duplicate 'exit:' directive"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stderr:\n"),
            ["run/control/001_x.ft:2: 'stderr:' needs text"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! flags\n"),
            ["run/control/001_x.ft:2: 'flags:' needs text"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! stderr: a\n//! stderr: b\n//! link: x\n//! link: x\n"),
            [
                "run/control/001_x.ft:4: link: file not found: x",
                "run/control/001_x.ft:5: link: file not found: x",
            ],
        )

    def test_body_problems(self):
        problems = self.problems_of(
            "//! fail\nfn f() {\n//! stdout:\n    x; //! stderr: y\n    z; //! error:\n"
            "    w; //! error: a //! error: b\n    //! error: q\n//| out\n}\n",
            "fail/control/001_x.ft",
            "fail",
        )
        self.assertEqual(
            problems,
            [
                "fail/control/001_x.ft:3: directive after the header",
                "fail/control/001_x.ft:4: only '//! error:' may follow code on a line",
                "fail/control/001_x.ft:5: 'error:' needs text",
                "fail/control/001_x.ft:6: more than one annotation on the line",
                "fail/control/001_x.ft:7: directive after the header",
                "fail/control/001_x.ft:8: '//|' line outside the header",
            ],
        )
        self.assertEqual(
            self.problems_of("//! fail\n//! error: x\n", "fail/control/001_x.ft", "fail"),
            [
                "fail/control/001_x.ft:2: 'error' annotates a code line and cannot appear in "
                "the header"
            ],
        )

    def test_multi_file_siblings(self):
        write(
            self.root, "fail/modules/001_m/main.ft", "//! fail\nimport other;\nfn i32 main() {}\n"
        )
        write(
            self.root,
            "fail/modules/001_m/other.ft",
            "//! stdout:\nfn i32 f() {\n    return x; //! error: undeclared\n}\n",
        )
        test = run_tests.Test("fail/modules/001_m", "fail/modules/001_m/main.ft", "fail")
        run_tests.load_test(self.root, test)
        self.assertTrue(test.multi)
        self.assertEqual(
            test.problems, ["fail/modules/001_m/other.ft:1: directives belong in main.ft only"]
        )
        self.assertEqual(test.errors, [("fail/modules/001_m/other.ft", 3, "undeclared")])

    def test_fail_test_needs_an_annotation(self):
        write(self.root, "fail/casts/001_bare.ft", "//! fail\nfn i32 main() {}\n")
        test = run_tests.Test("fail/casts/001_bare.ft", "fail/casts/001_bare.ft", "fail")
        run_tests.load_test(self.root, test)
        self.assertEqual(
            test.problems, ["fail/casts/001_bare.ft:1: fail test without an 'error' or 'error-any'"]
        )
        write(self.root, "fail/casts/002_any.ft", "//! fail\n//! error-any: nope\n")
        test = run_tests.Test("fail/casts/002_any.ft", "fail/casts/002_any.ft", "fail")
        self.assertEqual(run_tests.load_test(self.root, test).problems, [])
        write(self.root, "fail/modules/001_m/main.ft", "//! fail\nimport other;\n")
        write(self.root, "fail/modules/001_m/other.ft", "i32 x = y; //! error: undeclared\n")
        test = run_tests.Test("fail/modules/001_m", "fail/modules/001_m/main.ft", "fail")
        self.assertEqual(run_tests.load_test(self.root, test).problems, [])
        write(self.root, "run/casts/001_run.ft", "//! run\n")
        test = run_tests.Test("run/casts/001_run.ft", "run/casts/001_run.ft", "run")
        self.assertEqual(run_tests.load_test(self.root, test).problems, [])


# ---- discovery and lint ----------------------------------------------------------------


class Discovery(TempRoot):
    def test_layout(self):
        write(self.root, "run/arrays/001_a.ft", "//! run\n")
        write(self.root, "run/arrays/002_b.ft", "//! run\n")
        write(self.root, "run/modules/two/main.ft", "//! run\n")
        write(self.root, "run/modules/two/util.ft", "fn f() {}\n")
        write(self.root, "run/modules/two/sub/deep.ft", "fn g() {}\n")
        write(self.root, "fail/modules/001_c/main.ft", "//! fail\n")
        write(self.root, "programs/big.ft", "//! run\n")
        write(self.root, "README.md", "ignored\n")
        tests, problems = run_tests.discover(self.root)
        self.assertEqual(problems, [])
        self.assertEqual(
            [(t.path, t.entry, t.expected_kind, t.multi) for t in tests],
            [
                ("fail/modules/001_c", "fail/modules/001_c/main.ft", "fail", True),
                ("programs/big.ft", "programs/big.ft", "run", False),
                ("run/arrays/001_a.ft", "run/arrays/001_a.ft", "run", False),
                ("run/arrays/002_b.ft", "run/arrays/002_b.ft", "run", False),
                ("run/modules/two", "run/modules/two/main.ft", "run", True),
            ],
        )

    def test_layout_problems(self):
        write(self.root, "run/arrays/001_a.ft", "//! run\n")
        write(self.root, "run/arrays/003_c.ft", "//! run\n")
        write(self.root, "run/arrays/notes.txt", "")
        write(self.root, "run/arrays/unnumbered.ft", "//! run\n")
        write(self.root, "run/arrays/002_dir/main.ft", "//! run\n")
        write(self.root, "run/blobs/001_a.ft", "//! run\n")
        write(self.root, "run/stray.ft", "//! run\n")
        write(self.root, "fail/casts/001_a.ft", "//! fail\n")
        write(self.root, "fail/casts/001_b.ft", "//! fail\n")
        write(self.root, "fail/modules/001_m/main.ft", "//! fail\n")
        write(self.root, "fail/modules/001_m/helper.c", "")
        write(self.root, "fail/modules/Bad_Name/main.ft", "//! fail\n")
        write(self.root, "fail/modules/nomain/x.ft", "//! fail\n")
        write(self.root, "programs/Bad.ft", "//! run\n")
        write(self.root, "programs/nested/main.ft", "//! run\n")
        _, problems = run_tests.discover(self.root)
        self.assertEqual(
            problems,
            [
                "run/arrays/002_dir: directory test outside modules",
                "run/arrays/notes.txt: unexpected file",
                "run/arrays/unnumbered.ft: bad test name",
                "run/arrays: gap in numbering: expected 002, found 003",
                "run/blobs: unknown area 'blobs'",
                "run/stray.ft: unexpected file",
                "fail/casts: duplicate number 001: 001_a, 001_b",
                "fail/modules/001_m/helper.c: unexpected file",
                "fail/modules/Bad_Name: bad test name",
                "fail/modules/nomain: directory without main.ft",
                "programs/Bad.ft: bad test name",
                "programs/nested: directory test outside modules",
            ],
        )

    def test_numbering(self):
        self.assertEqual(run_tests.numbering_problems("d", ["001_a", "002_b", "plain"]), [])
        self.assertEqual(
            run_tests.numbering_problems("d", ["002_b"]),
            ["d: gap in numbering: expected 001, found 002"],
        )
        self.assertEqual(
            run_tests.numbering_problems("d", ["001_a", "001_b", "003_c"]),
            [
                "d: duplicate number 001: 001_a, 001_b",
                "d: gap in numbering: expected 002, found 003",
            ],
        )

    def test_expectations(self):
        path = write(
            self.root, "xfail.txt", "# comment\n\nrun/arrays/  # trailing\nfail/casts/001_a.ft\n"
        )
        prefixes = run_tests.load_expectations(path)
        self.assertEqual(prefixes, ["run/arrays/", "fail/casts/001_a.ft"])
        self.assertEqual(run_tests.load_expectations(None), [])
        tests = [run_tests.Test("run/arrays/001_a.ft", "run/arrays/001_a.ft", "run")]
        self.assertTrue(run_tests.listed(tests[0], prefixes))
        self.assertFalse(run_tests.listed(tests[0], ["run/arrays/002"]))
        self.assertEqual(
            run_tests.expectation_problems("xfail.txt", prefixes, tests),
            ["xfail.txt: 'fail/casts/001_a.ft' matches no test"],
        )
        self.assertEqual(
            run_tests.expectation_problems("/tmp/other.txt", ["run/arrays/002"], tests),
            ["/tmp/other.txt: 'run/arrays/002' matches no test"],
        )

    def test_select(self):
        tests = [
            run_tests.Test(p, p, "run")
            for p in ("run/arrays/001_a.ft", "run/slices/001_a.ft", "programs/p.ft")
        ]
        self.assertEqual(run_tests.select(tests, []), tests)
        self.assertEqual(
            [t.path for t in run_tests.select(tests, ["arrays"])], ["run/arrays/001_a.ft"]
        )
        self.assertEqual(
            [t.path for t in run_tests.select(tests, ["001_a", "programs/"])],
            ["run/arrays/001_a.ft", "run/slices/001_a.ft", "programs/p.ft"],
        )


# ---- judging ------------------------------------------------------------------------


def proc(returncode, stdout=b"", stderr=b"", timed_out=False, started=True):
    return run_tests.Proc(["fort"], returncode, stdout, stderr, timed_out, started)


class Judging(unittest.TestCase):
    def setUp(self):
        self.run_test = run_tests.Test(
            "run/control/001_x.ft", "run/control/001_x.ft", "run", kind="run"
        )
        self.fail_test = run_tests.Test(
            "fail/control/001_x.ft", "fail/control/001_x.ft", "fail", kind="fail"
        )

    def test_judge_compile(self):
        self.assertEqual(run_tests.judge_compile(proc(0), True), (None, ""))
        self.assertEqual(run_tests.judge_compile(proc(1), False), (None, ""))
        self.assertEqual(
            run_tests.judge_compile(proc(0), False),
            ("FAIL", "compiled successfully, expected exit 1"),
        )
        self.assertEqual(
            run_tests.judge_compile(proc(1, stderr=b"a.ft:1:1: error: boom\nmore\n"), True),
            ("FAIL", "compiler exited 1: a.ft:1:1: error: boom"),
        )
        self.assertEqual(run_tests.judge_compile(proc(1), True), ("FAIL", "compiler exited 1"))
        self.assertEqual(
            run_tests.judge_compile(proc(2, stderr=b"fort: error: not implemented\n"), True),
            ("ERROR", "compiler exited 2: fort: error: not implemented"),
        )
        self.assertEqual(run_tests.judge_compile(proc(2), False)[0], "ERROR")
        self.assertEqual(
            run_tests.judge_compile(proc(-signal.SIGSEGV), True),
            ("ERROR", "compiler killed by signal 11"),
        )
        self.assertEqual(
            run_tests.judge_compile(proc(-1, timed_out=True), True), ("ERROR", "compiler timed out")
        )

    def test_parse_diagnostics(self):
        stderr = (
            b"fail/x.ft:3:5: error: immutable\n"
            b"note: declared here\n"
            b"fail/x.ft:4:1: warning: nope\n"
            b"./fail/x.ft:10:2: error: with: colons\n"
        )
        diagnostics = run_tests.parse_diagnostics(stderr, Path("/nonexistent"))
        self.assertEqual(
            [(d.file, d.line, d.column, d.message) for d in diagnostics],
            [("fail/x.ft", 3, 5, "immutable"), ("fail/x.ft", 10, 2, "with: colons")],
        )

    def test_judge_fail_pass(self):
        self.fail_test.errors = [("fail/control/001_x.ft", 4, "immutable")]
        self.fail_test.error_any = ["circular"]
        self.fail_test.stderr = ["immutable"]
        stderr = (
            b"fail/control/001_x.ft:4:5: error: cannot assign, immutable\n"
            b"fail/control/001_x.ft:1:1: error: circular import\n"
        )
        self.assertEqual(run_tests.judge_fail(self.fail_test, proc(1, stderr=stderr)), ("PASS", ""))

    def test_judge_fail_problems(self):
        self.fail_test.errors = [("fail/control/001_x.ft", 4, "immutable")]
        self.assertEqual(
            run_tests.judge_fail(self.fail_test, proc(1)),
            ("FAIL", "compiler exited 1 without diagnostics"),
        )
        self.assertEqual(
            run_tests.judge_fail(
                self.fail_test, proc(1, stderr=b"fail/control/001_x.ft:9:1: error: x\n")
            ),
            (
                "FAIL",
                "no diagnostic on fail/control/001_x.ft:4 (expected 'immutable'); "
                "unannotated diagnostic fail/control/001_x.ft:9: x",
            ),
        )
        self.assertEqual(
            run_tests.judge_fail(
                self.fail_test, proc(1, stderr=b"fail/control/001_x.ft:4:1: error: other\n")
            ),
            ("FAIL", "diagnostic on fail/control/001_x.ft:4 lacks 'immutable': other"),
        )
        self.assertEqual(
            run_tests.judge_fail(
                self.fail_test, proc(1, stderr=b"other.ft:4:1: error: immutable\n")
            ),
            (
                "FAIL",
                "no diagnostic on fail/control/001_x.ft:4 (expected 'immutable'); "
                "unannotated diagnostic other.ft:4: immutable",
            ),
        )
        self.fail_test.error_any = ["circular"]
        self.fail_test.stderr = ["fatal"]
        self.assertEqual(
            run_tests.judge_fail(
                self.fail_test, proc(1, stderr=b"fail/control/001_x.ft:4:1: error: immutable\n")
            ),
            ("FAIL", "no diagnostic containing 'circular'; stderr lacks 'fatal'"),
        )
        self.assertEqual(run_tests.judge_fail(self.fail_test, proc(0))[0], "FAIL")
        self.assertEqual(run_tests.judge_fail(self.fail_test, proc(2))[0], "ERROR")

    def test_judge_fail_extra_diagnostic_on_annotated_line(self):
        self.fail_test.errors = [("fail/control/001_x.ft", 4, "immutable")]
        stderr = (
            b"fail/control/001_x.ft:4:1: error: immutable\nfail/control/001_x.ft:4:9: error: also\n"
        )
        self.assertEqual(run_tests.judge_fail(self.fail_test, proc(1, stderr=stderr)), ("PASS", ""))

    def test_judge_run_pass(self):
        self.run_test.stdout = b"a\nb\n"
        self.run_test.exit = 3
        self.run_test.stderr = ["warn"]
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(3, b"a\nb\n", b"a warning\n")),
            ("PASS", ""),
        )
        self.run_test.stderr = []
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), proc(0), proc(3, b"a\nb\n")), ("PASS", "")
        )

    def test_judge_run_stdout(self):
        self.run_test.stdout = b"a\nb\n"
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(0, b"a\nc\n")),
            ("FAIL", "stdout line 2: expected 'b', got 'c'"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(0, b"a\n")),
            ("FAIL", "stdout ends after line 1, expected 'b'"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(0, b"a\nb\nc\n")),
            ("FAIL", "stdout line 3 unexpected: 'c'"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(0, b"a\nb")),
            ("FAIL", "stdout lacks the final newline"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(0, b"a\nb \n")),
            ("FAIL", "stdout line 2: expected 'b', got 'b '"),
        )

    def test_judge_run_status(self):
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(1)),
            ("FAIL", "expected exit 0, got exit 1"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGABRT)),
            ("FAIL", "expected exit 0, got signal 6"),
        )
        self.run_test.abort = True
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGABRT)), ("PASS", "")
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(134)),
            ("FAIL", "expected SIGABRT, got exit 134"),
        )
        self.run_test.abort = False
        self.run_test.stdout = b"x\n"
        self.run_test.stderr = ["boom"]
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(2, b"y\n")),
            (
                "FAIL",
                "stdout line 1: expected 'x', got 'y'; expected exit 0, got exit 2; "
                "stderr lacks 'boom'",
            ),
        )

    def test_drop_qemu_notice(self):
        notice = b"qemu: uncaught target signal 6 (Abort) - core dumped\n"
        self.assertEqual(run_tests.drop_qemu_notice(b""), b"")
        self.assertEqual(run_tests.drop_qemu_notice(notice), b"")
        self.assertEqual(
            run_tests.drop_qemu_notice(b"x.ft:3:1: panic: boom\n" + notice),
            b"x.ft:3:1: panic: boom\n",
        )
        self.assertEqual(run_tests.drop_qemu_notice(notice + b"after\n"), b"after\n")
        self.assertEqual(
            run_tests.drop_qemu_notice(b"qemu: something else\n"), b"qemu: something else\n"
        )
        self.assertEqual(run_tests.drop_qemu_notice(b" " + notice), b" " + notice)

    def test_judge_run_qemu_notice_is_not_stderr(self):
        notice = b"qemu: uncaught target signal 6 (Abort) - core dumped\n"
        self.run_test.abort = True
        self.run_test.stderr = ["core dumped"]
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGABRT, b"", notice)),
            ("FAIL", "stderr lacks 'core dumped'"),
        )
        self.run_test.stderr = ["panic: boom"]
        self.assertEqual(
            run_tests.judge_run(
                self.run_test,
                proc(0),
                None,
                proc(-signal.SIGABRT, b"", b"x.ft:3:1: panic: boom\n" + notice),
            ),
            ("PASS", ""),
        )

    def test_judge_run_steps(self):
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(1), None, None), ("FAIL", "compiler exited 1")
        )
        self.assertEqual(run_tests.judge_run(self.run_test, proc(2), None, None)[0], "ERROR")
        self.assertEqual(
            run_tests.judge_run(
                self.run_test, proc(0), proc(1, stderr=b"undefined reference\n"), None
            ),
            ("ERROR", "link failed: undefined reference"),
        )
        self.assertEqual(
            run_tests.judge_run(
                self.run_test,
                proc(0),
                None,
                proc(255, stderr=b"runner: No such file", started=False),
            ),
            ("ERROR", "program failed to start: runner: No such file"),
        )
        self.assertEqual(
            run_tests.judge_run(
                self.run_test,
                proc(255, stderr=b"fort: Permission denied", started=False),
                None,
                None,
            ),
            ("ERROR", "compiler failed to start: fort: Permission denied"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), proc(-1, timed_out=True), None),
            ("ERROR", "link timed out"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-1, timed_out=True)),
            ("FAIL", "program timed out"),
        )

    def test_judge_unsupported(self):
        message = run_tests.UNSUPPORTED_MESSAGE
        self.assertEqual(
            run_tests.judge_unsupported(
                proc(1, stderr=("a.ft:2:1: error: floats %s\n" % message).encode())
            ),
            ("PASS", ""),
        )
        self.assertEqual(
            run_tests.judge_unsupported(proc(1, stderr=b"a.ft:2:1: error: other\n")),
            ("FAIL", "no diagnostic containing '%s'" % message),
        )
        self.assertEqual(
            run_tests.judge_unsupported(proc(1, stderr=message.encode())),
            ("FAIL", "compiler exited 1 without diagnostics"),
        )
        self.assertEqual(
            run_tests.judge_unsupported(proc(0)), ("FAIL", "compiled successfully, expected exit 1")
        )
        self.assertEqual(run_tests.judge_unsupported(proc(2))[0], "ERROR")

    def test_apply_expectations(self):
        self.assertEqual(run_tests.apply_expectations("PASS", "", False), ("PASS", ""))
        self.assertEqual(run_tests.apply_expectations("FAIL", "why", False), ("FAIL", "why"))
        self.assertEqual(run_tests.apply_expectations("FAIL", "why", True), ("XFAIL", "why"))
        self.assertEqual(run_tests.apply_expectations("ERROR", "why", True), ("XFAIL", "why"))
        self.assertEqual(
            run_tests.apply_expectations("PASS", "", True),
            ("XPASS", "listed in xfail.txt but passed"),
        )


# ---- end to end with a fake compiler ---------------------------------------------------


class EndToEnd(TempRoot):
    def setUp(self):
        super().setUp()
        self.fort = executable(self.root, "bin/fort", FAKE_FORT)
        self.cc = executable(self.root, "bin/cc", FAKE_CC)
        self.corpus = self.root / "lang"
        write(self.corpus, "ffi/helpers.c", "int helper(void) { return 1; }\n")
        (self.corpus / "std").mkdir()
        self.options = [
            "--root",
            str(self.corpus),
            "--fort",
            str(self.fort),
            "--std-dir",
            str(self.corpus / "std"),
            "--cc",
            str(self.cc),
            "-j",
            "2",
            "--timeout",
            "5",
        ]

    def run_main(self, *extra):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = run_tests.main(self.options + list(extra))
        return status, out.getvalue().splitlines()

    def write_corpus(self):
        write(
            self.corpus,
            "run/control/001_echo.ft",
            """\
            //! run
            //! stdout:
            //| hello
            //|
            //| C tmp-ok core-0
            //@ program echo hello
            //@ program echo
            //@ program t=tmp-bad
            //@ program if [ "$(cd "$TMPDIR" && pwd -P)" = "$(pwd -P)" ]; then t=tmp-ok; fi
            //@ program echo "$LC_ALL $t core-$(ulimit -c)"
            """,
        )
        write(
            self.corpus,
            "run/control/002_abort.ft",
            """\
            //! run
            //! abort
            //! stderr: runtime error: index
            //! stdout:
            //| before
            //@ program echo before
            //@ program echo "x.ft:3:1: runtime error: index 5 out of range" >&2
            //@ program echo "qemu: uncaught target signal 6 (Abort) - core dumped" >&2
            //@ program kill -ABRT $$
            """,
        )
        write(
            self.corpus,
            "run/control/003_args_stdin_exit.ft",
            """\
            //! run
            //! flags: --release
            //! args: one two
            //! stdin:
            //< first line
            //< second
            //! stdout:
            //| 2 one two
            //| first line
            //| second
            //! exit: 3
            //@ flag --release
            //@ program echo "$# $1 $2"
            //@ program cat
            //@ program exit 3
            """,
        )
        write(
            self.corpus,
            "run/control/004_wrong_output.ft",
            """\
            //! run
            //! stdout:
            //| expected
            //@ program echo actual
            """,
        )
        write(
            self.corpus,
            "run/control/005_not_implemented.ft",
            """\
            //! run
            //@ exit 2
            //@ stderr fort: error: not implemented
            """,
        )
        write(
            self.corpus,
            "run/control/006_no_abort.ft",
            """\
            //! run
            //! abort
            //@ program exit 0
            """,
        )
        write(
            self.corpus,
            "run/control/007_only_qemu_notice.ft",
            """\
            //! run
            //! abort
            //! stderr: core dumped
            //@ program echo "qemu: uncaught target signal 6 (Abort) - core dumped" >&2
            //@ program kill -ABRT $$
            """,
        )
        write(
            self.corpus,
            "run/ffi/001_link.ft",
            """\
            //! run
            //! link: ffi/helpers.c
            //! stdout:
            //| linked helpers.c fort_rt.o
            """,
        )
        write(
            self.corpus,
            "run/modules/two/main.ft",
            """\
            //! run
            //! stdout:
            //| from main
            //@ program echo from main
            import util;
            """,
        )
        write(self.corpus, "run/modules/two/util.ft", "fn i32 f() { return 1; }\n")
        write(
            self.corpus,
            "run/lexical/001_floats.ft",
            """\
            //! run
            //! stdout:
            //| 1.5
            //@ exit 1
            //@ stderr run/lexical/001_floats.ft:6:5: error: not supported by the bootstrap compiler
            """,
        )
        write(
            self.corpus,
            "fail/mutability/001_annotated.ft",
            """\
            //! fail
            fn i32 main() {
                i32 x = 1;
                x = 2; //! error: immutable
                return x;
            }
            //@ exit 1
            //@ stderr fail/mutability/001_annotated.ft:4:5: error: assignment to immutable 'x'
            """,
        )
        write(
            self.corpus,
            "fail/mutability/002_unannotated.ft",
            """\
            //! fail
            fn i32 main() {
                i32 x = 1;
                x = 2; //! error: immutable
                return x;
            }
            //@ exit 1
            //@ stderr fail/mutability/002_unannotated.ft:4:5: error: assignment to immutable 'x'
            //@ stderr fail/mutability/002_unannotated.ft:5:5: error: something else
            """,
        )
        write(
            self.corpus,
            "fail/modules/001_multi/main.ft",
            """\
            //! fail
            //! error-any: circular
            import other;
            //@ exit 1
            //@ stderr fail/modules/001_multi/main.ft:1:1: error: circular import main -> other
            //@ stderr fail/modules/001_multi/other.ft:3:12: error: undeclared 'x'
            """,
        )
        write(
            self.corpus,
            "fail/modules/001_multi/other.ft",
            """\
            import main;
            fn i32 f() {
                return x; //! error: undeclared
            }
            """,
        )
        write(
            self.corpus,
            "programs/prog.ft",
            """\
            //! run
            //! stdout:
            //| program
            //@ program echo program
            """,
        )
        write(self.corpus, "xfail.txt", "run/control/004_wrong_output.ft\nrun/control/005\n")
        write(self.corpus, "bootstrap-unsupported.txt", "run/lexical/001_floats.ft\n")

    def test_run(self):
        self.write_corpus()
        status, lines = self.run_main()
        self.assertEqual(
            lines,
            [
                "PASS fail/modules/001_multi",
                "PASS fail/mutability/001_annotated.ft",
                "FAIL fail/mutability/002_unannotated.ft: unannotated diagnostic "
                "fail/mutability/002_unannotated.ft:5: something else",
                "PASS programs/prog.ft",
                "PASS run/control/001_echo.ft",
                "PASS run/control/002_abort.ft",
                "PASS run/control/003_args_stdin_exit.ft",
                "XFAIL run/control/004_wrong_output.ft: stdout line 1: expected 'expected', "
                "got 'actual'",
                "XFAIL run/control/005_not_implemented.ft: compiler exited 2: fort: error: "
                "not implemented",
                "FAIL run/control/006_no_abort.ft: expected SIGABRT, got exit 0",
                "FAIL run/control/007_only_qemu_notice.ft: stderr lacks 'core dumped'",
                "PASS run/ffi/001_link.ft",
                "PASS run/lexical/001_floats.ft",
                "PASS run/modules/two",
                "run_tests.py: 14 tests: 9 passed, 3 failed, 2 xfail, 0 xpass, 0 errors",
            ],
        )
        self.assertEqual(status, 1)

    def test_filters_and_no_unsupported(self):
        self.write_corpus()
        status, lines = self.run_main("--no-unsupported", "floats", "001_echo")
        self.assertEqual(
            lines,
            [
                "PASS run/control/001_echo.ft",
                "FAIL run/lexical/001_floats.ft: compiler exited 1: run/lexical/001_floats.ft:6:5: "
                "error: not supported by the bootstrap compiler",
                "run_tests.py: 2 tests: 1 passed, 1 failed, 0 xfail, 0 xpass, 0 errors",
            ],
        )
        self.assertEqual(status, 1)

    def test_all_pass_exits_zero(self):
        self.write_corpus()
        status, lines = self.run_main(
            "run/control/001", "run/ffi", "run/modules", "programs/", "001_annotated"
        )
        self.assertEqual(status, 0)
        self.assertEqual(
            lines[-1], "run_tests.py: 5 tests: 5 passed, 0 failed, 0 xfail, 0 xpass, 0 errors"
        )

    def test_xpass_fails_the_run(self):
        self.write_corpus()
        xfail = write(self.root, "xpass.txt", "run/control/001_echo.ft\n")
        status, lines = self.run_main("--xfail", str(xfail), "001_echo")
        self.assertEqual(
            lines,
            [
                "XPASS run/control/001_echo.ft: listed in xfail.txt but passed",
                "run_tests.py: 1 tests: 0 passed, 0 failed, 0 xfail, 1 xpass, 0 errors",
            ],
        )
        self.assertEqual(status, 1)
        status, lines = self.run_main("--no-xfail", "004_wrong")
        self.assertEqual(status, 1)
        self.assertTrue(lines[0].startswith("FAIL run/control/004_wrong_output.ft: "))

    def test_false_compiler(self):
        self.write_corpus()
        status, lines = self.run_main("--fort", "/bin/false", "--no-xfail")
        self.assertEqual(status, 1)
        self.assertEqual(len(lines), 15)
        for line in lines[:-1]:
            self.assertTrue(line.startswith("FAIL "), line)
            self.assertIn("compiler exited 1", line)

    def test_list_and_lint(self):
        self.write_corpus()
        status, lines = self.run_main("--list")
        self.assertEqual(status, 0)
        self.assertEqual(lines[0], "fail/modules/001_multi")
        self.assertEqual(lines[-1], "14 tests")
        status, lines = self.run_main("--list", "modules")
        self.assertEqual(lines, ["fail/modules/001_multi", "run/modules/two", "2 tests"])
        status, lines = self.run_main("--lint")
        self.assertEqual((status, lines), (0, ["run_tests.py: 14 tests, no problems"]))

    def test_lint_failure_blocks_the_run(self):
        self.write_corpus()
        write(self.corpus, "run/control/009_gap.ft", "//! run\n//! bogus\n")
        write(self.corpus, "xfail.txt", "run/nothing/\n")
        status, lines = self.run_main("--lint")
        self.assertEqual(status, 1)
        self.assertEqual(
            lines,
            [
                "lint: run/control: gap in numbering: expected 008, found 009",
                "lint: run/control/009_gap.ft:2: unknown directive 'bogus'",
                "lint: xfail.txt: 'run/nothing/' matches no test",
                "run_tests.py: 3 problems in 15 tests",
            ],
        )
        status, lines = self.run_main("001_echo")
        self.assertEqual(status, 1)
        self.assertTrue(lines[0].startswith("lint: "))

    def test_missing_compiler(self):
        self.write_corpus()
        with self.assertRaises(SystemExit):
            self.run_main("--fort", str(self.root / "nope"))
        with self.assertRaises(SystemExit):
            self.run_main("--xfail", str(self.root / "nope"))

    def test_no_test_selected(self):
        self.write_corpus()
        status, lines = self.run_main("mutabilty", "nothing")
        self.assertEqual(
            (status, lines), (1, ["run_tests.py: error: no test matches mutabilty nothing"])
        )
        status, lines = self.run_main("--list", "mutabilty")
        self.assertEqual((status, lines), (0, ["0 tests"]))

    def test_other_expectation_file_is_named(self):
        self.write_corpus()
        other = write(self.corpus, "other-xfail.txt", "run/control/001_echo.ft\nrun/nothing/\n")
        status, lines = self.run_main("--xfail", str(other), "--lint")
        self.assertEqual(status, 1)
        self.assertEqual(lines[0], "lint: %s: 'run/nothing/' matches no test" % other)

    def test_missing_runner_is_an_error(self):
        self.write_corpus()
        status, lines = self.run_main("--runner", str(self.root / "no-runner"), "001_echo")
        self.assertEqual(status, 1)
        self.assertTrue(
            lines[0].startswith("ERROR run/control/001_echo.ft: program failed to start: "),
            lines[0],
        )

    def test_timeouts(self):
        write(self.corpus, "run/control/001_slow_compiler.ft", "//! run\n//@ hang\n")
        write(self.corpus, "run/control/002_slow_program.ft", "//! run\n//@ program sleep 30\n")
        status, lines = self.run_main("--timeout", "1")
        self.assertEqual(
            lines[:2],
            [
                "ERROR run/control/001_slow_compiler.ft: compiler timed out",
                "FAIL run/control/002_slow_program.ft: program timed out",
            ],
        )
        self.assertEqual(status, 1)

    def test_verbose_and_keep(self):
        self.write_corpus()
        status, lines = self.run_main("-v", "--keep", "004_wrong")
        self.assertEqual(status, 0)
        self.assertEqual(
            lines[0],
            "XFAIL run/control/004_wrong_output.ft: stdout line 1: expected 'expected', "
            "got 'actual'",
        )
        commands = [t for t in lines if t.startswith("  $ ")]
        self.assertEqual(len(commands), 2)
        self.assertIn("--cc %s --std-dir %s -o " % (self.cc, self.corpus / "std"), commands[0])
        self.assertTrue(commands[0].endswith("run/control/004_wrong_output.ft  [exit 0]"))
        self.assertTrue(commands[1].endswith("/prog  [exit 0]"))
        kept = [t for t in lines if t.startswith("  kept: ")]
        self.assertEqual(len(kept), 1)
        workdir = kept[0][len("  kept: ") :]
        self.assertTrue(os.path.isdir(workdir))
        self.assertTrue(os.path.isfile(os.path.join(workdir, "prog")))
        shutil.rmtree(workdir)

    def test_runner(self):
        self.write_corpus()
        runner = executable(self.root, "bin/runner", '#!/bin/sh\necho "runner $(basename "$1")"\n')
        write(
            self.corpus,
            "run/control/008_runner.ft",
            """\
            //! run
            //! stdout:
            //| runner prog
            //@ program echo never printed
            """,
        )
        status, lines = self.run_main("--runner", str(runner), "008_runner")
        self.assertEqual((status, lines[0]), (0, "PASS run/control/008_runner.ft"))


if __name__ == "__main__":
    unittest.main()
