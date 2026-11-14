#!/usr/bin/env python3
"""Hold the sites of part 2 of the leak rule in the test trees, and each test tree free of errors.

Part 1 of the leak rule of D17.20 is a compile error at an exit of an owner local and at an exit
of a local of an owning aggregate type, so the build of each test refuses such an exit. Part 2
only reports: `fort --leak-report` implies `--check` and lists each `del` and each owning `cast`
that the analysis cannot prove. This test runs it, for the Linux and the Darwin target, over each
suite of test/fort and test/lsp with the flags of the suite, over a program that imports each
module of test/fort/support, over each program of test/library, and over each entry of test/lang
with the flags of the entry. It holds four things:
- Each run exits 0, but a fail entry of test/lang, which exits 0 or 1. So no error stands in a
  test tree on either target: not in a module that no suite imports, and not in a `$if` block of
  the other target. A drop that misses a release leaves an owner of its value unproved at its
  exit, so this holds each release of each drop of test/fort/support.
- Each line of a report has one of the forms of LINES, so a change of the text of the report turns
  the test red.
- The sites of part 2 in the files below test/ match EXPECTED: a file and a place, with the count
  of its locations. The line of a site is not held, so an edit that moves a line leaves the test
  green. A site that EXPECTED does not give, a count that grows and a site that no report gives
  each turn the test red: a change that ends a site removes it from EXPECTED. A site in src or in
  std belongs to no test of this file.
- A control program gives one site of part 2, so the test cannot pass when it reads no site.

`--root` names the tree that the test reads: its std, its src and its test trees. The harness
module run_tests comes from the tree of this script.
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
sys.path.insert(0, str(HERE / "lang"))
import run_tests  # noqa: E402

# The sites of part 2 in the test trees, relative to the root. Each `del` of a node of a list or a
# tree takes a form of the catalogue (D17.11), so the trees hold none.
EXPECTED = {}

# The entries at the change that added this test, as floors, so that a parallel change that adds
# an entry does not turn the test red, and a pattern that finds none does: the suites of test/fort
# and test/lsp, the modules of test/fort/support, the programs of test/library and the entries of
# test/lang.
FORT_SUITES = 382
LSP_SUITES = 35
SUPPORT_MODULES = 21
LIBRARY_PROGRAMS = 9
LANG_ENTRIES = 981

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))

SITE = re.compile(
    r"^(.*):(\d+):(\d+): leak: cannot prove '(.*)' is empty (?:before this del|before this cast)$"
)

# The lines of a report: a site, its note, and the note of the work limit of part 2
# (src/fort/owner/report.ft, write_inners).
LINES = (
    SITE,
    re.compile(r"^(.*):(\d+):(\d+): note: move '(.*)' into a local and del it first$"),
    re.compile(r"^(.*):(\d+):(\d+): note: the function exceeds the work limit of the leak rule; "
               r"split it$"),
)

# The control: the call of `lend` kills the fact of `c->data`, so the `del` on line 9, column 5,
# is a site of part 2. The program has no error.
CONTROL_NAME = "control.ft"
CONTROL = """struct cell {
    u8 mut@ own data;
}
fn lend(cell mut* c) void {
}
fn main() i32 {
    cell mut* own c = new(cell);
    lend(c);
    del(c);
    return 0;
}
"""
CONTROL_SITES = {(CONTROL_NAME, 9, 5, "c->data")}

# The program that imports each module of test/fort/support, so that the report reads a module
# that no suite imports.
SUPPORT_ENTRY = "support_all.ft"

# The entries of test/library that are no program of its directory programs: the checker reads
# the compiler sources, and chain/main.ft reaches the two modules of the chain.
LIBRARY_ENTRIES = (("checker.ft", True), ("chain/main.ft", False))

OPTIONS = None


def std_root(target, out):
    """The standard root of `target`, as the build assembles it, in `out`."""
    script = os.path.join(OPTIONS.root, "tools", "assemble_std.sh")
    subprocess.run(["bash", script, target, os.path.join(OPTIONS.root, "std"), out],
                   check=True, capture_output=True)
    return out


def support_modules():
    """The module names of test/fort/support of the root, in name order."""
    support = pathlib.Path(OPTIONS.root) / "test" / "fort" / "support"
    return sorted(path.stem for path in support.glob("*.ft"))


def write_support_entry(directory):
    """Writes SUPPORT_ENTRY into `directory`, and gives its flags."""
    with open(os.path.join(directory, SUPPORT_ENTRY), "w") as handle:
        for name in support_modules():
            handle.write("import %s;\n" % name)
        handle.write("\nfn main() i32 {\n    return 0;\n}\n")
    root = pathlib.Path(OPTIONS.root)
    return ["-I", str(root / "src"), "-I", str(root / "test" / "fort" / "support")]


def suites(corpus):
    """The tests of the corpus directory `corpus` with their flags, as run_tests finds them."""
    tests, problems = run_tests.discover(corpus)
    if problems:
        raise AssertionError("%s: %s" % (corpus, problems))
    for test in tests:
        run_tests.load_test(corpus, test)
    return tests


def where(name, cwd):
    """The file of a report line relative to the root, from the directory `cwd` of the run."""
    path = os.path.normpath(os.path.join(cwd, name))
    return os.path.relpath(path, OPTIONS.root)


def report(job):
    """The sites of one report: a set of (file, line, column, place), each file relative to the
    root. A run that exits other than 0, or other than 0 and 1 for a fail entry, and a line that
    has no form of LINES are each an AssertionError."""
    triple, std, cwd, flags, entry, may_fail = job
    argv = [OPTIONS.fort, "--target", triple, "--std-dir", std, "--leak-report"]
    argv += [flag for flag in flags if flag not in run_tests.NOT_WITH_CHECK]
    argv.append(entry)
    # A fail entry of D2.1 holds bytes that are no UTF-8 text, and the compiler shows its line.
    # The decoder gives U+FFFD for each such byte. A line of the report is ASCII text.
    proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0 and not (may_fail and proc.returncode == 1):
        raise AssertionError("%s in %s exits %d: %s"
                             % (argv, cwd, proc.returncode, proc.stderr[-2000:]))
    found = set()
    for line in proc.stdout.splitlines():
        if not any(form.match(line) for form in LINES):
            raise AssertionError("%s in %s writes the line %r" % (argv, cwd, line))
        m = SITE.match(line)
        if m:
            found.add((where(m.group(1), cwd), int(m.group(2)), int(m.group(3)), m.group(4)))
    return found


def jobs_of(triple, std, out):
    """The runs of one target: a list of (triple, std, cwd, flags, entry, may_fail)."""
    root = pathlib.Path(OPTIONS.root)
    jobs = []
    for corpus in (root / "test" / "fort", root / "test" / "lsp"):
        for test in suites(corpus):
            jobs.append((triple, std, str(corpus), test.flags, test.entry, False))
    jobs.append((triple, std, str(root / "test" / "fort"), write_support_entry(out),
                 os.path.join(out, SUPPORT_ENTRY), False))
    library = root / "test" / "library"
    for path in sorted((library / "programs").glob("*.ft")):
        jobs.append((triple, std, str(path.parent), [], path.name, False))
    for name, sources in LIBRARY_ENTRIES:
        path = library / name
        flags = ["-I", str(root / "src")] if sources else []
        jobs.append((triple, std, str(path.parent), flags, path.name, False))
    lang = root / "test" / "lang"
    for test in suites(lang):
        jobs.append((triple, std, str(lang), test.flags, test.entry, test.kind == "fail"))
    return jobs


class LeakSites(unittest.TestCase):
    def test_the_control_gives_one_site_on_each_target(self):
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                control = os.path.join(out, "control")
                os.mkdir(control)
                with open(os.path.join(control, CONTROL_NAME), "w") as handle:
                    handle.write(CONTROL)
                with self.subTest(target=target):
                    found = report((triple, std, control, [], CONTROL_NAME, False))
                    names = set((os.path.basename(site[0]),) + site[1:] for site in found)
                    self.assertEqual(names, CONTROL_SITES)

    def test_the_trees_hold_their_entries(self):
        root = pathlib.Path(OPTIONS.root)
        self.assertGreaterEqual(len(suites(root / "test" / "fort")), FORT_SUITES)
        self.assertGreaterEqual(len(suites(root / "test" / "lsp")), LSP_SUITES)
        self.assertGreaterEqual(len(support_modules()), SUPPORT_MODULES)
        programs = list((root / "test" / "library" / "programs").glob("*.ft"))
        self.assertGreaterEqual(len(programs) + len(LIBRARY_ENTRIES), LIBRARY_PROGRAMS)
        lang = suites(root / "test" / "lang")
        self.assertGreaterEqual(len(lang), LANG_ENTRIES)
        self.assertTrue(any(test.kind == "fail" for test in lang))

    def test_each_tree_gives_the_listed_sites_on_each_target(self):
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                with self.subTest(target=target):
                    found = set()
                    with concurrent.futures.ThreadPoolExecutor(OPTIONS.j) as pool:
                        for sites in pool.map(report, jobs_of(triple, std, out)):
                            found |= sites
                    tests = set(site for site in found if site[0].startswith("test" + os.sep))
                    counts = collections.Counter((site[0], site[3]) for site in tests)
                    self.assertEqual(dict(counts), EXPECTED,
                                     "sites of --leak-report for %s: %s" % (target, sorted(tests)))


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--root", required=True,
                        help="the top of the source tree: its std, its src and its test trees")
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="reports in parallel")
    OPTIONS, rest = parser.parse_known_args()
    OPTIONS.fort = os.path.abspath(OPTIONS.fort)
    OPTIONS.root = os.path.abspath(OPTIONS.root)
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
