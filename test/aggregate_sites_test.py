#!/usr/bin/env python3
"""Hold the exits of the owning aggregate locals that the leak rule does not prove.

`fort --aggregate-report` lists each exit of a local of an owning aggregate type that part 1 of
the leak rule cannot prove, one site for each location and local. This test runs it over the
compiler (src/fort/main.ft), the language server (src/lsp/main.ft) and a program that imports
each module of the standard library, for the Linux and the Darwin target, and holds the count of
sites of each file against EXPECTED. No file may have a site that EXPECTED does not give. The
test trees are outside it.

EXPECTED gives no site. So the test also holds the report itself: each line of a report must have
one of the forms of LINES, and a control program must give the one site of the module that it
imports and no other. A change of the text of the report then turns this test red, and it cannot
pass with no site read, or with a report of the entry module alone.
"""

import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile
import unittest


EXPECTED = {}

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))

SITE = re.compile(r"^(.*):(\d+):(\d+): leak: cannot prove '(.*)' is empty at this exit$")

# The other lines of a report: the note of each site, the note of the work limit, and the line of
# a cause (src/fort/owner/report.ft, write_aggregate_lines).
LINES = (
    SITE,
    re.compile(r"^(.*):(\d+):(\d+): note: del or move '(.*)' before this exit$"),
    re.compile(r"^(.*):(\d+):(\d+): note: the function exceeds the aggregate work limit; split it$"),
    re.compile(r"^(.*):(\d+):(\d+): note: cause [1-4]: .+$"),
)

# The control: an entry module with no site, and a module that it imports. In `held`, `b` is a
# memory local, and the call kills the fact of `b.data`, so the exit at the `return` on line 9,
# column 5, of the imported module is a site.
CONTROL_MAIN = """import control_inner;
fn main() i32 {
    return control_inner.held();
}
"""
CONTROL_INNER = """struct buf {
    u8 mut@ own data;
}
fn fill(buf mut* b) void {
}
fn held() i32 {
    buf mut b = buf{};
    fill(&b);
    return 0;
}
"""
CONTROL_SITE = (9, 5, "b.data")

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


def report(triple, std, entry):
    """The site of each line of the report of `entry`: (file as printed, line, column, place).
    Each line that has no form of LINES fails the test."""
    argv = [OPTIONS.fort, "--target", triple, "--std-dir", std, "--aggregate-report", "-I",
            "src", entry]
    proc = subprocess.run(argv, cwd=OPTIONS.root, capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError("%s exits %d: %s" % (argv, proc.returncode, proc.stderr[-2000:]))
    found = set()
    for line in proc.stdout.splitlines():
        if not any(form.match(line) for form in LINES):
            raise AssertionError("a line of the report of %s has no known form: %r"
                                 % (entry, line))
        m = SITE.match(line)
        if m:
            found.add((m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)))
    return found


def sites(triple, std, entry):
    """The sites of the report of `entry`, each file relative to the root or as `std/<path>`."""
    found = set()
    for file, line, col, place in report(triple, std, entry):
        path = os.path.normpath(os.path.join(OPTIONS.root, file))
        if path.startswith(os.path.normpath(std) + os.sep):
            path = "std/" + os.path.relpath(path, std)
        else:
            path = os.path.relpath(path, OPTIONS.root)
        found.add((path, line, col, place))
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

    def test_the_control_gives_the_one_site_of_its_import(self):
        for target, triple in TARGETS:
            with self.subTest(target=target), tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                control = os.path.join(out, "control.ft")
                inner = os.path.join(out, "control_inner.ft")
                for path, text in ((control, CONTROL_MAIN), (inner, CONTROL_INNER)):
                    with open(path, "w") as handle:
                        handle.write(text)
                found = {(os.path.realpath(os.path.join(OPTIONS.root, file)), line, col, place)
                         for file, line, col, place in report(triple, std, control)}
                self.assertEqual(found, {(os.path.realpath(inner),) + CONTROL_SITE})


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
