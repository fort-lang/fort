#!/usr/bin/env python3
"""Unit tests of tools/mutate.py, the mutation runner of a coverage audit.

The runner answers one question for each row of a table: does a test in the
repository fail when this rule is broken? A wrong answer is silent, so the
parts that can give one are what this file tests. The failure parser must name
each test that ctest reports. The anchor must
match its file exactly once, or the round mutates the wrong line or no line.
The stale guard must refuse a verdict when the build gave back the baseline's
binary. And the restore must bring the sources back byte for byte.

The rounds themselves are not run here: one round rebuilds the compiler and
costs seconds to minutes. A fake guest answers the build and the stages instead,
which is what lets a test state the verdict of a round in a millisecond.

The last class reads the real table, `tools/mutations/emitter_bootstrap.json`,
and runs `--check` over the sources in the repository, so the table answers for
its own anchors in the gate.

Run with `python3 -m unittest mutate_test` from this directory.
Standard library only; Python 3.12.
"""

import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
ROOT = TEST_DIR.parent
TABLE_PATH = ROOT / "tools" / "mutations" / "emitter_bootstrap.json"
sys.path.insert(0, str(ROOT / "tools"))

import mutate  # noqa: E402

# What ctest prints when three tests fail. The per-test lines above the summary
# name only the test that was running; the summary block names them all.
CTEST_OUTPUT = """\
        Start  3: unit-gen_cast
  3/78 Test  #3: unit-gen_cast ....................***Failed    0.11 sec
        Start  4: unit-gen_call
  4/78 Test  #4: unit-gen_call ....................   Passed    0.09 sec

39% tests passed, 3 tests failed out of 78

Label Time Summary:
unit    =   6.02 sec*proc (78 tests)

The following tests FAILED:
          3 - unit-gen_cast (Failed)
         17 - unit-gen_own (Subprocess aborted)
         41 - lang (Timeout)
Errors while running CTest
"""

GREEN_OUTPUT = """\
100% tests passed, 0 tests failed out of 78

Label Time Summary:
unit    =   6.02 sec*proc (78 tests)
"""


class FakeGuest(mutate.Guest):
    """A guest that answers from a script rather than from a build.

    `results` maps the command to a (status, output) pair, and `binaries` gives
    the md5 the build produces on each call, in order. Every command reaches
    `self.commands`, so a test asserts which stages ran and which did not.
    """

    def __init__(self, root, results, binaries):
        super().__init__(root, "unused")
        self.results = results
        self.binaries = list(binaries)
        self.commands = []

    def run(self, command, timeout=3600):
        self.commands.append(command)
        return self.results.get(command, (0, ""))

    def md5(self, path):
        return self.binaries.pop(0) if self.binaries else "unset"


def small_table(root):
    """A two-source table whose build and stages the fake guest answers."""
    return {
        "sources": ["a.c", "b.c"],
        "binary": "build/debug/fort",
        "build": "ninja",
        "stages": [{"name": "narrow", "command": "ctest -R gen"},
                   {"name": "wide", "command": "ctest -L unit"}],
        "mutations": [{"decision": "D1.1", "file": "a.c",
                       "old": "return 1;", "new": "return 8;",
                       "what": "one is eight"}],
    }


GONE = {"decision": "D1.2", "file": "a.c", "old": "return 7;", "new": "return 9;",
        "what": "an anchor that rotted"}


def make_tree(root):
    """Writes the two sources the small table names, and returns their bytes."""
    (Path(root) / "a.c").write_text("int one(void) { return 1; }\n")
    (Path(root) / "b.c").write_text("int two(void) { return 2; }\n")
    return {"a.c": (Path(root) / "a.c").read_bytes(),
            "b.c": (Path(root) / "b.c").read_bytes()}


