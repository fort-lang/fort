#!/usr/bin/env python3
"""Hold the static overwrite rule against the programs that trapped at run time.

Before the rule of D17.11 became static, each program of FORMER_SITES stopped a checked build at
run time with `runtime error: overwriting owned value`, at the line and the column that the table
gives. Each one is a fail test of test/lang now. The emptiness pass must report the error of
D17.11 and its note at each of those positions, and nowhere else. A program of LEAK_SITES also
keeps an allocation at an exit on purpose, so its runs give the error of the leak rule and its note
there too, after the errors of D17.11 (D17.20), and no other error of the leak rule.

The test compiles each program in four modes: a checked build, a `--release` build, a
`--no-bounds-check` build and `--check`. A mode flag in the `//! flags:` of a program does not
replace the mode under test, so each program gets all four modes. A build runs with `-S` into a
temporary directory, so it needs no `--cc`. A build that reports the error must write no module
file, and a build of a FIXED program must write one.
Each run must exit 1 with the same diagnostics, so checked and release builds give the same
verdict. That is 32 runs: 8 programs, 9 sites, 4 modes. Each program of FIXED stores after a
`del` or a `move`, and each of its 24 runs (6 programs, 4 modes) must exit 0 with no diagnostic.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

OPTIONS = None
LANG = Path(__file__).resolve().parent / "lang"
FLAGS = re.compile(r"^//! flags:(.*)$", re.M)
DIAGNOSTIC = re.compile(r"^(\S+):(\d+):(\d+): (error|note): (.*)$")
# The mode options of the four runs. An empty list is a checked build.
MODES = ([], ["--release"], ["--no-bounds-check"], ["--check"])
# The flags that select a mode. The test takes them out of the flags of a program.
MODE_FLAGS = ("--release", "--no-bounds-check", "--check")
# The runs of each test: the programs of each table times the modes.
FORMER_RUNS = 32
FIXED_RUNS = 24
# Each former trap program, below test/lang, with the sites of the error: the file name, the line,
# the column and the place that the message names. The comment gives the run test that held the
# trap. 054 has two sites: the store of line 22 ran at run time, and the static rule reports it.
FORMER_SITES = {
    # run/errors/025_declaration_overwrites.ft
    "fail/ownership/048_declaration_overwrites.ft": [
        ("048_declaration_overwrites.ft", 20, 22, "leaked")],
    # run/errors/031_overwrite_own_element.ft
    "fail/ownership/049_overwrite_own_element.ft": [
        ("049_overwrite_own_element.ft", 15, 15, "owners[0]")],
    # run/modes/008_overwrite_check_kept.ft
    "fail/ownership/050_overwrite_no_bounds_check.ft": [
        ("050_overwrite_no_bounds_check.ft", 20, 7, "p")],
    # run/ownership/012_overwrite_checked.ft
    "fail/ownership/051_overwrite_checked.ft": [("051_overwrite_checked.ft", 22, 7, "p")],
    # run/ownership/013_overwrite_release.ft
    "fail/ownership/052_overwrite_release.ft": [("052_overwrite_release.ft", 20, 7, "p")],
    # run/ownership/020_overwrite_own_field.ft
    "fail/ownership/053_overwrite_own_field.ft": [
        ("053_overwrite_own_field.ft", 17, 13, "v->data")],
    # run/ownership/066_overwrite_through_a_second_deref.ft
    "fail/ownership/054_overwrite_through_a_second_deref.ft": [
        ("054_overwrite_through_a_second_deref.ft", 22, 16, "p->next->g"),
        ("054_overwrite_through_a_second_deref.ft", 23, 10, "p->g")],
    # run/modules/imported_overwrite/main.ft
    "fail/modules/028_imported_overwrite/main.ft": [("slot.ft", 8, 10, "*out")],
}
# The count of the sites of FORMER_SITES: the runtime tests trapped at 8 sites, and 054 adds one.
SITE_COUNT = 9
# The programs of FORMER_SITES with an exit that the leak rule refuses, with the file name, the
# line, the column and the owner of each such exit. The loop body of 048 ends at its closing brace
# with `leaked` live, which is also why its declaration is an error.
LEAK_SITES = {
    "fail/ownership/048_declaration_overwrites.ft": [
        ("048_declaration_overwrites.ft", 23, 5, "leaked")],
}
# The run tests that store after a `del` or a `move`: the fixes that the note names.
FIXED = (
    "run/modes/008_store_after_del_no_bounds_check.ft",
    "run/ownership/012_store_after_del.ft",
    "run/ownership/013_store_after_move_release.ft",
    "run/ownership/020_refill_own_field.ft",
    "run/ownership/066_declaration_after_del.ft",
    "run/ownership/067_own_element_after_del.ft",
)


def flags_of(entry):
    """The words of the `//! flags:` directive of the test at `entry`, below test/lang."""
    found = FLAGS.search((LANG / entry).read_text())
    return found.group(1).split() if found else []


def options_of(entry, mode, output):
    """The options of a run of `entry` in `mode`: the mode, then the flags of the program that
    select no mode, then `-S -o output` for a build."""
    words = mode + [word for word in flags_of(entry) if word not in MODE_FLAGS]
    if "--check" not in mode:
        words = words + ["-S", "-o", output]
    return words


def compile_in(entry, mode):
    """Runs the compiler over `entry` in `mode`, from test/lang. Returns the exit status, the
    diagnostics as (file name, line, column, severity, message), and whether a build wrote its
    module file (None under `--check`, which writes none)."""
    with tempfile.TemporaryDirectory() as scratch:
        output = Path(scratch) / "out.ll"
        words = options_of(entry, mode, str(output))
        done = subprocess.run(
            [OPTIONS.fort, "--std-dir", OPTIONS.std_dir] + words + [entry],
            cwd=LANG, capture_output=True, text=True, timeout=300, check=False)
        wrote = None if "--check" in mode else output.exists()
    found = []
    for line in done.stderr.splitlines():
        match = DIAGNOSTIC.match(line)
        if match is not None:
            found.append((Path(match.group(1)).name, int(match.group(2)), int(match.group(3)),
                          match.group(4), match.group(5)))
    return done.returncode, found, wrote


def expected(sites, leaks=()):
    """The error and the note of each site, in order, and then those of each leak."""
    out = []
    for name, line, col, place in sites:
        out.append((name, line, col, "error",
                    "overwriting owned value: cannot prove '%s' is empty here" % place))
        out.append((name, line, col, "note", "del or move '%s' before this store" % place))
    for name, line, col, place in leaks:
        out.append((name, line, col, "error",
                    "leak: cannot prove '%s' is empty at this exit" % place))
        out.append((name, line, col, "note", "del or move '%s' before this exit" % place))
    return out


class OracleTest(unittest.TestCase):
    def test_each_former_trap_fails_to_compile_at_its_site_in_every_mode(self):
        sites = 0
        runs = 0
        for entry, where in FORMER_SITES.items():
            self.assertTrue((LANG / entry).read_text().startswith("//! fail\n"), entry)
            want = expected(where, LEAK_SITES.get(entry, ()))
            for mode in MODES:
                with self.subTest(program=entry, mode=" ".join(mode) or "checked"):
                    status, found, wrote = compile_in(entry, mode)
                    self.assertEqual(status, 1)
                    self.assertEqual(found, want)
                    self.assertEqual(wrote, None if "--check" in mode else False)
                runs += 1
            sites += len(where)
        # A count below these would mean that the table lost a site or a mode, and the test held
        # less than it says.
        self.assertEqual(sites, SITE_COUNT)
        self.assertEqual(runs, FORMER_RUNS)
        self.assertTrue(set(LEAK_SITES) <= set(FORMER_SITES))

    def test_each_fix_compiles_in_every_mode(self):
        runs = 0
        for entry in FIXED:
            for mode in MODES:
                with self.subTest(program=entry, mode=" ".join(mode) or "checked"):
                    status, found, wrote = compile_in(entry, mode)
                    self.assertEqual((status, found), (0, []))
                    self.assertEqual(wrote, None if "--check" in mode else True)
                runs += 1
        self.assertEqual(runs, FIXED_RUNS)

    def test_a_mode_flag_of_a_program_does_not_replace_the_mode_under_test(self):
        # 052 asks for --release and 050 for --no-bounds-check; each mode runs as itself
        release = "fail/ownership/052_overwrite_release.ft"
        bounds = "fail/ownership/050_overwrite_no_bounds_check.ft"
        self.assertEqual(flags_of(release), ["--release"])
        self.assertEqual(flags_of(bounds), ["--no-bounds-check"])
        self.assertEqual(options_of(release, [], "o.ll"), ["-S", "-o", "o.ll"])
        self.assertEqual(options_of(bounds, [], "o.ll"), ["-S", "-o", "o.ll"])
        self.assertEqual(options_of(bounds, ["--release"], "o.ll"),
                         ["--release", "-S", "-o", "o.ll"])
        self.assertEqual(options_of(release, ["--check"], "o.ll"), ["--check"])


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard library directory")
    OPTIONS, remaining = parser.parse_known_args()
    # the compiler runs in test/lang, so a relative path would name another file
    OPTIONS.fort = str(Path(OPTIONS.fort).resolve())
    OPTIONS.std_dir = str(Path(OPTIONS.std_dir).resolve())
    unittest.main(argv=[sys.argv[0]] + remaining)


if __name__ == "__main__":
    main()
