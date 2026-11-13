#!/usr/bin/env python3
"""Test the library options of test/lang/run_tests.py, `--use` and `--cache-dir` (D9.11), the
flags that its check command leaves out, and the count of `llvm.memcpy` under `--verify-ir`."""

import argparse
import contextlib
import io
import os
from pathlib import Path
import stat
import sys
import tempfile
import types
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY / "test" / "lang"))
import run_tests  # noqa: E402

TEST = types.SimpleNamespace(flags=["-I", "../../src"], entry="ast_test.ft")


def config(uses=(), cache_dir=""):
    return run_tests.Config(root=REPOSITORY, fort="/bin/fort", std_dir="/std",
                            uses=list(uses), cache_dir=cache_dir)


class LibraryOptionsTest(unittest.TestCase):
    def test_no_library_adds_nothing(self):
        self.assertEqual(run_tests.compile_command(config(), TEST, "out"),
                         ["/bin/fort", "--cc", run_tests.DEFAULT_CC, "--std-dir", "/std",
                          "-I", "../../src", "-o", "out", "ast_test.ft"])
        self.assertEqual(run_tests.check_command(config(), TEST, False),
                         ["/bin/fort", "--check", "--std-dir", "/std", "-I", "../../src",
                          "ast_test.ft"])

    def test_a_build_gets_each_use_and_the_cache(self):
        c = config(["std=/a/std.flib", "fort=/a/fort.flib"], "/a/cache")
        self.assertEqual(run_tests.compile_command(c, TEST, "out", emit_ir=True),
                         ["/bin/fort", "--cc", run_tests.DEFAULT_CC, "--std-dir", "/std",
                          "--use", "std=/a/std.flib", "--use", "fort=/a/fort.flib",
                          "--cache-dir", "/a/cache", "-I", "../../src", "-S", "-o", "out",
                          "ast_test.ft"])

    def test_a_check_gets_each_use_and_no_cache(self):
        c = config(["std=/a/std.flib"], "/a/cache")
        self.assertEqual(run_tests.check_command(c, TEST, True),
                         ["/bin/fort", "--check", "--std-dir", "/std", "--use", "std=/a/std.flib",
                          "--json", "-I", "../../src", "ast_test.ft"])

    def test_a_cache_without_a_use_adds_nothing(self):
        self.assertEqual(run_tests.library_options(config([], "/a/cache"), True), [])

    def test_use_takes_name_and_an_absolute_path(self):
        self.assertEqual(run_tests.library_use("std=lib/std.flib"),
                         "std=" + os.path.abspath("lib/std.flib"))
        self.assertEqual(run_tests.library_use("a=b=c"), "a=" + os.path.abspath("b=c"))
        for bad in ("std", "=x", "std=", ""):
            with self.subTest(bad=bad):
                with self.assertRaises(argparse.ArgumentTypeError):
                    run_tests.library_use(bad)

    def test_the_command_line_repeats_use(self):
        args = run_tests.parse_args(["--use", "std=s", "--use", "fort=f", "--cache-dir", "c"])
        self.assertEqual(args.use, ["std=" + os.path.abspath("s"), "fort=" + os.path.abspath("f")])
        self.assertEqual(args.cache_dir, "c")

    def test_a_bad_use_is_a_usage_error(self):
        with contextlib.redirect_stderr(io.StringIO()) as err:
            with self.assertRaises(SystemExit) as caught:
                run_tests.parse_args(["--use", "std"])
        self.assertEqual(caught.exception.code, 2)
        self.assertIn("expected NAME=PATH, got 'std'", err.getvalue())

    def test_cache_dir_requires_use(self):
        with self.assertRaises(SystemExit) as caught:
            run_tests.main(["--cache-dir", "c", "--list"])
        self.assertEqual(str(caught.exception), "run_tests.py: error: --cache-dir requires --use")


