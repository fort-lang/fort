#!/usr/bin/env python3
"""Hold the runs of tools/fort_lint.py over a list of files of `std/`.

The lint checks a module of `std/` as `std.<name>` of the standard root of its target, which
symbolic links to the files of `std/` make. A list may name one module alone, a module of a target
directory alone, or the modules of both targets in one run (notes/style.md 3). Each run below
names its files, as a person runs the lint over one file. The last case plants one bad name in a
module of each kind in a copy of `std/`, so a run that judges no record of a file fails it.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


OPTIONS = None

CLEAN = re.compile(r"^fort_lint: (\d+) file\(s\), no violation of D1\.4$")


def lint(root, files, target="linux"):
    """Run the lint over `files`, paths below `root`, and return (status, output)."""
    argv = [sys.executable, os.path.join(OPTIONS.source, "tools", "fort_lint.py"), "--fort",
            OPTIONS.fort, "--root", root, "--target", target] + files
    proc = subprocess.run(argv, cwd=root, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def std_files(root):
    """Each `.ft` of `std/` below `root`, both target directories included, sorted."""
    found = []
    for top, _, names in os.walk(os.path.join(root, "std")):
        for name in names:
            if name.endswith(".ft"):
                found.append(os.path.relpath(os.path.join(top, name), root))
    return sorted(found)


class LintOfStd(unittest.TestCase):
    def assert_clean(self, files, target="linux"):
        status, output = lint(OPTIONS.source, files, target)
        self.assertEqual(status, 0, output)
        last = output.strip().splitlines()[-1]
        match = CLEAN.match(last)
        self.assertIsNotNone(match, output)
        self.assertEqual(int(match.group(1)), len(files), output)

    def test_one_shared_module_alone(self):
        self.assert_clean(["std/sort.ft"])

    def test_one_module_of_each_target_directory_alone(self):
        for target in ("linux", "darwin"):
            with self.subTest(target=target):
                self.assert_clean(["std/%s/libc.ft" % target], target)
                self.assert_clean(["std/%s/libc.ft" % target])

    def test_the_modules_of_both_targets_in_one_run(self):
        files = std_files(OPTIONS.source)
        self.assertIn("std/linux/libc.ft", files)
        self.assertIn("std/darwin/libc.ft", files)
        self.assert_clean(files)

    def test_a_bad_name_in_each_kind_of_module_is_reported(self):
        with tempfile.TemporaryDirectory() as out:
            root = os.path.join(os.path.realpath(out), "tree")
            shutil.copytree(os.path.join(OPTIONS.source, "std"), os.path.join(root, "std"))
            planted = ("std/sort.ft", "std/linux/os.ft", "std/darwin/os.ft")
            for rel in planted:
                with open(os.path.join(root, rel), "a", encoding="utf-8") as handle:
                    handle.write("\nfn BadName() void {\n}\n")
            status, output = lint(root, list(planted))
            self.assertEqual(status, 1, output)
            for rel in planted:
                self.assertRegex(output, re.escape(rel) + r":\d+:4: fn 'BadName' is not "
                                 r"lower_case \(D1\.4\)")


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--root", required=True, help="the top of the source tree")
    OPTIONS, rest = parser.parse_known_args()
    OPTIONS.fort = os.path.abspath(OPTIONS.fort)
    OPTIONS.source = os.path.abspath(OPTIONS.root)
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
