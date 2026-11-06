#!/usr/bin/env python3
"""Hold the sites of the leak rule in the tests of the library builds.

The tests of the library builds are the suites test/fort/library/*_test.ft and the programs of
test/library: the checker of the interface test, the programs of the artifact and use tests, and
the chain of the cache test. `fort --aggregate-report` lists each exit of a local of an owning
aggregate type that part 1 of the leak rule cannot prove, and `fort --leak-report` lists each
`del` that part 2 cannot prove. This test runs both reports over each suite with the flags of the
suite, and over each program of test/library from its own directory, for the Linux and the Darwin
target. It holds the sites in the files of test/fort/library and test/library against EXPECTED,
which gives none. A site in a support module, in src or in std belongs to the test of that tree.

A control program gives one site of each report in a file of the scope. The test holds the
control too, so it cannot pass when it reads no site.
"""

import argparse
import collections
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
FORT_TESTS = HERE / "fort"
sys.path.insert(0, str(HERE / "lang"))
import run_tests  # noqa: E402

EXPECTED = {"aggregate": {}, "leak": {}}

# A suite of the scope, relative to test/fort, and a file of the scope, relative to test/.
SUITE = re.compile(r"^library/[^/]*_test\.ft$")
SCOPE = re.compile(r"^(fort/library|library)/.*\.ft$")

# The entries of test/library, each compiled from its own directory with its own flags: the
# checker reads the compiler sources, and chain/main.ft reaches the two modules of the chain.
PROGRAMS = (("checker.ft", True), ("chain/main.ft", False))

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))

SITE = re.compile(
    r"^(.*):(\d+):(\d+): leak: cannot prove '(.*)' is empty "
    r"(?:at this exit|before this del|before this cast)$"
)

# The control: in `held`, `b` is a memory local and the call kills the fact of `b.data`, so the
# exit on line 9 is a site of part 1. In `freed`, the call kills the fact of `c->data`, so the
# `del` on line 19 is a site of part 2.
CONTROL_NAME = "library/control.ft"
CONTROL = """struct buf {
    u8 mut@ own data;
}
fn fill(buf mut* b) void {
}
fn held() i32 {
    buf mut b = buf{};
    fill(&b);
    return 0;
}
struct cell {
    u8 mut@ own data;
}
fn lend(cell mut* c) void {
}
fn freed() i32 {
    cell mut* own c = new(cell);
    lend(c);
    del(c);
    return 0;
}
fn main() i32 {
    return held() + freed();
}
"""
CONTROL_SITES = {
    "aggregate": {(CONTROL_NAME, 9, 5, "b.data")},
    "leak": {(CONTROL_NAME, 19, 5, "c->data")},
}

OPTIONS = None


def std_root(target, out):
    """The standard root of `target`, as the build assembles it, in `out`."""
    script = os.path.join(OPTIONS.root, "tools", "assemble_std.sh")
    subprocess.run(["bash", script, target, os.path.join(OPTIONS.root, "std"), out],
                   check=True, capture_output=True)
    return out


def report(job):
    """The sites of one report of one entry in the scope: a set of (file, line, column, place).
    The file is relative to `base`, the test directory of the tree."""
    kind, triple, std, cwd, base, flags, entry = job
    argv = [OPTIONS.fort, "--target", triple, "--std-dir", std, "--%s-report" % kind]
    argv += [flag for flag in flags if flag not in run_tests.NOT_WITH_CHECK]
    argv.append(entry)
    proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError("%s exits %d: %s" % (argv, proc.returncode, proc.stderr[-2000:]))
    found = set()
    for line in proc.stdout.splitlines():
        m = SITE.match(line)
        if m:
            name = os.path.relpath(os.path.normpath(os.path.join(cwd, m.group(1))), base)
            if SCOPE.match(name):
                found.add((name, int(m.group(2)), int(m.group(3)), m.group(4)))
    return found


def entries():
    """The suites of test/fort/library and the programs of test/library: a list of (directory,
    flags, entry)."""
    tests, problems = run_tests.discover(FORT_TESTS)
    if problems:
        raise AssertionError("test/fort: %s" % problems)
    suites = [test for test in tests if SUITE.match(test.path)]
    for test in suites:
        run_tests.load_test(FORT_TESTS, test)
    found = [(str(FORT_TESTS), test.flags, test.entry) for test in suites]
    library = HERE / "library"
    programs = sorted((library / "programs").glob("*.ft"))
    for path in programs:
        found.append((str(path.parent), [], path.name))
    for name, sources in PROGRAMS:
        path = library / name
        flags = ["-I", os.path.join(OPTIONS.root, "src")] if sources else []
        found.append((str(path.parent), flags, path.name))
    return found, len(suites), len(programs)


class AggregateLibrary(unittest.TestCase):
    def test_the_control_gives_one_site_of_each_report(self):
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                base = os.path.join(out, "test")
                control = os.path.join(base, "library")
                os.makedirs(control)
                with open(os.path.join(base, CONTROL_NAME), "w") as handle:
                    handle.write(CONTROL)
                for kind in ("aggregate", "leak"):
                    with self.subTest(target=target, report=kind):
                        found = report((kind, triple, std, control, base, [],
                                        os.path.basename(CONTROL_NAME)))
                        self.assertEqual(found, CONTROL_SITES[kind])

    def test_each_report_gives_no_site_in_the_scope_on_each_target(self):
        jobs, suites, programs = entries()
        # the counts at the change that added this test, as floors: a parallel change that adds
        # an entry of the scope does not turn the test red
        self.assertGreaterEqual(suites, 21)
        self.assertGreaterEqual(programs, 7)
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                for kind in ("aggregate", "leak"):
                    with self.subTest(target=target, report=kind):
                        work = [(kind, triple, std, cwd, str(HERE), flags, entry)
                                for cwd, flags, entry in jobs]
                        found = set()
                        with concurrent.futures.ThreadPoolExecutor(OPTIONS.j) as pool:
                            for sites in pool.map(report, work):
                                found |= sites
                        counts = collections.Counter((site[0], site[3]) for site in found)
                        self.assertEqual(dict(counts), EXPECTED[kind],
                                         "sites of --%s-report for %s: %s"
                                         % (kind, target, sorted(found)))


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--root", required=True, help="the top of the source tree")
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="reports in parallel")
    OPTIONS, rest = parser.parse_known_args()
    OPTIONS.fort = os.path.abspath(OPTIONS.fort)
    OPTIONS.root = os.path.abspath(OPTIONS.root)
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