class CheckCommandTest(unittest.TestCase):
    FIR = types.SimpleNamespace(flags=["--fir", "-I", "../../src"], entry="t.ft")

    def test_a_check_leaves_out_the_flag_of_fir(self):
        self.assertEqual(run_tests.check_command(config(), self.FIR, False),
                         ["/bin/fort", "--check", "--std-dir", "/std", "-I", "../../src", "t.ft"])
        self.assertEqual(run_tests.check_command(config(), self.FIR, True, True),
                         ["/bin/fort", "--check", "--std-dir", "/std", "--json", "--index", "-I",
                          "../../src", "t.ft"])

    def test_a_build_keeps_the_flag_of_fir(self):
        self.assertEqual(run_tests.compile_command(config(), self.FIR, "out"),
                         ["/bin/fort", "--cc", run_tests.DEFAULT_CC, "--std-dir", "/std", "--fir",
                          "-I", "../../src", "-o", "out", "t.ft"])

    def test_only_the_flag_of_fir_stays_out(self):
        self.assertEqual(run_tests.NOT_WITH_CHECK, ("--fir",))
        other = types.SimpleNamespace(flags=["--fir-after=lower", "--release"], entry="t.ft")
        self.assertEqual(run_tests.check_command(config(), other, False)[-3:],
                         ["--fir-after=lower", "--release", "t.ft"])


MEMMOVE_CALL = ("  call void @llvm.memmove.p0.p0.i64(ptr align 8 %t1, ptr align 8 %t0, "
                "i64 16, i1 false)")
MEMCPY_CALL = ("  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %t1, ptr align 8 %t0, "
               "i64 16, i1 false)")
MEMCPY_DECLARATION = ("declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture writeonly, "
                      "ptr noalias nocapture readonly, i64, i1 immarg) #5")
MEMMOVE_DECLARATION = ("declare void @llvm.memmove.p0.p0.i64(ptr nocapture writeonly, "
                       "ptr nocapture readonly, i64, i1 immarg) #5")
# A string constant whose bytes are the text of a call of `llvm.memcpy`.
MEMCPY_STRING = ('@.str.0 = private unnamed_addr constant [26 x i8] '
                 'c"call void @llvm.memcpy.p0("')
# The module of a program whose copy is `llvm.memmove`: the lines in the order of a real module.
CLEAN_MODULE = [
    MEMCPY_STRING,
    'define void @"main.through"(ptr %to.in, ptr %from.in) #0 {',
    "entry:",
    MEMMOVE_CALL,
    "  call void @memcpy(ptr %t1, ptr %t0, i64 16) #3",
    "  ret void",
    "}",
    "declare ptr @memcpy(ptr, ptr, i64) nobuiltin",
    MEMMOVE_DECLARATION,
]

# A compiler that copies the file `$FAKE_MODULE` to the path after `-o`, and exits `$FAKE_EXIT`.
FAKE_FORT = """#!/bin/sh
out=
while [ $# -gt 0 ]; do
    if [ "$1" = "-o" ]; then
        out=$2
    fi
    shift
done
if [ -n "$FAKE_MODULE" ]; then
    cp "$FAKE_MODULE" "$out"
fi
exit "${FAKE_EXIT:-0}"
"""
# A verifier that reads nothing and exits `$FAKE_OPT_EXIT`.
FAKE_OPT = """#!/bin/sh
echo "fake opt: the module is bad" >&2
exit "${FAKE_OPT_EXIT:-0}"
"""


class MemcpyLinesTest(unittest.TestCase):
    def test_a_call_and_a_declaration_count(self):
        self.assertEqual(run_tests.memcpy_lines(MEMCPY_CALL + "\n"), [MEMCPY_CALL])
        self.assertEqual(run_tests.memcpy_lines(MEMCPY_DECLARATION + "\n"), [MEMCPY_DECLARATION])
        text = "\n".join([MEMCPY_CALL, MEMMOVE_CALL, MEMCPY_CALL, MEMCPY_DECLARATION])
        self.assertEqual(run_tests.memcpy_lines(text),
                         [MEMCPY_CALL, MEMCPY_CALL, MEMCPY_DECLARATION])

    def test_each_form_of_a_call_counts(self):
        for line in ("  tail call void @llvm.memcpy.p0.p0.i64(ptr %a, ptr %b, i64 8, i1 false)",
                     "  call void @llvm.memcpy.p0.p0.i32(ptr %a, ptr %b, i32 8, i1 false)",
                     "  call void @llvm.memcpy.inline.p0.p0.i64(ptr %a, ptr %b, i64 8, i1 false)",
                     "\tcall void @llvm.memcpy.p0.p0.i64(ptr %a, ptr %b, i64 8, i1 false) #5"):
            with self.subTest(line=line):
                self.assertEqual(run_tests.memcpy_lines(line), [line])

    def test_a_memmove_does_not_count(self):
        self.assertEqual(run_tests.memcpy_lines(MEMMOVE_CALL), [])
        self.assertEqual(run_tests.memcpy_lines(MEMMOVE_DECLARATION), [])
        self.assertEqual(run_tests.memcpy_lines(
            "  call void @llvm.memset.p0.i64(ptr align 8 %x.2, i8 0, i64 16, i1 false)"), [])

    def test_the_memcpy_of_c_does_not_count(self):
        # an extern function of the program: no intrinsic
        self.assertEqual(run_tests.memcpy_lines("declare ptr @memcpy(ptr, ptr, i64) nobuiltin"), [])
        self.assertEqual(run_tests.memcpy_lines(
            "  %t3 = call ptr @memcpy(ptr %t0, ptr %t1, i64 %t2) #3"), [])

    def test_the_bytes_of_a_string_constant_do_not_count(self):
        self.assertEqual(run_tests.memcpy_lines(MEMCPY_STRING), [])

    def test_a_clean_module_and_an_empty_text_have_none(self):
        self.assertEqual(run_tests.memcpy_lines("\n".join(CLEAN_MODULE) + "\n"), [])
        self.assertEqual(run_tests.memcpy_lines(""), [])


