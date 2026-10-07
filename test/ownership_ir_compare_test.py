#!/usr/bin/env python3
"""Check tools/ownership_ir_compare.py with a recording compiler and with the built compiler."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CHECKOUT = Path(__file__).resolve().parents[1]
TOOL = CHECKOUT / "tools" / "ownership_ir_compare.py"
OPTIONS = None

# A compiler stand-in. FAKE_SELECTED and FAKE_UNSELECTED give the exit status and the IR text of
# a run with and without --ownership-check. FAKE_LOG receives one line for each argument vector.
FAKE = r'''#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
with open(os.environ["FAKE_LOG"], "a") as log:
    log.write(" ".join(args) + "\n")
which = "FAKE_SELECTED" if "--ownership-check" in args else "FAKE_UNSELECTED"
status, _, text = os.environ[which].partition(":")
if "--release" in args and "--no-bounds-check" in args:
    text = text.replace("MODE", "both")
elif "--release" in args:
    text = text.replace("MODE", "release")
elif "--no-bounds-check" in args:
    text = text.replace("MODE", "nobounds")
else:
    text = text.replace("MODE", "default")
if status == "0":
    with open(args[args.index("-o") + 1], "w") as out:
        out.write(text)
else:
    sys.stderr.write("main.ft:1:1: error: fake\n")
sys.exit(int(status))
'''


class CompareToolTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="fort-ir-compare-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.fake = self.root / "fort"
        self.fake.write_text(FAKE)
        self.fake.chmod(0o700)
        self.log = self.root / "log"
        self.entry = self.root / "main.ft"
        self.entry.write_text("fn main() i32 { return 0; }\n")

    def compare(self, selected, unselected, *extra, fort=None, std=None, entries=None):
        env = dict(os.environ, FAKE_LOG=str(self.log), FAKE_SELECTED=selected,
                   FAKE_UNSELECTED=unselected)
        if entries is None:
            entries = [self.entry]
        argv = [sys.executable, str(TOOL), "--fort", str(fort or self.fake),
                "--std-dir", str(std or self.root), *extra, *(str(path) for path in entries)]
        return subprocess.run(argv, cwd=self.root, env=env, capture_output=True, text=True,
                              timeout=600, check=False)

    def runs(self):
        return [line.split() for line in self.log.read_text().splitlines()]

    def test_equal_ir_in_each_mode_exits_zero(self):
        result = self.compare("0:define MODE\n", "0:define MODE\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            "equal %s default" % self.entry,
            "equal %s release" % self.entry,
            "equal %s nobounds" % self.entry,
            "equal %s release-nobounds" % self.entry,
            "ownership_ir_compare: 4 cases: 4 equal, 0 differ, 0 rejected",
        ])
        runs = self.runs()
        self.assertEqual(len(runs), 8)
        # Each pair differs only in --ownership-check, and each run writes IR with -S.
        for unselected, selected in zip(runs[0::2], runs[1::2]):
            self.assertNotIn("--ownership-check", unselected)
            self.assertEqual([arg for arg in selected if arg != "--ownership-check"][:-3],
                             unselected[:-3])
            for run in (unselected, selected):
                self.assertIn("-S", run)
                self.assertEqual(run[-1], str(self.entry))
                self.assertEqual(run[:2], ["--std-dir", str(self.root)])
        self.assertEqual([run.count("--release") for run in runs], [0, 0, 1, 1, 0, 0, 1, 1])
        self.assertEqual([run.count("--no-bounds-check") for run in runs],
                         [0, 0, 0, 0, 1, 1, 1, 1])

    def test_one_differing_byte_exits_one_with_a_diff(self):
        result = self.compare("0:define MODE\n  call @check\n", "0:define MODE\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("differ %s default" % self.entry, result.stdout)
        self.assertIn("+  call @check", result.stdout)
        self.assertIn("ownership_ir_compare: 4 cases: 0 equal, 4 differ, 0 rejected",
                      result.stdout)

    def test_a_rejected_selected_build_is_no_equality(self):
        result = self.compare("1:", "0:define MODE\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("rejected %s default: the selected build exited 1 and wrote no IR"
                      % self.entry, result.stdout)
        self.assertIn("main.ft:1:1: error: fake", result.stdout)
        self.assertIn("4 cases: 0 equal, 0 differ, 4 rejected", result.stdout)

    def test_a_failed_unselected_build_is_a_tool_error(self):
        result = self.compare("0:define MODE\n", "1:")
        self.assertEqual(result.returncode, 2)
        self.assertIn("the unselected build exited 1", result.stderr)

    def test_a_compiler_error_status_is_a_tool_error(self):
        result = self.compare("2:", "0:define MODE\n")
        self.assertEqual(result.returncode, 2)
        self.assertIn("the selected build exited 2", result.stderr)

    def test_flags_and_modes_reach_both_builds(self):
        result = self.compare("0:define MODE\n", "0:define MODE\n", "--mode", "release",
                              "--flag=--cfg", "--flag=feature=on")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ownership_ir_compare: 1 cases: 1 equal", result.stdout)
        for run in self.runs():
            self.assertIn("--release", run)
            self.assertEqual(run[run.index("--cfg") + 1], "feature=on")

    def test_every_entry_is_compared(self):
        other = self.root / "other.ft"
        other.write_text("fn main() i32 { return 1; }\n")
        result = self.compare("0:define MODE\n", "0:define MODE\n", "--mode", "default",
                              entries=[self.entry, other])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.runs()), 4)
        self.assertIn("equal %s default" % other, result.stdout)

    def test_a_flag_cannot_select_the_analysis(self):
        # With --ownership-check in both builds the tool would compare a selected build with
        # itself and report equal.
        for flags in (["--flag=--ownership-check"],
                      ["--flag=--ownership-report", "--flag=report.json"],
                      ["--flag=--ownership-report=report.json"]):
            with self.subTest(flags=flags):
                result = self.compare("0:define MODE\n", "0:define MODE\n", *flags)
                self.assertEqual(result.returncode, 2)
                self.assertIn("the tool adds --ownership-check to one build only", result.stderr)
                self.assertFalse(self.log.exists())

    def test_one_mode_builds_release_without_bounds_checks(self):
        result = self.compare("0:define MODE\n", "0:define MODE\n", "--mode", "release-nobounds")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.runs()), 2)
        for run in self.runs():
            self.assertIn("--release", run)
            self.assertIn("--no-bounds-check", run)

    def test_no_entry_is_a_usage_error(self):
        result = self.compare("0:", "0:", entries=[])
        self.assertEqual(result.returncode, 2)

    # ---- the built compiler ----------------------------------------------------------------

    def test_the_built_compiler_rejects_a_selected_violation(self):
        self.entry.write_text(
            "fn leak() void {\n"
            "    i32 mut* own p = new(i32);\n"
            "}\n"
            "fn main() i32 { return 0; }\n"
        )
        result = self.compare("", "", fort=OPTIONS.fort, std=OPTIONS.std_dir)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("rejected %s default" % self.entry, result.stdout)
        self.assertIn("error: lost ownership of 'p'", result.stdout)
        self.assertIn("4 cases: 0 equal, 0 differ, 4 rejected", result.stdout)

    def test_the_built_compiler_gives_a_tool_error_for_a_source_error(self):
        self.entry.write_text("fn main() i32 { return missing; }\n")
        result = self.compare("", "", fort=OPTIONS.fort, std=OPTIONS.std_dir)
        self.assertEqual(result.returncode, 2)
        self.assertIn("the unselected build exited 1", result.stderr)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True)
    parser.add_argument("--std-dir", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.fort = str(Path(OPTIONS.fort).resolve())
    OPTIONS.std_dir = str(Path(OPTIONS.std_dir).resolve())
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
