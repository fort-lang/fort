#!/usr/bin/env python3
"""Unit tests of run_tests.py: directive parsing, lint, judging and an end-to-end run.

The end-to-end tests use a fake `fort` whose behavior is scripted by `//@`
lines in the test files it is given (they are ordinary comments to the
harness), a fake `cc` that turns `prog.o` into `prog` and a fake `opt` that
rejects a module containing the word `invalid`. Run with
`python3 -m unittest run_tests_test` from this directory.
"""

import contextlib
import io
import json
import os
import shutil
import signal
import stat
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_tests  # noqa: E402

FAKE_FORT = r'''#!/usr/bin/env python3
"""A stand-in compiler: the entry file's //@ lines script its behavior.

//@ exit N          exit with status N (default 0; the program is written only on 0)
//@ stderr TEXT     print TEXT on stderr (a diagnostic, for example)
//@ program LINE    a line of the /bin/sh program written to -o
//@ module LINE     a line of the LLVM module written to -o under -S
//@ ir-exit N       exit with status N under -S (default 0)
//@ flag FLAG       exit 2 unless FLAG is on the command line
//@ hang            sleep, to exercise the timeout

Under --check it writes no program; under --check --json it writes one document
to stdout and no text diagnostic (D20.1, D20.2). The document is derived from
the //@ stderr lines that look like diagnostics unless one of these overrides it:

//@ check-exit N    exit with status N under --check (default: the `exit` status)
//@ json-exit N     exit with status N under --check --json (default: the --check status)
//@ json-file PATH  add PATH to the document's "files" (the entry is always there)
//@ json TEXT       write TEXT as the document instead of the derived one
//@ json-none       write nothing at all to stdout
//@ json-stderr T   print T on stderr under --check --json, which must write none
//@ symbol TEXT     one record of the document's "symbols", written under --index
"""
import json
import os
import re
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
ir_status, module = 0, ['target triple = "x86_64-unknown-linux-gnu"']
check = "--check" in args
as_json = check and "--json" in args
indexed = as_json and "--index" in args
check_status, json_status = None, None
diagnostics, doc_lines, doc_files, symbols = [], [], [], []
no_document = False
with open(entry) as f:
    for line in f:
        line = line.rstrip("\n")
        if line.startswith("//@ exit "):
            status = int(line[9:])
        elif line.startswith("//@ check-exit "):
            check_status = int(line[15:])
        elif line.startswith("//@ json-exit "):
            json_status = int(line[14:])
        elif line.startswith("//@ json-file "):
            doc_files.append(line[14:])
        elif line.startswith("//@ json-none"):
            no_document = True
        elif line.startswith("//@ json-stderr ") and as_json:
            sys.stderr.write(line[16:] + "\n")
        elif line.startswith("//@ json "):
            doc_lines.append(line[9:])
        elif line.startswith("//@ symbol "):
            symbols.append(line[11:])
        elif line.startswith("//@ stderr "):
            diagnostics.append(line[11:])
            if not as_json:
                sys.stderr.write(line[11:] + "\n")
        elif line.startswith("//@ program "):
            program.append(line[12:])
        elif line.startswith("//@ module "):
            module.append(line[11:])
        elif line.startswith("//@ ir-exit "):
            ir_status = int(line[12:])
        elif line.startswith("//@ flag ") and line[9:] not in args:
            sys.stderr.write("fort: error: missing flag %s\n" % line[9:])
            sys.exit(2)
        elif line.startswith("//@ hang"):
            time.sleep(30)
if check:
    if as_json and not no_document:
        if doc_lines:
            sys.stdout.write("\n".join(doc_lines) + "\n")
        else:
            records = []
            for text in diagnostics:
                m = re.match(r"^(.+):(\d+):(\d+): error: (.*)$", text)
                if m:
                    records.append(
                        {
                            "file": m.group(1),
                            "line": int(m.group(2)),
                            "col": int(m.group(3)),
                            "end_line": int(m.group(2)),
                            "end_col": int(m.group(3)) + 1,
                            "severity": "error",
                            "message": m.group(4),
                            "notes": [],
                        }
                    )
            document = {
                "version": 1,
                "files": [entry] + doc_files,
                "diagnostics": records,
                "symbols": [json.loads(s) for s in symbols] if indexed else [],
            }
            sys.stdout.write(json.dumps(document, separators=(",", ":")) + "\n")
    code = status if check_status is None else check_status
    if as_json and json_status is not None:
        code = json_status
    sys.exit(code)
if "-S" in args:
    if ir_status == 0:
        with open(out, "w") as f:
            f.write("\n".join(module) + "\n")
    sys.exit(ir_status)
if status == 0:
    with open(out, "w") as f:
        f.write("\n".join(program) + "\n")
    os.chmod(out, 0o755)
sys.exit(status)
'''

FAKE_CC = r'''#!/usr/bin/env python3
"""A stand-in linker: copies the object (a shell script) to -o and logs the other inputs.

An input named `broken.c` makes it fail, as a real linker would on a bad helper.
"""
import os
import shutil
import sys

args = sys.argv[1:]
out = args[args.index("-o") + 1]
inputs = [a for i, a in enumerate(args) if not a.startswith("-") and args[i - 1] != "-o"]
if any(os.path.basename(p) == "broken.c" for p in inputs):
    sys.stderr.write("cc: error: broken.c: undefined reference\n")
    sys.exit(1)
shutil.copy(inputs[0], out)
with open(out, "a") as f:
    f.write("echo linked %s\n" % " ".join(os.path.basename(p) for p in inputs[1:]))
os.chmod(out, 0o755)
'''

FAKE_OPT = r'''#!/usr/bin/env python3
"""A stand-in LLVM opt: rejects a module that contains the word `invalid`."""
import sys

path = sys.argv[-1]
with open(path) as f:
    text = f.read()
if "invalid" in text:
    sys.stderr.write("opt: %s:1:1: error: invalid module\n" % path)
    sys.exit(1)
'''


FAKE_OPT_HANG = """#!/usr/bin/env python3
import time

time.sleep(30)
"""