class VerifyModuleTest(unittest.TestCase):
    """verify_module against a fake compiler and a fake verifier."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory(prefix="fort-harness-test-")
        self.addCleanup(self.dir.cleanup)
        root = Path(self.dir.name)
        self.fort = self.script(root / "fort", FAKE_FORT)
        self.opt = self.script(root / "opt", FAKE_OPT)
        self.workdir = root / "work"
        self.workdir.mkdir()
        self.source = root / "module.ll"
        self.config = run_tests.Config(root=root, fort=self.fort, std_dir="/std", opt=self.opt,
                                       timeout=30.0)

    @staticmethod
    def script(path, text):
        path.write_text(text, encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return str(path)

    def verify(self, lines, **more):
        self.source.write_text("\n".join(lines) + "\n", encoding="utf-8")
        env = dict(os.environ, FAKE_MODULE=str(self.source), **more)
        procs = []
        test = types.SimpleNamespace(flags=[], entry="t.ft")
        result = run_tests.verify_module(self.config, test, str(self.workdir), env, procs)
        return result, procs

    def test_a_module_with_memmove_only_passes(self):
        result, procs = self.verify(CLEAN_MODULE)
        self.assertEqual(result, (None, ""))
        # the compiler ran with `-S`, then the verifier ran
        self.assertEqual(len(procs), 2)

    def test_a_call_of_memcpy_fails_the_module(self):
        lines = list(CLEAN_MODULE)
        lines[3] = MEMCPY_CALL
        (verdict, reason), _ = self.verify(lines)
        self.assertEqual(verdict, "FAIL")
        self.assertEqual(reason, "the module holds llvm.memcpy 1 times (D19.3): "
                         + MEMCPY_CALL.strip())

    def test_a_declaration_of_memcpy_fails_the_module(self):
        (verdict, reason), _ = self.verify(CLEAN_MODULE + [MEMCPY_DECLARATION])
        self.assertEqual(verdict, "FAIL")
        self.assertIn("llvm.memcpy 1 times (D19.3): declare void @llvm.memcpy.", reason)

    def test_the_reason_gives_the_count_and_the_first_line(self):
        lines = CLEAN_MODULE + [MEMCPY_CALL, MEMCPY_CALL, MEMCPY_DECLARATION]
        (verdict, reason), _ = self.verify(lines)
        self.assertEqual(verdict, "FAIL")
        self.assertEqual(reason, "the module holds llvm.memcpy 3 times (D19.3): "
                         + MEMCPY_CALL.strip())

    def test_a_module_that_the_verifier_rejects_keeps_its_reason(self):
        (verdict, reason), _ = self.verify(CLEAN_MODULE + [MEMCPY_CALL], FAKE_OPT_EXIT="1")
        self.assertEqual(verdict, "FAIL")
        self.assertEqual(reason, "the IR verifier rejected the module: "
                         "fake opt: the module is bad")

    def test_a_compiler_error_under_s_keeps_its_reason(self):
        (verdict, reason), procs = self.verify(CLEAN_MODULE + [MEMCPY_CALL], FAKE_EXIT="1")
        self.assertEqual(verdict, "FAIL")
        self.assertTrue(reason.startswith("-S: compiler exited 1"), reason)
        self.assertEqual(len(procs), 1)


if __name__ == "__main__":
    unittest.main()
