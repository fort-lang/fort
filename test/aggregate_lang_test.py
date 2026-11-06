#!/usr/bin/env python3
"""Hold the sites of the leak rule that the language corpus keeps.

`fort --aggregate-report` lists each exit of a local of an owning aggregate type that part 1 of
the leak rule cannot prove, and `fort --leak-report` lists each `del` that part 2 cannot prove.
This test runs both reports over each entry of test/lang, with the flags of the entry, for the
Linux and the Darwin target. test/lang/run_tests.py finds the entries and their flags. The test
holds the sites of each report against its list below. A site is a file and a place, with the
count of its locations, and the line of a site is not held, so an edit that moves a line leaves
the test green. A site that is not in the list, a count that grows and a listed site that no
report gives each turn the test red: a change that ends a site removes it from the list.

The aggregate sites: fail/ownership/083 keeps 2, because a `del` there moves the lines that its
golden pins. The sites of part 2 wait for their own migration.
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
LANG = HERE / "lang"
sys.path.insert(0, str(LANG))
import run_tests  # noqa: E402

EXPECTED = {
    "aggregate": {
        ("fail/ownership/083_use_after_a_del_of_another_place.ft", "t.arr[0].a"): 2,
    },
    "leak": {
        ("programs/binary_tree.ft", "(*out)->left"): 1,
        ("programs/binary_tree.ft", "heir->left->left"): 1,
        ("programs/binary_tree.ft", "heir->right->left"): 1,
        ("programs/binary_tree.ft", "node->left->left"): 3,
        ("programs/binary_tree.ft", "node->right->left"): 2,
        ("programs/binary_tree.ft", "successor->left"): 1,
        ("programs/binary_tree.ft", "t->root->left"): 2,
        ("programs/binary_tree.ft", "t.root->left"): 1,
        ("programs/json.ft", "tail->next->first"): 2,
        ("programs/json.ft", "v->first->first"): 2,
        ("programs/linked_list.ft", "step->next->next"): 1,
        ("run/ownership/021_list_push_pop.ft", "l->head->next"): 1,
        ("run/ownership/021_list_push_pop.ft", "n->next"): 1,
        ("run/ownership/021_list_push_pop.ft", "n->next->next"): 1,
        ("run/ownership/021_list_push_pop.ft", "pop(l)->next"): 1,
        ("run/ownership/062_zero_accumulator_in_a_loop.ft", "head->next"): 1,
    },
}

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))

SITE = re.compile(
    r"^(.*):(\d+):(\d+): leak: cannot prove '(.*)' is empty "
    r"(?:at this exit|before this del|before this cast)$"
)

OPTIONS = None


def std_root(target, out):
    """The standard root of `target`, as the build assembles it, in `out`."""
    script = os.path.join(OPTIONS.root, "tools", "assemble_std.sh")
    subprocess.run(["bash", script, target, os.path.join(OPTIONS.root, "std"), out],
                   check=True, capture_output=True)
    return out


def where(name, std):
    """The file of a report line: below test/lang, or `std/...` for a module of the root."""
    path = os.path.normpath(os.path.join(LANG, name))
    if path.startswith(os.path.normpath(std) + os.sep):
        return "std/" + os.path.relpath(path, std)
    return os.path.relpath(path, LANG)


def report(job):
    """The sites of one report of one entry: a set of (file, line, column, place)."""
    kind, triple, std, test = job
    argv = [OPTIONS.fort, "--target", triple, "--std-dir", std, "--%s-report" % kind]
    argv += [flag for flag in test.flags if flag not in run_tests.NOT_WITH_CHECK]
    argv.append(test.entry)
    proc = subprocess.run(argv, cwd=LANG, capture_output=True, text=True)
    if proc.returncode not in (0, 1):
        raise AssertionError("%s exits %d: %s" % (argv, proc.returncode, proc.stderr[-2000:]))
    found = set()
    for line in proc.stdout.splitlines():
        m = SITE.match(line)
        if m:
            found.add((where(m.group(1), std), int(m.group(2)), int(m.group(3)), m.group(4)))
    return found


class AggregateLang(unittest.TestCase):
    def test_each_report_gives_the_listed_sites_on_each_target(self):
        tests, problems = run_tests.discover(LANG)
        self.assertEqual(problems, [])
        for test in tests:
            run_tests.load_test(LANG, test)
        self.assertGreater(len(tests), 800)
        for target, triple in TARGETS:
            with tempfile.TemporaryDirectory() as out:
                std = std_root(target, os.path.join(out, "std"))
                for kind in ("aggregate", "leak"):
                    with self.subTest(target=target, report=kind):
                        jobs = [(kind, triple, std, test) for test in tests]
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