FAKE_OPT_SIGNAL = """#!/usr/bin/env python3
import os
import signal

os.kill(os.getpid(), signal.SIGKILL)
"""


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
            fn main() i32 { return 3; }
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
            fn main() i32 { return 0; }
            """,
        )
        self.assertEqual(test.problems, [])
        self.assertTrue(test.abort)
        self.assertEqual(test.stdout, b"before\n")

    def test_signal(self):
        test = parse(
            self.root,
            "run/ffi/002_trap.ft",
            """\
            //! run
            //! signal: ILL
            //! stdout:
            //| before
            fn main() i32 { return 0; }
            """,
        )
        self.assertEqual(test.problems, [])
        self.assertEqual(test.signal_name, "ILL")
        self.assertFalse(test.abort)
        self.assertEqual(test.stdout, b"before\n")
        for name in ("ABRT", "BUS", "FPE", "ILL", "SEGV", "TRAP"):
            other = parse(self.root, "run/control/001_x.ft", "//! run\n//! signal: %s\n" % name)
            self.assertEqual(other.problems, [])
            self.assertEqual(other.signal_name, name)

    def test_fail_annotations(self):
        test = parse(
            self.root,
            "fail/mutability/001_x.ft",
            """\
            //! fail
            //! error-any: circular
            //! stderr: something
            fn main() i32 {
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
            self.problems_of("fn main() i32 {}\n"),
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

    def test_signal_problems(self):
        expected = "signal: expected one of ABRT, BUS, FPE, ILL, SEGV, TRAP"
        for value in ("SIGILL", "ill", "4", "KILL", "ILL ABRT"):
            self.assertEqual(
                self.problems_of("//! run\n//! signal: %s\n" % value),
                ["run/control/001_x.ft:2: " + expected],
            )
        self.assertEqual(
            self.problems_of("//! run\n//! signal\n"),
            ["run/control/001_x.ft:2: 'signal:' needs text"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! signal: ILL\n//! signal: SEGV\n"),
            ["run/control/001_x.ft:3: duplicate 'signal:' directive"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! signal: ILL\n//! abort\n"),
            ["run/control/001_x.ft:1: 'abort' and 'signal' are mutually exclusive"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! exit: 1\n//! signal: ILL\n"),
            ["run/control/001_x.ft:1: 'exit' and 'signal' are mutually exclusive"],
        )
        self.assertEqual(
            self.problems_of("//! run\n//! exit: 1\n//! abort\n//! signal: ILL\n"),
            [
                "run/control/001_x.ft:1: 'exit' and 'abort' are mutually exclusive",
                "run/control/001_x.ft:1: 'exit' and 'signal' are mutually exclusive",
                "run/control/001_x.ft:1: 'abort' and 'signal' are mutually exclusive",
            ],
        )
        self.assertEqual(
            self.problems_of("//! fail\n//! signal: ILL\n", "fail/control/001_x.ft", "fail"),
            ["fail/control/001_x.ft:2: 'signal' is only allowed in run tests"],
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
        write(self.root, "run/modules/two/util.ft", "fn f() void {}\n")
        write(self.root, "run/modules/two/sub/deep.ft", "fn g() void {}\n")
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

    def test_module_tests_at_the_root(self):
        """test/fort: a `<x>_test.ft` beside the root is a run test of a compiler module."""
        write(self.root, "containers_test.ft", "//! run\n")
        write(self.root, "diag_test.ft", "//! run\n")
        tests, problems = run_tests.discover(self.root)
        self.assertEqual(problems, [])
        self.assertEqual(
            [(t.path, t.entry, t.expected_kind, t.multi) for t in tests],
            [
                ("containers_test.ft", "containers_test.ft", "run", False),
                ("diag_test.ft", "diag_test.ft", "run", False),
            ],
        )

    def test_module_test_name_is_checked(self):
        write(self.root, "Bad_Name_test.ft", "//! run\n")
        _, problems = run_tests.discover(self.root)
        self.assertEqual(problems, ["Bad_Name_test.ft: bad test name"])

    def test_a_plain_ft_at_the_root_is_a_problem(self):
        """A `.ft` there that is not a test is a typo nothing would run, so it is reported."""
        write(self.root, "helper.ft", "fn f() void {}\n")
        write(self.root, "containers_tets.ft", "//! run\n")
        write(self.root, "README.md", "ignored\n")
        tests, problems = run_tests.discover(self.root)
        self.assertEqual(tests, [])
        self.assertEqual(
            problems, ["containers_tets.ft: bad test name", "helper.ft: bad test name"]
        )

    def test_a_test_below_the_root_is_reported(self):
        """A `*_test.ft` in a directory discovery does not walk (T-079).

        `test/fort/support` holds the code several module tests share and is
        invisible to `discover`, so a test misfiled there would otherwise run
        nowhere and say nothing at all.
        """
        write(self.root, "containers_test.ft", "//! run\n")
        write(self.root, "support/stray_test.ft", "garbage garbage\n")
        tests, problems = run_tests.discover(self.root)
        self.assertEqual([t.path for t in tests], ["containers_test.ft"])
        self.assertEqual(problems, ["support/stray_test.ft: test outside the root of the corpus"])

    def test_shared_code_below_the_root_is_not_a_test(self):
        """The directory is there to hold exactly this, so it is no problem."""
        write(self.root, "containers_test.ft", "//! run\n")
        write(self.root, "support/types_env.ft", "fn f() void {}\n")
        write(self.root, "support/capture.ft", "fn g() void {}\n")
        _, problems = run_tests.discover(self.root)
        self.assertEqual(problems, [])

    def test_a_test_nested_deeper_below_the_root_is_reported(self):
        write(self.root, "helpers/inner/lexer_test.ft", "//! run\n")
        _, problems = run_tests.discover(self.root)
        self.assertEqual(
            problems, ["helpers/inner/lexer_test.ft: test outside the root of the corpus"]
        )

    def test_a_module_of_a_directory_test_is_not_reported(self):
        """`run`, `fail` and `programs` are walked by name and judged there."""
        write(self.root, "run/modules/two/main.ft", "//! run\n")
        write(self.root, "run/modules/two/util_test.ft", "fn f() void {}\n")
        _, problems = run_tests.discover(self.root)
        self.assertEqual(problems, [])

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
            for p in ("run/arrays/001_a.ft", "run/spans/001_a.ft", "programs/p.ft")
        ]
        self.assertEqual(run_tests.select(tests, []), tests)
        self.assertEqual(
            [t.path for t in run_tests.select(tests, ["arrays"])], ["run/arrays/001_a.ft"]
        )
        self.assertEqual(
            [t.path for t in run_tests.select(tests, ["001_a", "programs/"])],
            ["run/arrays/001_a.ft", "run/spans/001_a.ft", "programs/p.ft"],
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
            ("FAIL", "expected exit 0, got SIGABRT"),
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

    def test_judge_run_signal(self):
        self.run_test.signal_name = "ILL"
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGILL)), ("PASS", "")
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGABRT)),
            ("FAIL", "expected SIGILL, got SIGABRT"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(0)),
            ("FAIL", "expected SIGILL, got exit 0"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(132)),
            ("FAIL", "expected SIGILL, got exit 132"),
        )
        self.run_test.signal_name = "SEGV"
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGSEGV)), ("PASS", "")
        )
        # A signal outside the normative set keeps its number, and a status is
        # never confused with a signal of the same value.
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGKILL)),
            ("FAIL", "expected SIGSEGV, got signal 9"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(int(signal.SIGSEGV))),
            ("FAIL", "expected SIGSEGV, got exit 11"),
        )
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGILL)),
            ("FAIL", "expected SIGSEGV, got SIGILL"),
        )

    def test_judge_run_signal_ignores_the_qemu_notice(self):
        notice = b"qemu: uncaught target signal 4 (Illegal instruction) - core dumped\n"
        self.run_test.signal_name = "ILL"
        self.run_test.stderr = ["Illegal instruction"]
        self.assertEqual(
            run_tests.judge_run(self.run_test, proc(0), None, proc(-signal.SIGILL, b"", notice)),
            ("FAIL", "stderr lacks 'Illegal instruction'"),
        )
        self.run_test.stderr = ["about to trap"]
        self.assertEqual(
            run_tests.judge_run(
                self.run_test, proc(0), None, proc(-signal.SIGILL, b"", b"about to trap\n" + notice)
            ),
            ("PASS", ""),
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

    def test_judge_unsupported_takes_the_bootstrap_diagnostic_anywhere(self):
        """The shape every entry of the list had before T-131: the words, in
        the test's file or in a module of the library's closure."""
        message = run_tests.UNSUPPORTED_MESSAGE
        test = run_tests.Test("fail/a.ft", "fail/a.ft", "fail")
        self.assertEqual(
            run_tests.judge_unsupported(
                test, proc(1, stderr=("fail/a.ft:2:1: error: floats %s\n" % message).encode())
            ),
            ("PASS", ""),
        )
        self.assertEqual(
            run_tests.judge_unsupported(
                test, proc(1, stderr=("std/math.ft:9:1: error: %s: ?:\n" % message).encode())
            ),
            ("PASS", ""),
        )

    def test_judge_unsupported_takes_a_plain_error_in_the_tests_own_file(self):
        """The shape T-131 adds: the C bootstrap meets a form added after pin
        0 and reports an ordinary syntax error, with none of the words."""
        test = run_tests.Test("fail/a.ft", "fail/a.ft", "fail")
        self.assertEqual(
            run_tests.judge_unsupported(test, proc(1, stderr=b"fail/a.ft:2:1: error: other\n")),
            ("PASS", ""),
        )

    def test_judge_unsupported_takes_a_plain_error_in_a_file_of_a_directory_test(self):
        multi = run_tests.Test("fail/dir", "fail/dir/main.ft", "fail")
        self.assertEqual(
            run_tests.judge_unsupported(
                multi, proc(1, stderr=b"fail/dir/other.ft:2:1: error: nonsense\n")
            ),
            ("PASS", ""),
        )

    def test_judge_unsupported_refuses_an_error_about_another_test(self):
        """The guard the file clause is for: a compiler that reports about
        something that is not this test passes neither shape."""
        message = run_tests.UNSUPPORTED_MESSAGE
        test = run_tests.Test("fail/a.ft", "fail/a.ft", "fail")
        self.assertEqual(
            run_tests.judge_unsupported(test, proc(1, stderr=b"fail/b.ft:2:1: error: other\n")),
            ("FAIL", "no diagnostic in fail/a.ft and none containing '%s'" % message),
        )
        multi = run_tests.Test("fail/dir", "fail/dir/main.ft", "fail")
        self.assertEqual(
            run_tests.judge_unsupported(
                multi, proc(1, stderr=b"fail/directory/other.ft:2:1: error: other\n")
            )[0],
            "FAIL",
        )

    def test_judge_unsupported_refuses_a_compiler_that_says_nothing(self):
        message = run_tests.UNSUPPORTED_MESSAGE
        test = run_tests.Test("fail/a.ft", "fail/a.ft", "fail")
        self.assertEqual(
            run_tests.judge_unsupported(test, proc(1, stderr=message.encode())),
            ("FAIL", "compiler exited 1 without diagnostics"),
        )
        self.assertEqual(
            run_tests.judge_unsupported(test, proc(0)),
            ("FAIL", "compiled successfully, expected exit 1"),
        )
        self.assertEqual(run_tests.judge_unsupported(test, proc(2))[0], "ERROR")

    def test_owns_file(self):
        """A directory test owns every file under it and a single-file test
        owns itself. The prefix is a path prefix: `fail/dir` does not own
        `fail/directory/x.ft`."""
        one = run_tests.Test("fail/a.ft", "fail/a.ft", "fail")
        self.assertTrue(run_tests.owns_file(one, "fail/a.ft"))
        self.assertFalse(run_tests.owns_file(one, "fail/a.ft.bak"))
        self.assertFalse(run_tests.owns_file(one, "std/rt.ft"))
        multi = run_tests.Test("fail/dir", "fail/dir/main.ft", "fail")
        self.assertTrue(run_tests.owns_file(multi, "fail/dir/main.ft"))
        self.assertTrue(run_tests.owns_file(multi, "fail/dir/other.ft"))
        self.assertTrue(run_tests.owns_file(multi, "fail/dir"))
        self.assertFalse(run_tests.owns_file(multi, "fail/directory/other.ft"))

    def test_apply_expectations(self):
        self.assertEqual(run_tests.apply_expectations("PASS", "", False), ("PASS", ""))
        self.assertEqual(run_tests.apply_expectations("FAIL", "why", False), ("FAIL", "why"))
        self.assertEqual(run_tests.apply_expectations("FAIL", "why", True), ("XFAIL", "why"))
        self.assertEqual(run_tests.apply_expectations("ERROR", "why", True), ("XFAIL", "why"))
        self.assertEqual(
            run_tests.apply_expectations("PASS", "", True),
            ("XPASS", "listed in xfail.txt but passed"),
        )
        # An XPASS names the list actually in use, which --xfail may change.
        self.assertEqual(
            run_tests.apply_expectations("PASS", "", True, "xfail-stage2.txt"),
            ("XPASS", "listed in xfail-stage2.txt but passed"),
        )


# ---- end to end with a fake compiler ---------------------------------------------------


class EndToEnd(TempRoot):
    def setUp(self):
        super().setUp()
        self.fort = executable(self.root, "bin/fort", FAKE_FORT)
        self.cc = executable(self.root, "bin/cc", FAKE_CC)
        self.opt = executable(self.root, "bin/opt", FAKE_OPT)
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
            //| linked helpers.c
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
        write(self.corpus, "run/modules/two/util.ft", "fn f() i32 { return 1; }\n")
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
            fn main() i32 {
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
            fn main() i32 {
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
            //@ json-file fail/modules/001_multi/other.ft
            //@ stderr fail/modules/001_multi/main.ft:1:1: error: circular import main -> other
            //@ stderr fail/modules/001_multi/other.ft:3:12: error: undeclared 'x'
            """,
        )
        write(
            self.corpus,
            "fail/modules/001_multi/other.ft",
            """\
            import main;
            fn f() i32 {
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
        """The XPASS names the list in use, which --xfail may have changed."""
        self.write_corpus()
        xfail = write(self.root, "xpass.txt", "run/control/001_echo.ft\n")
        status, lines = self.run_main("--xfail", str(xfail), "001_echo")
        self.assertEqual(
            lines,
            [
                "XPASS run/control/001_echo.ft: listed in xpass.txt but passed",
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

    def verify_options(self):
        return ["--verify-ir", "--opt", str(self.opt)]

    def write_ir_test(self, name, *scripted):
        write(
            self.corpus,
            name,
            "//! run\n//! stdout:\n//| hi\n//@ program echo hi\n" + "".join(scripted),
        )

    def test_verify_ir_accepts_a_valid_module(self):
        self.write_ir_test(
            "run/control/001_ok.ft", '//@ module define i32 @"main.main"() { ret i32 0 }\n'
        )
        status, lines = self.run_main(*self.verify_options())
        self.assertEqual(lines[0], "PASS run/control/001_ok.ft")
        self.assertEqual(status, 0)

    def test_verify_ir_rejects_a_bad_module(self):
        self.write_ir_test("run/control/001_bad.ft", "//@ module invalid\n")
        status, lines = self.run_main(*self.verify_options())
        self.assertEqual(status, 1)
        self.assertTrue(
            lines[0].startswith(
                "FAIL run/control/001_bad.ft: the IR verifier rejected the module: opt: "
            ),
            lines[0],
        )
        self.assertTrue(lines[0].endswith("error: invalid module"), lines[0])

    def test_a_bad_module_is_verified_only_on_request(self):
        self.write_ir_test("run/control/001_bad.ft", "//@ module invalid\n")
        status, lines = self.run_main()
        self.assertEqual(lines[0], "PASS run/control/001_bad.ft")
        self.assertEqual(status, 0)

    def test_verify_ir_reports_a_failing_emission(self):
        self.write_ir_test(
            "run/control/001_no_ir.ft",
            "//@ ir-exit 2\n",
            "//@ stderr fort: error: not implemented\n",
        )
        status, lines = self.run_main(*self.verify_options())
        self.assertEqual(
            lines[0],
            "ERROR run/control/001_no_ir.ft: -S: compiler exited 2: fort: error: not implemented",
        )
        self.assertEqual(status, 1)

    def test_verify_ir_reports_a_rejected_emission(self):
        self.write_ir_test("run/control/001_error.ft", "//@ ir-exit 1\n")
        status, lines = self.run_main(*self.verify_options())
        self.assertEqual(lines[0], "FAIL run/control/001_error.ft: -S: compiler exited 1")
        self.assertEqual(status, 1)

    def test_verify_ir_needs_an_opt(self):
        self.write_ir_test("run/control/001_ok.ft")
        status, lines = self.run_main("--verify-ir", "--opt", str(self.root / "no-opt"))
        self.assertEqual(status, 1)
        self.assertTrue(
            lines[0].startswith("ERROR run/control/001_ok.ft: the IR verifier failed to start: "),
            lines[0],
        )

    def test_verify_ir_leaves_a_fail_test_alone(self):
        write(
            self.corpus,
            "fail/mutability/001_x.ft",
            """\
            //! fail
            fn main() i32 {
                x = 2; //! error: immutable
            }
            //@ module invalid
            //@ exit 1
            //@ stderr fail/mutability/001_x.ft:3:5: error: assignment to immutable 'x'
            """,
        )
        status, lines = self.run_main(*self.verify_options())
        self.assertEqual(lines[0], "PASS fail/mutability/001_x.ft")
        self.assertEqual(status, 0)

    def test_verify_ir_timeout_and_signal_are_toolchain_errors(self):
        self.write_ir_test("run/control/001_ok.ft")
        hang = executable(self.root, "bin/opt_hang", FAKE_OPT_HANG)
        status, lines = self.run_main("--verify-ir", "--opt", str(hang), "--timeout", "1")
        self.assertEqual(lines[0], "ERROR run/control/001_ok.ft: the IR verifier timed out")
        self.assertEqual(status, 1)
        killed = executable(self.root, "bin/opt_signal", FAKE_OPT_SIGNAL)
        status, lines = self.run_main("--verify-ir", "--opt", str(killed))
        self.assertEqual(
            lines[0], "ERROR run/control/001_ok.ft: the IR verifier was killed by signal 9"
        )
        self.assertEqual(status, 1)

    def test_a_failed_link_is_not_masked_by_the_verifier(self):
        write(self.corpus, "ffi/broken.c", "int broken(void);\n")
        write(
            self.corpus,
            "run/ffi/001_link.ft",
            "//! run\n//! link: ffi/broken.c\n//@ module invalid\n",
        )
        status, lines = self.run_main(*self.verify_options())
        self.assertEqual(
            lines[0],
            "ERROR run/ffi/001_link.ft: link failed: cc: error: broken.c: undefined reference",
        )
        self.assertEqual(status, 1)

    def test_link_command(self):
        test = run_tests.Test("run/ffi/001_link.ft", "run/ffi/001_link.ft", "run")
        test.links = ["ffi/helpers.c"]
        config = run_tests.Config(root=self.corpus, fort="/bin/fort", std_dir="/std", cc="clang")
        self.assertEqual(
            run_tests.link_command(config, test, "/tmp/w/prog", "/tmp/w/prog.o"),
            [
                "clang",
                "--target=x86_64-linux-gnu",
                # The fort side is compiled at -O1, and a helper built at -O0
                # re-narrows its parameters and hides a wrong extension.
                "-O1",
                "-o",
                "/tmp/w/prog",
                "/tmp/w/prog.o",
                str(self.corpus / "ffi/helpers.c"),
            ],
        )

    def test_verify_command(self):
        config = run_tests.Config(root=self.corpus, fort="/bin/fort", std_dir="/std", opt="opt-18")
        self.assertEqual(
            run_tests.verify_command(config, "/tmp/w/prog.ll"),
            ["opt-18", "-passes=verify", "-disable-output", "/tmp/w/prog.ll"],
        )

    def test_verify_ir_command(self):
        test = run_tests.Test("run/control/001_ok.ft", "run/control/001_ok.ft", "run")
        test.flags = ["--release"]
        config = run_tests.Config(root=self.corpus, fort="/bin/fort", std_dir="/std")
        self.assertEqual(
            run_tests.compile_command(config, test, "/tmp/w/prog.ll", emit_ir=True),
            [
                "/bin/fort",
                "--cc",
                run_tests.DEFAULT_CC,
                "--std-dir",
                "/std",
                "--release",
                "-S",
                "-o",
                "/tmp/w/prog.ll",
                "run/control/001_ok.ft",
            ],
        )

    def test_defaults_name_clang_and_opt(self):
        args = run_tests.parse_args([])
        self.assertEqual((args.cc, args.target, args.opt), ("clang", "x86_64-linux-gnu", "opt-18"))
        self.assertFalse(args.verify_ir)

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

    def test_mac_native_child_has_no_qemu_prefix(self):
        write(
            self.corpus,
            "run/control/001_prefix.ft",
            """\
            //! run
            //! stdout:
            //| unset
            //@ program echo "${QEMU_LD_PREFIX-unset}"
            """,
        )
        with mock.patch.dict(os.environ, {"QEMU_LD_PREFIX": "/inherited"}):
            status, lines = self.run_main("--target", "arm64-apple-macosx26.6.2")
        self.assertEqual((status, lines[0]), (0, "PASS run/control/001_prefix.ft"))

    def test_linux_child_keeps_qemu_prefix_without_runner(self):
        write(
            self.corpus,
            "run/control/001_prefix.ft",
            """\
            //! run
            //! stdout:
            //| /usr/x86_64-linux-gnu
            //@ program echo "${QEMU_LD_PREFIX-unset}"
            """,
        )
        with mock.patch.dict(os.environ, {}, clear=True):
            status, lines = self.run_main("--target", "x86_64-linux-gnu")
        self.assertEqual((status, lines[0]), (0, "PASS run/control/001_prefix.ft"))


# ---- the check document (D20.1, D20.2) -------------------------------------------------


class CheckJson(EndToEnd):
    """`--check-json`: the document of `fort --check --json` against the text form."""

    def document(self, files, diagnostics, **rest):
        """A document of D20.2, before the `//@ json` override writes it out."""
        doc = {
            "version": 1,
            "files": list(files),
            "diagnostics": list(diagnostics),
            "symbols": [],
        }
        doc.update(rest)
        return doc

    def diagnostic(self, file, line, col, end_col, message, notes=()):
        """One error of a document, a range on one line (D20.2, D20.4)."""
        return {
            "file": file,
            "line": line,
            "col": col,
            "end_line": line,
            "end_col": end_col,
            "severity": "error",
            "message": message,
            "notes": list(notes),
        }

    def note(self, file, line, col, end_col, message):
        return {
            "file": file,
            "line": line,
            "col": col,
            "end_line": line,
            "end_col": end_col,
            "message": message,
        }

    def script(self, name, directives, expected="scripted"):
        """One fail test of the corpus whose //@ lines script both runs.

        Its code line is line 3, which every scripted range points into; the
        corpus is fresh in every test, so every test is the first of its area.
        """
        path = "fail/mutability/%s" % name
        header = "//! fail\n//! error-any: %s\nfn i32 main() { return 0; }\n" % expected
        write(self.corpus, path, header + "\n".join(directives) + "\n")
        return path

    def json_line(self, doc):
        """The `//@ json` directive that makes the fake write `doc` verbatim."""
        return "//@ json " + json.dumps(doc, separators=(",", ":"))

    def only(self, name):
        """Run --check-json over the one fail test `name`."""
        return self.run_main("--check-json", name)

    def test_the_corpus_documents_match_the_text_form(self):
        self.write_corpus()
        status, lines = self.run_main("--check-json")
        self.assertEqual(
            lines,
            [
                "PASS fail/modules/001_multi",
                "PASS fail/mutability/001_annotated.ft",
                "PASS fail/mutability/002_unannotated.ft",
                "run_tests.py: 3 tests: 3 passed, 0 failed, 0 xfail, 0 xpass, 0 errors",
            ],
        )
        self.assertEqual(status, 0)

    def test_a_test_with_nothing_to_compare_is_not_selected(self):
        self.write_corpus()
        # A run test with no golden index has neither diagnostics nor an index
        # to hold the document against (D20.2, D20.3).
        status, lines = self.run_main("--check-json", "run/control")
        self.assertEqual(status, 1)
        self.assertEqual(lines, ["run_tests.py: error: no document test matches run/control"])

    def test_a_listed_test_is_not_expected_to_fail(self):
        self.write_corpus()
        xfail = write(self.root, "xpass.txt", "fail/mutability/001_annotated.ft\n")
        # xfail.txt says the compiler cannot pass the test yet; the two forms
        # of one run must agree anyway (D20.2).
        status, lines = self.run_main("--check-json", "--xfail", str(xfail), "001_annotated")
        self.assertEqual((status, lines[0]), (0, "PASS fail/mutability/001_annotated.ft"))

    def test_a_diagnostic_only_in_the_text_form_fails(self):
        path = self.script("001_missing.ft", ["//@ exit 1"], "gone")
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: gone from the document" % path,
            self.json_line(self.document([path], [])),
        ]
        self.script("001_missing.ft", directives, "gone")
        status, lines = self.only("001_missing")
        self.assertEqual(status, 1)
        self.assertEqual(
            lines[0],
            "FAIL %s: only in the text form: %s:3:1: error: gone from the document" % (path, path),
        )

    def test_a_diagnostic_only_in_the_document_fails(self):
        path = "fail/mutability/001_extra.ft"
        doc = self.document([path], [self.diagnostic(path, 3, 1, 2, "invented")])
        self.script("001_extra.ft", ["//@ exit 1", self.json_line(doc)], "invented")
        status, lines = self.only("001_extra")
        self.assertEqual(status, 1)
        self.assertIn("only in the document: %s:3:1: error: invented" % path, lines[0])

    def test_an_empty_stdout_is_not_a_document(self):
        path = "fail/mutability/001_silent.ft"
        directives = ["//@ exit 1", "//@ stderr %s:3:1: error: quiet" % path, "//@ json-none"]
        self.script("001_silent.ft", directives, "quiet")
        status, lines = self.only("001_silent")
        self.assertEqual(status, 1)
        self.assertIn("stdout is not one JSON document", lines[0])

    def test_a_truncated_document_is_not_a_document(self):
        self.script("001_cut.ft", ["//@ exit 1", '//@ json {"version":1,"files":["a.ft"'], "cut")
        status, lines = self.only("001_cut")
        self.assertEqual(status, 1)
        self.assertIn("stdout is not one JSON document", lines[0])

    def test_a_missing_key_is_reported(self):
        path = "fail/mutability/001_shape.ft"
        doc = self.document([path], [])
        del doc["symbols"]
        self.script("001_shape.ft", ["//@ exit 1", self.json_line(doc)], "shape")
        status, lines = self.only("001_shape")
        self.assertEqual(status, 1)
        self.assertIn("document: keys are", lines[0])

    def test_another_version_and_a_filled_index_are_reported(self):
        path = "fail/mutability/001_version.ft"
        doc = self.document([path], [], version=2, symbols=["x"])
        self.script("001_version.ft", ["//@ exit 1", self.json_line(doc)], "version")
        status, lines = self.only("001_version")
        self.assertEqual(status, 1)
        self.assertIn("version is 2, expected 1", lines[0])
        self.assertIn("symbols is not empty", lines[0])

    def test_a_file_missing_from_files_is_reported(self):
        path = "fail/mutability/001_unlisted.ft"
        doc = self.document([], [self.diagnostic(path, 3, 1, 2, "unlisted")])
        self.script("001_unlisted.ft", ["//@ exit 1", self.json_line(doc)], "unlisted")
        status, lines = self.only("001_unlisted")
        self.assertEqual(status, 1)
        self.assertIn('is not in "files"', lines[0])

    def test_a_range_outside_its_line_is_reported(self):
        path = "fail/mutability/001_outside.ft"
        doc = self.document([path], [self.diagnostic(path, 3, 1, 400, "outside")])
        self.script("001_outside.ft", ["//@ exit 1", self.json_line(doc)], "outside")
        status, lines = self.only("001_outside")
        self.assertEqual(status, 1)
        self.assertIn("end column 400 is outside line 3", lines[0])

    def test_a_range_that_ends_before_it_starts_is_reported(self):
        path = "fail/mutability/001_reversed.ft"
        doc = self.document([path], [self.diagnostic(path, 3, 8, 2, "reversed")])
        self.script("001_reversed.ft", ["//@ exit 1", self.json_line(doc)], "reversed")
        status, lines = self.only("001_reversed")
        self.assertEqual(status, 1)
        self.assertIn("ends before it starts", lines[0])

    def test_an_empty_range_is_reported(self):
        path = "fail/mutability/001_empty.ft"
        # A diagnostic that is not the lexer's covers the tokens it is about,
        # so its range is never empty (D20.4).
        doc = self.document([path], [self.diagnostic(path, 3, 4, 4, "expected ';'")])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:4: error: expected ';'" % path,
            self.json_line(doc),
        ]
        self.script("001_empty.ft", directives, "expected")
        status, lines = self.only("001_empty")
        self.assertEqual(status, 1)
        self.assertIn("empty range at 3:4", lines[0])

    def test_an_empty_range_of_a_lexical_error_is_allowed(self):
        path = "fail/mutability/001_lexical.ft"
        # The lexer reports the position of bytes that are not a token, so its
        # range is empty (D14.2, D20.4); the message is what says so.
        message = "unterminated string literal"
        doc = self.document([path], [self.diagnostic(path, 3, 4, 4, message)])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:4: error: %s" % (path, message),
            self.json_line(doc),
        ]
        self.script("001_lexical.ft", directives, message)
        status, lines = self.only("001_lexical")
        self.assertEqual((status, lines[0]), (0, "PASS %s" % path))

    def test_a_note_missing_from_the_document_is_reported(self):
        path = "fail/mutability/001_dropped_note.ft"
        doc = self.document([path], [self.diagnostic(path, 3, 1, 2, "noted")])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: noted" % path,
            "//@ stderr %s:2:1: note: declared here" % path,
            self.json_line(doc),
        ]
        self.script("001_dropped_note.ft", directives, "noted")
        status, lines = self.only("001_dropped_note")
        self.assertEqual(status, 1)
        self.assertIn("only in the text form: %s:2:1: note: declared here" % path, lines[0])

    def test_a_note_beside_its_error_is_not_nested(self):
        path = "fail/mutability/001_hoisted.ft"
        # D20.2 nests a note in the "notes" of the error it follows; a note
        # listed beside it is the same text with the structure lost.
        hoisted = self.diagnostic(path, 2, 1, 2, "declared here")
        hoisted["severity"] = "note"
        doc = self.document([path], [self.diagnostic(path, 3, 1, 2, "noted"), hoisted])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: noted" % path,
            "//@ stderr %s:2:1: note: declared here" % path,
            self.json_line(doc),
        ]
        self.script("001_hoisted.ft", directives, "noted")
        status, lines = self.only("001_hoisted")
        self.assertEqual(status, 1)
        self.assertIn("diagnostics[1]: a note after an error must be nested", lines[0])

    def test_a_note_at_another_position_is_reported(self):
        path = "fail/mutability/001_moved_note.ft"
        notes = [self.note(path, 1, 1, 1, "declared here")]
        doc = self.document([path], [self.diagnostic(path, 3, 1, 2, "noted", notes)])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: noted" % path,
            "//@ stderr %s:2:1: note: declared here" % path,
            self.json_line(doc),
        ]
        self.script("001_moved_note.ft", directives, "noted")
        status, lines = self.only("001_moved_note")
        self.assertEqual(status, 1)
        self.assertIn("report 2 is '%s:2:1: note: declared here'" % path, lines[0])

    def test_the_order_of_the_diagnostics_is_compared(self):
        path = "fail/mutability/001_order.ft"
        first = self.diagnostic(path, 3, 1, 2, "first")
        second = self.diagnostic(path, 3, 5, 6, "second")
        doc = self.document([path], [second, first])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: first" % path,
            "//@ stderr %s:3:5: error: second" % path,
            self.json_line(doc),
        ]
        self.script("001_order.ft", directives, "first")
        status, lines = self.only("001_order")
        self.assertEqual(status, 1)
        self.assertIn("report 1 is '%s:3:1: error: first'" % path, lines[0])

    def test_a_duplicated_diagnostic_is_reported(self):
        path = "fail/mutability/001_twice.ft"
        once = self.diagnostic(path, 3, 1, 2, "twice")
        doc = self.document([path], [once, once])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: twice" % path,
            self.json_line(doc),
        ]
        self.script("001_twice.ft", directives, "twice")
        status, lines = self.only("001_twice")
        self.assertEqual(status, 1)
        self.assertIn("only in the document: %s:3:1: error: twice" % path, lines[0])

    def test_the_entry_must_be_in_the_files(self):
        path = "fail/mutability/001_no_entry.ft"
        doc = self.document([], [])
        self.script("001_no_entry.ft", ["//@ exit 1", self.json_line(doc)], "none")
        status, lines = self.only("001_no_entry")
        self.assertEqual(status, 1)
        self.assertIn("files: the entry '%s' is missing" % path, lines[0])

    def test_a_note_is_checked_like_a_diagnostic(self):
        path = "fail/mutability/001_note.ft"
        notes = [self.note("other.ft", 1, 1, 2, "over there")]
        doc = self.document([path], [self.diagnostic(path, 3, 1, 2, "noted", notes)])
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: noted" % path,
            self.json_line(doc),
        ]
        self.script("001_note.ft", directives, "noted")
        status, lines = self.only("001_note")
        self.assertEqual(status, 1)
        self.assertIn("diagnostics[0].notes[0]: 'other.ft' is not in \"files\"", lines[0])

    def test_a_crash_must_leave_stdout_empty(self):
        path = "fail/mutability/001_crash.ft"
        doc = self.document([path], [])
        directives = ["//@ exit 1", "//@ check-exit 2", self.json_line(doc)]
        self.script("001_crash.ft", directives, "crash")
        status, lines = self.only("001_crash")
        self.assertEqual(status, 1)
        self.assertEqual(lines[0], "FAIL %s: stdout is not empty after exit 2" % path)

    def test_an_internal_error_with_an_empty_stdout_is_an_error(self):
        path = "fail/mutability/001_silent_crash.ft"
        directives = [
            "//@ exit 1",
            "//@ check-exit 2",
            "//@ stderr fort: error: internal error: simulated",
            "//@ json-none",
        ]
        self.script("001_silent_crash.ft", directives, "crash")
        status, lines = self.only("001_silent_crash")
        self.assertEqual(status, 1)
        self.assertEqual(
            lines[0],
            "ERROR %s: text: compiler exited 2: fort: error: internal error: simulated" % path,
        )

    def test_the_two_runs_must_agree_on_the_status(self):
        path = "fail/mutability/001_status.ft"
        doc = self.document([path], [])
        directives = ["//@ exit 1", "//@ check-exit 0", "//@ json-exit 1", self.json_line(doc)]
        self.script("001_status.ft", directives, "status")
        status, lines = self.only("001_status")
        self.assertEqual(status, 1)
        self.assertIn("--json exited 1, the text form 0", lines[0])

    def test_a_text_diagnostic_under_json_is_reported(self):
        path = "fail/mutability/001_chatty.ft"
        doc = self.document([path], [self.diagnostic(path, 3, 1, 2, "chatty")])
        # --json writes no text diagnostic (D20.2); the fake's `//@ stderr`
        # lines reach stderr only because this document is scripted.
        directives = [
            "//@ exit 1",
            "//@ stderr %s:3:1: error: chatty" % path,
            "//@ json-stderr noise",
            self.json_line(doc),
        ]
        self.script("001_chatty.ft", directives, "chatty")
        status, lines = self.only("001_chatty")
        self.assertEqual(status, 1)
        self.assertIn("--json wrote to stderr: noise", lines[0])


