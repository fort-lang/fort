#!/usr/bin/env python3
"""Check the deepest nesting of D2.11 under a small stack in each compiler given.

The checker recurses once for each group of a written type. A type of 255
nested groups, `(...((i32*)*)...*)`, therefore holds 255 checker frames. Each
compiler must check that type with a stack of STACK_LIMIT bytes. The test sets
the limit in the child before exec, so the main thread of the compiler gets
that stack on linux and on darwin.
"""

import argparse
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import unittest


OPTIONS = None

# The stack that each compiler gets. With 6 KB of suffixes in each checker
# frame, the 254 groups needed 1792 KB in a debug build on darwin. With the
# suffixes on the checker's own stack, the C compiler needs 160 KB and the fort
# compiler needs 128 KB.
STACK_LIMIT = 1024 * 1024

# The groups of the deepest type: the block of `main` and 255 parentheses make
# 256 levels of nesting, at the limit of 256. The type itself is no level.
GROUPS = 255


# The function types of the deepest chain. A function type is one level, so a
# global of 256 chained function types stands at the limit of 256.
FUNCTIONS = 256


def chained_functions(count):
    """A global of `count` function types, each the return type of the one before."""
    return "fn () " * count + "i32 p = null;\n"


def nested_groups(count):
    """A type of `count` groups, each with one `*` suffix."""
    text = "i32"
    for _ in range(count):
        text = "(" + text + "*)"
    return text


def small_stack():
    _, hard = resource.getrlimit(resource.RLIMIT_STACK)
    limit = STACK_LIMIT
    if hard != resource.RLIM_INFINITY and hard < limit:
        limit = hard
    resource.setrlimit(resource.RLIMIT_STACK, (limit, hard))


class StackDepthTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fort-stack-depth-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.standard = self.root / "std"
        self.standard.mkdir()
        (self.standard / "rt.ft").write_text("// Empty fixture runtime.\n")

    def check(self, fort, type_text):
        return self.check_source(
            fort, "fn main() i32 {\n    " + type_text + " p = null;\n    return 0;\n}\n")

    def check_source(self, fort, source):
        entry = self.root / "main.ft"
        entry.write_text(source)
        return subprocess.run(
            [fort, "--std-dir", str(self.standard), "--check", str(entry)],
            cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            timeout=120, preexec_fn=small_stack,
        )

    def test_the_deepest_groups_check_under_a_small_stack(self):
        type_text = nested_groups(GROUPS)
        self.assertEqual(type_text.count("("), GROUPS)
        self.assertEqual(type_text.count("*"), GROUPS)
        for fort in OPTIONS.fort:
            with self.subTest(fort=fort):
                result = self.check(fort, type_text)
                self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
                self.assertEqual(result.stderr, b"")

    def test_a_group_too_deep_is_refused_under_a_small_stack(self):
        # The block of `main` is the first level, so the 256th group opens the
        # 257th level. The refusal must be a diagnostic and not a crash.
        type_text = nested_groups(GROUPS + 1)
        for fort in OPTIONS.fort:
            with self.subTest(fort=fort):
                result = self.check(fort, type_text)
                self.assertEqual(result.returncode, 1, result.stderr.decode("utf-8", "replace"))
                self.assertIn(b"error: nesting deeper than 256", result.stderr)


    def test_the_deepest_function_chain_checks_under_a_small_stack(self):
        main = "fn main() i32 {\n    return 0;\n}\n"
        for fort in OPTIONS.fort:
            with self.subTest(fort=fort):
                result = self.check_source(fort, chained_functions(FUNCTIONS) + main)
                self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", "replace"))
                self.assertEqual(result.stderr, b"")
                # One more function type is the 257th level.
                result = self.check_source(fort, chained_functions(FUNCTIONS + 1) + main)
                self.assertEqual(result.returncode, 1, result.stderr.decode("utf-8", "replace"))
                self.assertIn(b"error: nesting deeper than 256", result.stderr)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", action="append", required=True,
                        help="a compiler to check; repeatable")
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.fort = [str(Path(fort).resolve()) for fort in OPTIONS.fort]
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
