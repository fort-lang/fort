#!/usr/bin/env python3
"""Test the library options of test/lang/run_tests.py: `--use` and `--cache-dir` (D9.11)."""

import argparse
import contextlib
import io
import os
from pathlib import Path
import sys
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


if __name__ == "__main__":
    unittest.main()