class TestFailingTests(unittest.TestCase):
    """The failure parser reads the summary block and nothing else."""

    def test_it_names_every_failed_test_whatever_its_kind(self):
        # A plain failure, an abort and a timeout: all three are red, and the
        # parser that read only `(Failed)` reported a third of this round.
        self.assertEqual(mutate.failing_tests(CTEST_OUTPUT),
                         ["unit-gen_cast", "unit-gen_own", "lang"])

    def test_it_reads_no_name_out_of_a_green_run(self):
        self.assertEqual(mutate.failing_tests(GREEN_OUTPUT), [])
        self.assertEqual(mutate.failing_tests(""), [])

    def test_it_keeps_the_order_and_drops_a_repeat(self):
        doubled = CTEST_OUTPUT + "          3 - unit-gen_cast (Failed)\n"
        self.assertEqual(mutate.failing_tests(doubled),
                         ["unit-gen_cast", "unit-gen_own", "lang"])

    def test_a_passing_line_of_the_progress_report_is_not_a_failure(self):
        # `  4/78 Test  #4: unit-gen_call ... Passed` starts with digits and a
        # slash, which the summary's `  4 - name (Failed)` shape excludes.
        self.assertEqual(mutate.failing_tests(
            "  3/78 Test  #3: unit-gen_cast ....***Failed    0.11 sec\n"), [])


