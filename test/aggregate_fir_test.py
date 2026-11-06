#!/usr/bin/env python3
"""Hold the sites of the leak rule in the suites of the FIR.

The suites of the FIR are the files test/fort/fir_*test.ft. `fort --aggregate-report` lists each
exit of a local of an owning aggregate type that part 1 of the leak rule cannot prove, and
`fort --leak-report` lists each `del` that part 2 cannot prove. This test runs both reports over
each of those suites, with the flags of the suite, for the Linux and the Darwin target. It holds
the sites in the files of the suites against EXPECTED, which gives none. A site in a support
module or in src belongs to the test of that tree.

Some suites hold drops of their own (the `drop` and `drop_made` of an environment, a run of the
tokenizer or a fixture). A drop that misses a release leaves an owner of its value unproved at its
exit, and that exit is a site. So this test also holds each release of those drops. The suites
end the environments of support/fir_env.ft, lower_env.ft and llvm_env.ft through their `drop` and
`drop_made`, which reach the `drop` of support/check_env.ft and modules_env.ft, and no heap probe
holds those ends. So the test holds `drop` and `drop_made` of those five modules too. The other
functions of the support modules are outside it.

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

# A suite of the scope, and a file of the scope, relative to test/fort.
SUITE = re.compile(r"^fir_[^/]*_test\.ft$")
SCOPE = re.compile(r"^fir_[^/]*\.ft$")

# The support modules whose drops the suites call, and the functions of them in the scope.
SUPPORT = ("support/fir_env.ft", "support/lower_env.ft", "support/llvm_env.ft",
           "support/check_env.ft", "support/modules_env.ft")
DROPS = ("drop", "drop_made")
FN = re.compile(r"^fn (\w+)\(")

# The count of the suites of the scope at 0e8e954f. It is a floor, so a parallel change that adds
# a suite does not turn this test red, and a pattern that finds no suite does.
SUITES = 125

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))

SITE = re.compile(
    r"^(.*):(\d+):(\d+): leak: cannot prove '(.*)' is empty "
    r"(?:at this exit|before this del|before this cast)$"
)

# The control: in `held`, `b` is a memory local and the call kills the fact of `b.data`, so the
# exit on line 9 is a site of part 1. In `freed`, the call kills the fact of `c->data`, so the
# `del` on line 19 is a site of part 2.
CONTROL_NAME = "fir_control_test.ft"
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


def function_at(path, line):
    """The name of the function of `path` that holds `line`, or None before the first one."""
    with open(path) as handle:
        lines = handle.read().split("\n")
    for k in range(min(line, len(lines)) - 1, -1, -1):
        m = FN.match(lines[k])
        if m:
            return m.group(1)
    return None


def in_scope(cwd, name, line):
    """Whether the site at `line` of `name`, relative to `cwd`, is in the scope."""
    if SCOPE.match(name):
        return True
    return name in SUPPORT and function_at(os.path.join(cwd, name), line) in DROPS


def report(job):
    """The sites of one report of one file in the scope: a set of (file, line, column, place)."""
    kind, triple, std, cwd, flags, entry = job
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
            name = os.path.relpath(os.path.normpath(os.path.join(cwd, m.group(1))), cwd)
            if in_scope(cwd, name, int(m.group(2))):
                found.add((name, int(m.group(2)), int(m.group(3)), m.group(4)))
    return found


class AggregateFir(unittest.TestCase):
    def test_the_control_gives_one_site_of_each_report(self):
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                control = os.path.join(out, "control")
                os.mkdir(control)
                with open(os.path.join(control, CONTROL_NAME), "w") as handle:
                    handle.write(CONTROL)
                for kind in ("aggregate", "leak"):
                    with self.subTest(target=target, report=kind):
                        found = report((kind, triple, std, control, [], CONTROL_NAME))
                        self.assertEqual(found, CONTROL_SITES[kind])

    def test_each_report_gives_no_site_in_the_suites_on_each_target(self):
        tests, problems = run_tests.discover(FORT_TESTS)
        self.assertEqual(problems, [])
        suites = [test for test in tests if SUITE.match(test.path)]
        for test in suites:
            run_tests.load_test(FORT_TESTS, test)
        self.assertGreaterEqual(len(suites), SUITES)
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                for kind in ("aggregate", "leak"):
                    with self.subTest(target=target, report=kind):
                        jobs = [(kind, triple, std, str(FORT_TESTS), test.flags, test.entry)
                                for test in suites]
                        found = set()
                        with concurrent.futures.ThreadPoolExecutor(OPTIONS.j) as pool:
                            for sites in pool.map(report, jobs):
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
