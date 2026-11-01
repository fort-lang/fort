#!/usr/bin/env python3
"""Hold the exits of the owning aggregate locals that the leak rule does not prove.

`fort --aggregate-report` lists each exit of a local of an owning aggregate type that part 1 of
the leak rule cannot prove, one site for each location and local. This test runs it over the
compiler (src/fort/main.ft), the language server (src/lsp/main.ft) and a program that imports
each module of the standard library, for the Linux and the Darwin target, and holds the count of
sites of each file against EXPECTED. No other file may have a site. The test trees are outside
it.

The 264 sites of src/fort/driver.ft stand in eight functions that hold a composite context. The
change that migrates them sets EXPECTED to no site, and a change that adds a site elsewhere
turns this test red.
"""

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
import unittest


EXPECTED = {"src/fort/driver.ft": 264}

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))

SITE = re.compile(r"^(.*):(\d+):(\d+): leak: cannot prove '(.*)' is empty at this exit$")

OPTIONS = None


def std_root(target, out):
    """The standard root of `target`, as the build assembles it, in `out`."""
    script = os.path.join(OPTIONS.root, "tools", "assemble_std.sh")
    subprocess.run(["bash", script, target, os.path.join(OPTIONS.root, "std"), out],
                   check=True, capture_output=True)
    return out


def std_program(std, out):
    """A program that imports each module of the standard root `std`, `std.rt` aside."""
    names = []
    for top, _, files in os.walk(std):
        for name in files:
            if name.endswith(".ft"):
                rel = os.path.relpath(os.path.join(top, name), std)[:-len(".ft")]
                if rel != "rt":
                    names.append("std." + rel.replace(os.sep, "."))
    path = os.path.join(out, "std_all.ft")
    with open(path, "w") as handle:
        for name in sorted(names):
            handle.write("import %s;\n" % name)
        handle.write("\nfn main() i32 {\n    return 0;\n}\n")
    return path


def sites(triple, std, entry):
    """The site of each line of the report of `entry`: (file, line, column, place)."""
    argv = [OPTIONS.fort, "--target", triple, "--std-dir", std, "--aggregate-report", "-I",
            "src", entry]
    proc = subprocess.run(argv, cwd=OPTIONS.root, capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError("%s exits %d: %s" % (argv, proc.returncode, proc.stderr[-2000:]))
    found = set()
    for line in proc.stdout.splitlines():
        m = SITE.match(line)
        if not m:
            continue
        path = os.path.normpath(os.path.join(OPTIONS.root, m.group(1)))
        if path.startswith(os.path.normpath(std) + os.sep):
            path = "std/" + os.path.relpath(path, std)
        else:
            path = os.path.relpath(path, OPTIONS.root)
        found.add((path, int(m.group(2)), int(m.group(3)), m.group(4)))
    return found


class AggregateSites(unittest.TestCase):
    def test_each_target_gives_the_expected_count_of_each_file(self):
        for target, triple in TARGETS:
            with self.subTest(target=target), tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                found = set()
                for entry in ("src/fort/main.ft", "src/lsp/main.ft", std_program(std, out)):
                    found |= sites(triple, std, entry)
                counts = collections.Counter(site[0] for site in found)
                self.assertEqual(dict(counts), EXPECTED,
                                 "sites by file for %s: %s" % (target, sorted(counts.items())))


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--root", required=True, help="the top of the source tree")
    OPTIONS, rest = parser.parse_known_args()
    OPTIONS.fort = os.path.abspath(OPTIONS.fort)
    OPTIONS.root = os.path.abspath(OPTIONS.root)
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