# ---- the identifier index (D20.3) ------------------------------------------------------


class GoldenIndex(EndToEnd):
    """`--check-json` over a test with an `index.json` beside it (D20.3)."""

    def symbol(self, file, line, col, end_col, name, kind, type_, is_decl, decl=None):
        """One record of the index, a range on one line (D20.3, D20.4)."""
        return {
            "file": file,
            "line": line,
            "col": col,
            "end_line": line,
            "end_col": end_col,
            "name": name,
            "kind": kind,
            "type": type_,
            "is_decl": is_decl,
            "decl": decl,
        }

    def range_of(self, record):
        return {key: record[key] for key in run_tests.RANGE_KEYS}

    def write_test(self, symbols, golden=None, directives=()):
        """A run test of two lines whose index is `symbols`, with its golden."""
        path = "run/modules/indexed"
        entry = path + "/main.ft"
        body = "//! run\n//@ program echo ok\n" + "\n".join(directives) + "\n"
        write(self.corpus, entry, body)
        write(
            self.corpus,
            path + "/" + run_tests.INDEX_NAME,
            run_tests.render_index(symbols if golden is None else golden),
        )
        return path, entry

    def record(self, entry, line=2, **rest):
        args = dict(name="main", kind="fn", type_="fn () i32", is_decl=True)
        args.update(rest)
        rec = self.symbol(entry, line, 8, 12, **args)
        if rec["decl"] is None and rec["kind"] != "builtin":
            rec["decl"] = self.range_of(rec)
        return rec

    def run_indexed(self, symbols, golden=None, extra=()):
        path, entry = self.write_test(
            symbols, golden, ["//@ symbol " + json.dumps(s) for s in symbols] + list(extra)
        )
        status, lines = self.run_main("--check-json", path)
        return status, lines, entry

    def test_a_golden_index_that_matches_passes(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry)]
        status, lines, _ = self.run_indexed(symbols)
        self.assertEqual((status, lines[0]), (0, "PASS run/modules/indexed"))

    def test_a_test_with_a_golden_index_is_selected(self):
        self.write_corpus()
        _, entry = self.write_test([])
        symbols = [self.record(entry)]
        status, lines, _ = self.run_indexed(symbols)
        # A run test has no diagnostics to compare, so only its golden index
        # selects it (D20.3).
        self.assertEqual(status, 0)
        self.assertIn("PASS run/modules/indexed", lines)

    def test_a_differing_record_names_the_line(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry)]
        golden = [self.record(entry, name="other")]
        status, lines, _ = self.run_indexed(symbols, golden)
        self.assertEqual(status, 1)
        self.assertIn("index.json:1: expected", lines[0])
        self.assertIn('"name": "other"', lines[0])

    def test_a_missing_record_is_reported(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry)]
        golden = symbols + [self.record(entry, name="gone", line=3)]
        status, lines, _ = self.run_indexed(symbols, golden)
        self.assertEqual(status, 1)
        self.assertIn("index.json:2: expected", lines[0])
        self.assertIn("got (no record)", lines[0])

    def test_a_builtin_is_the_only_record_without_a_declaration(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, name="println", kind="builtin", type_="", is_decl=False)]
        status, lines, _ = self.run_indexed(symbols)
        self.assertEqual((status, lines[0]), (0, "PASS run/modules/indexed"))

    def test_a_null_declaration_on_anything_else_fails(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, kind="local", type_="i32", is_decl=False)]
        symbols[0]["decl"] = None
        # The golden is well formed, so what fails is the document's own
        # record and not the lint of the file (D20.3).
        status, lines, _ = self.run_indexed(symbols, [self.record(entry)])
        self.assertEqual(status, 1)
        self.assertIn('symbols[0]: only a builtin has a null "decl"', lines[0])

    def test_an_unknown_kind_fails(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, kind="widget")]
        status, lines, _ = self.run_indexed(symbols, [self.record(entry)])
        self.assertEqual(status, 1)
        self.assertIn("symbols[0]: kind is 'widget'", lines[0])

    def test_a_declaration_whose_decl_is_not_its_own_range_fails(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry)]
        symbols[0]["decl"]["line"] = 1
        status, lines, _ = self.run_indexed(symbols, [self.record(entry)])
        self.assertEqual(status, 1)
        self.assertIn('symbols[0]: the declaration\'s "decl" is not its own range', lines[0])

    def test_an_alias_declares_a_name_for_a_declaration_elsewhere(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, name="double", is_decl=True)]
        symbols[0]["decl"] = {
            "file": "run/modules/indexed/other.ft",
            "line": 1,
            "col": 1,
            "end_line": 1,
            "end_col": 1,
        }
        write(self.corpus, "run/modules/indexed/other.ft", "fn f() i32 { return 0; }\n")
        # An `as` alias is the one declaration whose "decl" is in another file
        # (D9.3, D20.3), so the harness accepts it and nothing else.
        status, lines, _ = self.run_indexed(
            symbols, extra=["//@ json-file run/modules/indexed/other.ft"]
        )
        self.assertEqual((status, lines[0]), (0, "PASS run/modules/indexed"))

    def test_a_null_type_is_a_declaration_that_failed_to_check(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, kind="local", type_=None, is_decl=True)]
        status, lines, _ = self.run_indexed(symbols)
        self.assertEqual((status, lines[0]), (0, "PASS run/modules/indexed"))

    def test_a_type_that_is_neither_a_string_nor_null_fails(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, type_=7)]
        status, lines, _ = self.run_indexed(symbols, [self.record(entry)])
        self.assertEqual(status, 1)
        self.assertIn("symbols[0]: type is 7", lines[0])

    def test_a_range_outside_its_file_fails(self):
        _, entry = self.write_test([])
        symbols = [self.record(entry, line=99)]
        status, lines, _ = self.run_indexed(symbols, [self.record(entry)])
        self.assertEqual(status, 1)
        self.assertIn("symbols[0]: start line 99 is outside the file", lines[0])

    def test_symbols_must_be_empty_without_the_golden(self):
        # The fake writes the records only under --index, so a fail test whose
        # document holds any is one the harness never asked for (D20.3).
        path = "fail/mutability/001_extra.ft"
        doc = self.document([path], [], symbols=[self.record(path)])
        self.script("001_extra.ft", ["//@ exit 1", "//@ json " + json.dumps(doc)], "scripted")
        status, lines = self.run_main("--check-json", "001_extra")
        self.assertEqual(status, 1)
        self.assertIn("symbols is not empty", lines[0])

    def document(self, files, diagnostics, **rest):
        doc = {
            "version": 1,
            "files": list(files),
            "diagnostics": list(diagnostics),
            "symbols": [],
        }
        doc.update(rest)
        return doc

    def script(self, name, directives, expected="scripted"):
        path = "fail/mutability/%s" % name
        header = "//! fail\n//! error-any: %s\nfn i32 main() { return 0; }\n" % expected
        write(self.corpus, path, header + "\n".join(directives) + "\n")
        return path


