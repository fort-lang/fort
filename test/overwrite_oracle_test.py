#!/usr/bin/env python3
"""Hold the emptiness pass against the runtime overwrite check of the language corpus.

A program of test/lang/run that stops with `runtime error: overwriting owned value` at
`<file>:<line>:<col>` stores over a live `own` value there, in a checked build. The emptiness
pass must not prove that check, so `fort --overwrite-report` must list the site. The test finds
each such program by its `//! stderr:` directive, runs the report with the `//! flags:` of the
program from test/lang, as run_tests.py compiles it, and fails when the report does not list
the site.

A directive whose position names another file takes no part: a test that writes the message
itself names `src/app.ft`. A directive with no position stops in a module that the program
imports; IMPORTED_SITES names that site, and the report must list it.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import unittest

OPTIONS = None
LANG = Path(__file__).resolve().parent / "lang"
STDERR = re.compile(
    r"^//! stderr: (?:(\S+):(\d+):(\d+): )?runtime error: overwriting owned value$", re.M)
FLAGS = re.compile(r"^//! flags:(.*)$", re.M)
# The programs whose own sites stop: 025, 031, 008, 012, 020 and 066 of test/lang/run.
OWN_SITES = 6
# The site, as file name, line and column, of each program that stops in a module it imports.
# The key is the path of the program below test/lang/run.
IMPORTED_SITES = {"modules/imported_overwrite/main.ft": ("slot.ft", 8, 10)}


def trapping_programs():
    """Each program of test/lang/run with the directive, and the match of the directive."""
    for path in sorted((LANG / "run").rglob("*.ft")):
        found = STDERR.search(path.read_text())
        if found is not None:
            yield path, found


def report(path):
    """The site lines that `--overwrite-report` writes for the program at `path`."""
    flags = FLAGS.search(path.read_text())
    args = flags.group(1).split() if flags else []
    entry = str(path.relative_to(LANG))
    done = subprocess.run(
        [OPTIONS.fort, "--std-dir", OPTIONS.std_dir, "--overwrite-report"] + args + [entry],
        cwd=LANG, capture_output=True, text=True, timeout=300, check=False)
    if done.returncode != 0:
        raise AssertionError("%s: exit %d: %s" % (entry, done.returncode, done.stderr))
    return done.stdout.splitlines()


def site_of(line):
    """The file name, the line and the column of one line of the report."""
    where = line.split(" ", 1)[0]
    path, number, col = where.rsplit(":", 2)
    return Path(path).name, int(number), int(col)


class OracleTest(unittest.TestCase):
    def test_each_site_that_stops_at_run_time_is_reported(self):
        own = 0
        other = 0
        for path, found in trapping_programs():
            sites = [site_of(line) for line in report(path)]
            with self.subTest(program=str(path.relative_to(LANG))):
                if found.group(1) is None:
                    key = str(path.relative_to(LANG / "run"))
                    self.assertIn(key, IMPORTED_SITES)
                    self.assertIn(IMPORTED_SITES[key], sites)
                    other += 1
                elif Path(found.group(1)).name == path.name:
                    want = (path.name, int(found.group(2)), int(found.group(3)))
                    self.assertIn(want, sites)
                    own += 1
        # A count below these would mean that the search found fewer programs, and the test
        # held less than it says.
        self.assertEqual(own, OWN_SITES)
        self.assertEqual(other, len(IMPORTED_SITES))

    def test_a_program_with_no_owning_store_reports_nothing_of_its_own(self):
        path = LANG / "run" / "lexical" / "001_int_literals.ft"
        if not path.exists():
            self.skipTest("the program is not in the corpus")
        sites = [site_of(line) for line in report(path)]
        self.assertNotIn(path.name, [s[0] for s in sites])


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard library directory")
    OPTIONS, remaining = parser.parse_known_args()
    # the report runs in test/lang, so a relative path would name another file
    OPTIONS.fort = str(Path(OPTIONS.fort).resolve())
    OPTIONS.std_dir = str(Path(OPTIONS.std_dir).resolve())
    unittest.main(argv=[sys.argv[0]] + remaining)


if __name__ == "__main__":
    main()