class TestApplyAndRestore(unittest.TestCase):
    """The anchor matches once, or the round does not run."""

    def test_it_substitutes_the_anchor(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            mutate.apply_mutation(root, small_table(root)["mutations"][0])
            self.assertEqual((Path(root) / "a.c").read_text(),
                             "int one(void) { return 8; }\n")

    def test_an_anchor_that_is_not_there_stops_the_round(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            with self.assertRaises(mutate.AnchorError) as raised:
                mutate.apply_mutation(root, GONE)
            self.assertIn("matches 0 times", str(raised.exception))

    def test_an_anchor_that_is_there_twice_stops_the_round(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            (Path(root) / "a.c").write_text("return 1;\nreturn 1;\n")
            with self.assertRaises(mutate.AnchorError) as raised:
                mutate.apply_mutation(root, small_table(root)["mutations"][0])
            self.assertIn("matches 2 times", str(raised.exception))

    def test_restore_brings_every_source_back(self):
        with tempfile.TemporaryDirectory() as root:
            saved = make_tree(root)
            table = small_table(root)
            mutate.apply_mutation(root, table["mutations"][0])
            (Path(root) / "b.c").write_text("ruined\n")
            mutate.restore_all(table, saved, root)
            self.assertEqual((Path(root) / "a.c").read_bytes(), saved["a.c"])
            self.assertEqual((Path(root) / "b.c").read_bytes(), saved["b.c"])

    def test_a_restored_file_gets_a_new_mtime(self):
        # The shared folder hands ninja the mtime, so a restore that kept the
        # old one is not rebuilt.
        with tempfile.TemporaryDirectory() as root:
            saved = make_tree(root)
            old = os.stat(Path(root) / "a.c").st_mtime_ns
            os.utime(Path(root) / "a.c", ns=(old - 10**9, old - 10**9))
            mutate.restore_all(small_table(root), saved, root)
            self.assertGreater(os.stat(Path(root) / "a.c").st_mtime_ns,
                               old - 10**9)


class TestRunRound(unittest.TestCase):
    """One round: the four verdicts, and which stages each one runs."""

    def round_with(self, results, binaries):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root)
        saved = make_tree(root)
        table = small_table(root)
        guest = FakeGuest(root, results, binaries)
        with tempfile.TemporaryDirectory() as logs:
            row = mutate.run_round(table, guest, root, saved,
                                   table["mutations"][0], "base-md5", logs)
            kept = (Path(logs) / "D1.1.log").read_text()
        return row, guest, kept

    def test_a_mutation_a_stage_catches_is_caught_and_names_the_tests(self):
        row, guest, kept = self.round_with(
            {"ctest -R gen": (8, CTEST_OUTPUT)}, ["mutant-md5"])
        self.assertEqual(row["verdict"], "caught")
        self.assertEqual(row["stage"], "narrow")
        self.assertEqual(row["tests"],
                         ["unit-gen_cast", "unit-gen_own", "lang"])
        # The wide stage costs 320 s to 420 s and the round has its answer.
        self.assertNotIn("ctest -L unit", guest.commands)
        self.assertIn("unit-gen_own (Subprocess aborted)", kept)

    def test_a_mutation_no_stage_catches_survives_after_every_stage(self):
        row, guest, _ = self.round_with({}, ["mutant-md5"])
        self.assertEqual(row["verdict"], "survived")
        self.assertEqual(row["tests"], [])
        self.assertEqual(guest.commands,
                         ["ninja", "ctest -R gen", "ctest -L unit"])

    def test_a_mutant_that_does_not_compile_is_not_a_survivor(self):
        row, guest, kept = self.round_with(
            {"ninja": (1, "error: unused parameter 'g'")}, ["mutant-md5"])
        self.assertEqual(row["verdict"], "build-failed")
        self.assertEqual(guest.commands, ["ninja"])
        self.assertIn("unused parameter", kept)

    def test_a_stage_that_hangs_is_a_timeout_and_not_a_catch(self):
        # A loop mutation can hang. The run must record
        # the row and go on, not end 75 rows early with a traceback.
        row, guest, kept = self.round_with(
            {"ctest -R gen": (mutate.TIMEOUT_STATUS, "mutate: the command ran longer")},
            ["mutant-md5"])
        self.assertEqual(row["verdict"], "timeout")
        self.assertEqual(row["stage"], "narrow")
        self.assertEqual(row["tests"], [])
        self.assertNotIn("ctest -L unit", guest.commands)
        self.assertIn("ran longer", kept)

    def test_a_build_that_hangs_is_a_timeout(self):
        row, guest, _ = self.round_with(
            {"ninja": (mutate.TIMEOUT_STATUS, "")}, ["mutant-md5"])
        self.assertEqual(row["verdict"], "timeout")
        self.assertEqual(row["stage"], "build")
        self.assertEqual(guest.commands, ["ninja"])

    def test_a_rotted_anchor_ends_its_row_and_not_the_run(self):
        with tempfile.TemporaryDirectory() as root, \
                tempfile.TemporaryDirectory() as logs:
            saved = make_tree(root)
            table = small_table(root)
            guest = FakeGuest(root, {}, ["mutant-md5"])
            row = mutate.run_round(table, guest, root, saved, GONE, "base-md5", logs)
            self.assertEqual(row["verdict"], "anchor-stale")
            self.assertIn("matches 0 times", row["error"])
            self.assertEqual(guest.commands, [])
            self.assertEqual((Path(root) / "a.c").read_bytes(), saved["a.c"])

    def test_a_binary_equal_to_the_baseline_gives_no_verdict(self):
        # The build said it succeeded and produced the baseline's binary, so
        # the mutation reached no object and a green stage would be a lie.
        row, guest, _ = self.round_with({}, ["base-md5"])
        self.assertEqual(row["verdict"], "stale")
        self.assertEqual(guest.commands, ["ninja"])

    def test_the_round_starts_from_the_saved_sources(self):
        with tempfile.TemporaryDirectory() as root, \
                tempfile.TemporaryDirectory() as logs:
            saved = make_tree(root)
            table = small_table(root)
            (Path(root) / "b.c").write_text("what the last round left\n")
            guest = FakeGuest(root, {}, ["mutant-md5"])
            mutate.run_round(table, guest, root, saved, table["mutations"][0],
                             "base-md5", logs)
            self.assertEqual((Path(root) / "b.c").read_bytes(), saved["b.c"])
            self.assertIn("return 8;", (Path(root) / "a.c").read_text())


class TestMutatedRows(unittest.TestCase):
    """The guard that keeps a test from judging its own round.

    A round rewrites a source. A test that reads the table against the live
    sources then fails inside the round, and the round records the compiler as
    caught when no test of the compiler saw the mutation. `mutated_rows` is what
    the test and `--check` ask before they report.
    """

    def test_a_clean_tree_carries_no_round(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            self.assertEqual(mutate.mutated_rows(small_table(root), root), [])

    def test_a_tree_under_a_round_names_the_row_that_is_applied(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            table = small_table(root)
            mutate.apply_mutation(root, table["mutations"][0])
            self.assertEqual(mutate.mutated_rows(table, root), ["D1.1"])

    def test_a_rotted_anchor_is_not_a_round(self):
        # Neither the anchor nor the mutation is in the file, so the table is
        # stale and `--check` must still say so.
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            (Path(root) / "a.c").write_text("int one(void) { return 0; }\n")
            self.assertEqual(mutate.mutated_rows(small_table(root), root), [])

    def test_check_does_not_call_the_applied_row_stale(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            table = small_table(root)
            mutate.apply_mutation(root, table["mutations"][0])
            out = io.StringIO()
            with redirect_stdout(out):
                status = mutate.check_table(table, root)
            self.assertEqual(status, 0)
            self.assertIn("holds this row's mutation, not its anchor", out.getvalue())
            self.assertIn("a round is in progress: D1.1", out.getvalue())
            self.assertIn("1 rows, 0 stale", out.getvalue())

    def test_a_second_row_on_the_mutated_lines_is_not_stale(self):
        # Two rows of the shipped table anchor the same lines.
        # Applying one leaves the other with no anchor, and that
        # is the round talking: `--check` reads the text with the applied row
        # put back, so it reports 0 stale.
        with tempfile.TemporaryDirectory() as root:
            table = {"sources": ["a.c"], "binary": "b", "build": "", "stages": [],
                     "mutations": [
                         {"decision": "D1.1", "file": "a.c", "old": "return 1;",
                          "new": "return 8;", "what": "one is eight"},
                         {"decision": "D1.1b", "file": "a.c", "old": "return 1;",
                          "new": "abort(); return 1;", "what": "a probe"}]}
            (Path(root) / "a.c").write_text("int one(void) { return 1; }\n")
            mutate.apply_mutation(root, table["mutations"][0])
            out = io.StringIO()
            with redirect_stdout(out):
                status = mutate.check_table(table, root)
            self.assertEqual(status, 0, out.getvalue())
            self.assertIn("2 rows, 0 stale", out.getvalue())

    def test_a_rotted_row_is_still_stale_while_a_round_runs(self):
        with tempfile.TemporaryDirectory() as root:
            table = small_table(root)
            table["mutations"].append(dict(GONE))
            make_tree(root)
            mutate.apply_mutation(root, table["mutations"][0])
            out = io.StringIO()
            with redirect_stdout(out):
                status = mutate.check_table(table, root)
            self.assertEqual(status, 1)
            self.assertIn("D1.2: a.c: 0 matches", out.getvalue())
            self.assertIn("2 rows, 1 stale", out.getvalue())


class TestRunTable(unittest.TestCase):
    """Which rows a run reaches, and where it stops."""

    def table_of_two(self, root):
        table = small_table(root)
        table["mutations"].append({"decision": "D2.1", "file": "b.c",
                                   "old": "return 2;", "new": "return 9;",
                                   "what": "two is nine"})
        return table

    def run_two(self, results, binaries, wanted=()):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root)
        saved = make_tree(root)
        table = self.table_of_two(root)
        guest = FakeGuest(root, results, binaries)
        logs = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, logs)
        out = io.StringIO()
        with redirect_stdout(out):
            rows = mutate.run_table(table, guest, root, saved, "base-md5", logs, wanted)
        return rows, guest, out.getvalue()

    def test_it_runs_every_row_in_order(self):
        rows, _, _ = self.run_two({}, ["m1", "m2"])
        self.assertEqual([r["decision"] for r in rows], ["D1.1", "D2.1"])
        self.assertEqual([r["verdict"] for r in rows], ["survived", "survived"])

    def test_only_selects_one_row(self):
        rows, _, _ = self.run_two({}, ["m1"], wanted={"D2.1"})
        self.assertEqual([r["decision"] for r in rows], ["D2.1"])

    def test_a_timeout_stops_the_run_because_the_guest_keeps_working(self):
        # `tools/vm` waits on an ssh without a pty, so the host-side timeout
        # leaves the command running in the guest. A second round in the same
        # build directory would read that collision as a finding.
        rows, guest, said = self.run_two(
            {"ctest -R gen": (mutate.TIMEOUT_STATUS, "")}, ["m1", "m2"])
        self.assertEqual([r["verdict"] for r in rows], ["timeout"])
        self.assertEqual(guest.commands, ["ninja", "ctest -R gen"])
        self.assertIn("may still run in the guest", said)
        self.assertIn("the run stops here", said)
        # The pattern is quoted inside the command: `tools/vm` interpolates its
        # argument into the guest script raw, so an unquoted `|` is a pipe in
        # the guest shell and `pgrep -af ninja | ctest` runs ctest.
        self.assertIn("tools/vm run \"pgrep -af 'ninja|ctest'\"", said)

    def test_a_build_failure_does_not_stop_the_run(self):
        # A mutant that does not compile costs one row, not the table.
        rows, _, _ = self.run_two({"ninja": (1, "error: unused parameter")},
                                  ["m1", "m2"])
        self.assertEqual([r["verdict"] for r in rows],
                         ["build-failed", "build-failed"])


class TestRestoreAndCheck(unittest.TestCase):
    """What the end of a run does, and what it does not do after a hang."""

    def finish(self, rows):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root)
        saved = make_tree(root)
        table = small_table(root)
        mutate.apply_mutation(root, table["mutations"][0])
        guest = FakeGuest(root, {}, ["restored-md5"])
        out = io.StringIO()
        with redirect_stdout(out):
            back = mutate.restore_and_check(table, guest, root, saved, rows,
                                            "base-md5")
        return back, guest, out.getvalue(), root

    def test_an_ordinary_run_restores_rebuilds_and_compares(self):
        back, guest, said, root = self.finish([{"decision": "D1.1",
                                                "verdict": "caught"}])
        self.assertEqual(guest.commands, ["ninja"])
        self.assertEqual(back, "restored-md5")
        self.assertIn("baseline base-md5 MISMATCH", said)
        self.assertEqual((Path(root) / "a.c").read_text(),
                         "int one(void) { return 1; }\n")

    def test_a_hung_run_restores_the_files_and_builds_nothing(self):
        # The rebuild would share the build directory with the command that
        # timed out, which is what stopping the run was for.
        back, guest, said, root = self.finish([{"decision": "D1.1",
                                                "verdict": "timeout"}])
        self.assertEqual(guest.commands, [])
        self.assertIn("restored the sources only", said)
        self.assertIn("was not measured", said)
        self.assertNotIn("MISMATCH", said)
        self.assertEqual((Path(root) / "a.c").read_text(),
                         "int one(void) { return 1; }\n")

    def test_a_hung_run_claims_no_md5_comparison(self):
        # `back` takes the baseline so that no MATCH or MISMATCH is printed; the
        # `timeout` row is what makes the exit status 1.
        back, _, _, _ = self.finish([{"decision": "D1.1", "verdict": "timeout"}])
        self.assertEqual(back, "base-md5")
        self.assertEqual(mutate.exit_status(
            [{"decision": "D1.1", "verdict": "timeout"}], [], back, "base-md5"), 1)


class TestExitStatus(unittest.TestCase):
    """A run that did not answer must not exit 0."""

    def rows(self, *verdicts):
        return [{"decision": "D%d.1" % i, "verdict": v}
                for i, v in enumerate(verdicts)]

    def test_a_run_whose_rows_all_answered_exits_0(self):
        rows = self.rows("caught", "survived", "caught")
        self.assertEqual(mutate.exit_status(rows, [], "md5", "md5"), 0)

    def test_a_build_failure_a_stale_binary_a_timeout_or_a_rotted_anchor_exits_1(self):
        for verdict in ("build-failed", "stale", "timeout", "anchor-stale"):
            rows = self.rows("caught", verdict)
            self.assertEqual(mutate.exit_status(rows, [], "md5", "md5"), 1, verdict)

    def test_an_only_that_selects_nothing_exits_1(self):
        self.assertEqual(mutate.exit_status([], [], "md5", "md5"), 1)
        self.assertEqual(mutate.exit_status(self.rows("caught"), ["D9.9"], "md5", "md5"), 1)

    def test_a_restore_that_does_not_come_back_exits_1(self):
        self.assertEqual(mutate.exit_status(self.rows("caught"), [], "other", "md5"), 1)


class TestCheckTable(unittest.TestCase):
    """`--check` reads no build and reports the anchors that rotted."""

    def check(self, table, root):
        out = io.StringIO()
        with redirect_stdout(out):
            status = mutate.check_table(table, root)
        return status, out.getvalue()

    def test_a_table_whose_anchors_all_match_is_clean(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            status, out = self.check(small_table(root), root)
            self.assertEqual(status, 0)
            self.assertIn("1 rows, 0 stale", out)

    def test_an_anchor_that_moved_is_stale_and_the_run_fails(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            (Path(root) / "a.c").write_text("int one(void) { return 0; }\n")
            status, out = self.check(small_table(root), root)
            self.assertEqual(status, 1)
            self.assertIn("D1.1: a.c: 0 matches", out)
            self.assertIn("1 rows, 1 stale", out)

    def test_an_anchor_that_appears_twice_is_stale(self):
        with tempfile.TemporaryDirectory() as root:
            make_tree(root)
            (Path(root) / "a.c").write_text("return 1;\nreturn 1;\n")
            status, out = self.check(small_table(root), root)
            self.assertEqual(status, 1)
            self.assertIn("D1.1: a.c: 2 matches", out)

    def test_a_file_that_is_gone_is_stale_and_is_not_read(self):
        with tempfile.TemporaryDirectory() as root:
            status, out = self.check(small_table(root), root)
            self.assertEqual(status, 1)
            self.assertIn("no such file", out)


class TestGuest(unittest.TestCase):
    """The one part that talks to a shell."""

    def test_it_returns_the_status_and_both_streams(self):
        with tempfile.TemporaryDirectory() as root:
            guest = mutate.Guest(root, "/bin/sh -c")
            status, out = guest.run("echo out; echo err 1>&2; exit 3")
            self.assertEqual(status, 3)
            self.assertIn("out", out)
            self.assertIn("err", out)

    def test_a_build_that_made_no_binary_has_no_md5(self):
        with tempfile.TemporaryDirectory() as root:
            guest = mutate.Guest(root, "/bin/sh -c")
            self.assertIsNone(guest.md5("no/such/binary"))

    def test_a_command_that_runs_too_long_reports_a_timeout(self):
        with tempfile.TemporaryDirectory() as root:
            guest = mutate.Guest(root, "/bin/sh -c")
            status, out = guest.run("sleep 30", timeout=1)
            self.assertEqual(status, mutate.TIMEOUT_STATUS)
            self.assertIn("ran longer than 1 s", out)


class TestTheRealTable(unittest.TestCase):
    """The table in the repository answers for its own shape and anchors.

    These tests read the **live** sources, so they must not run during a round:
    a round rewrites one of them, and a red test here would be recorded as the
    compiler's catch. The class skips on such a tree, and the table's second
    stage excludes this suite by name as well. A
    skip is visible: `unittest` prints `OK (skipped=...)`.
    """

    @classmethod
    def setUpClass(cls):
        with open(TABLE_PATH) as handle:
            cls.table = json.load(handle)
        applied = mutate.mutated_rows(cls.table, ROOT)
        if applied:
            raise unittest.SkipTest(
                "a mutation round holds the sources: %s" % ", ".join(applied))

    def test_every_row_carries_the_five_fields_a_round_reads(self):
        for row in self.table["mutations"]:
            for field in ("decision", "file", "old", "new", "what"):
                self.assertIn(field, row, row.get("decision", "?"))
            self.assertNotEqual(row["old"], row["new"], row["decision"])
            self.assertIn(row["file"], self.table["sources"], row["decision"])

    def test_no_decision_is_named_twice(self):
        names = [row["decision"] for row in self.table["mutations"]]
        self.assertEqual(len(names), len(set(names)))

    def test_every_row_of_the_table_leaves_no_row_stale_and_names_itself(self):
        """Every row, one at a time, applied the way a round applies it.

        Apply each row because a sample does not prove the complete table.
        """
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root)
        pristine = {}
        for name in self.table["sources"]:
            (Path(root) / name).parent.mkdir(parents=True, exist_ok=True)
            pristine[name] = (ROOT / name).read_bytes()
            (Path(root) / name).write_bytes(pristine[name])
        swept = 0
        for row in self.table["mutations"]:
            decision = row["decision"]
            mutate.apply_mutation(root, row)
            self.assertEqual(mutate.mutated_rows(self.table, root), [decision],
                             "%s: the guard does not see this round" % decision)
            out = io.StringIO()
            with redirect_stdout(out):
                status = mutate.check_table(self.table, root)
            self.assertEqual(status, 0, "%s:\n%s" % (decision, out.getvalue()))
            self.assertIn("a round is in progress: %s" % decision, out.getvalue())
            self.assertIn("%d rows, 0 stale" % len(self.table["mutations"]),
                          out.getvalue())
            (Path(root) / row["file"]).write_bytes(pristine[row["file"]])
            swept += 1
        self.assertEqual(swept, 88)

    def test_check_reports_no_stale_anchor_against_the_sources(self):
        out = io.StringIO()
        with redirect_stdout(out):
            status = mutate.check_table(self.table, str(ROOT))
        self.assertEqual(status, 0, out.getvalue())
        self.assertIn("88 rows, 0 stale", out.getvalue())


if __name__ == "__main__":
    unittest.main()