class GoldenIndexLint(unittest.TestCase):
    """The lint of a golden index file, which runs without a compiler (D20.3)."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)
        self.root = Path(self.dir)

    def golden(self, text):
        path = self.root / "run" / "modules" / "t" / run_tests.INDEX_NAME
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return run_tests.golden_index_problems(self.root, path)

    def record(self, **rest):
        rec = {
            "file": "run/modules/t/main.ft",
            "line": 1,
            "col": 8,
            "end_line": 1,
            "end_col": 12,
            "name": "main",
            "kind": "fn",
            "type": "fn () i32",
            "is_decl": True,
            "decl": None,
        }
        rec.update(rest)
        return rec

    def test_a_well_formed_golden_has_no_problem(self):
        self.assertEqual(self.golden(run_tests.render_index([self.record()])), [])

    def test_an_empty_golden_is_a_problem(self):
        self.assertEqual(self.golden(""), ["run/modules/t/index.json: no records"])

    def test_a_line_that_is_not_json_is_reported(self):
        problems = self.golden("{not json}\n")
        self.assertEqual(len(problems), 1)
        self.assertIn("run/modules/t/index.json:1: not one JSON record", problems[0])

    def test_a_missing_key_is_reported(self):
        record = self.record()
        del record["type"]
        problems = self.golden(run_tests.render_index([record]))
        self.assertEqual(len(problems), 1)
        self.assertIn("run/modules/t/index.json:1: keys are", problems[0])

    def test_an_unknown_kind_is_reported(self):
        problems = self.golden(run_tests.render_index([self.record(kind="widget")]))
        self.assertEqual(problems, ["run/modules/t/index.json:1: kind is 'widget'"])

    def test_another_spelling_of_the_same_record_is_reported(self):
        text = json.dumps(self.record(), separators=(",", ":")) + "\n"
        problems = self.golden(text)
        self.assertEqual(
            problems,
            ["run/modules/t/index.json:1: not spelled as the harness writes a record"],
        )

    def test_the_golden_holds_the_records_of_the_test_and_not_the_library(self):
        # Every closure holds std.rt and what it imports (D9.10), so an
        # --index run over any test answers with the standard library's
        # records too. Their file names are the --std-dir the run was given,
        # which is a build directory and differs between machines, so the
        # golden holds the records of the test's own files alone.
        test = run_tests.Test("run/modules/t", "run/modules/t/main.ft", "run")
        mine = self.record()
        library = self.record(file="/vagrant/build/debug/std/libc.ft", name="O_RDONLY")
        other = self.record(file="run/modules/u/main.ft")
        kept = run_tests.index_of_the_test(test, [library, mine, other])
        self.assertEqual(kept, [mine])

    def test_the_golden_is_compared_against_the_filtered_index(self):
        write(self.root, "run/modules/t/main.ft", "//! run\n")
        (self.root / "run" / "modules" / "t" / run_tests.INDEX_NAME).write_text(
            run_tests.render_index([self.record()]), encoding="utf-8"
        )
        test = run_tests.Test("run/modules/t", "run/modules/t/main.ft", "run")
        library = self.record(file="/vagrant/build/debug/std/rt.ft", name="alloc")
        problems = run_tests.golden_index_diff(test, [library, self.record()], self.root)
        self.assertEqual(problems, [])

    def test_a_golden_beside_a_test_is_not_an_unexpected_file(self):
        write(self.root, "run/modules/t/main.ft", "//! run\n")
        (self.root / "run" / "modules" / "t" / run_tests.INDEX_NAME).write_text(
            run_tests.render_index([self.record()]), encoding="utf-8"
        )
        tests, problems = run_tests.discover(self.root)
        self.assertEqual(problems, [])
        self.assertEqual([(t.path, t.golden_index) for t in tests], [("run/modules/t", True)])


if __name__ == "__main__":
    unittest.main()
