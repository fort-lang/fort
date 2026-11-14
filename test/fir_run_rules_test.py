#!/usr/bin/env python3
"""Hold the two rules of test/fir_run_test.py that accept a difference between the interpreter and
the checked build: the memory fault at dead storage (D10.9 rule 2) and a pinned difference.

The test gives each rule the results of two runs as plain records and builds no program.
"""

import collections
import pathlib
import signal
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fir_run_test  # noqa: E402

Run = collections.namedtuple("Run", "returncode stdout stderr")
Program = collections.namedtuple("Program", "path signal_name")

DEAD = ["fort: event use after release in m.main at 18:19 through owner m.main _1"]
FAULT = -signal.SIGSEGV


def stops(program, want, events, got_stdout=b"before\nafter\n", got_stderr=b""):
    return fir_run_test.stops_at_dead_storage(
        program, want, events, got_stdout, got_stderr, want.stderr
    )


class DeadStorageFault(unittest.TestCase):
    def test_a_pinned_fault_at_dead_storage_is_accepted(self):
        for name, status in (("SEGV", -signal.SIGSEGV), ("BUS", -signal.SIGBUS)):
            self.assertTrue(stops(Program("p.ft", name), Run(status, b"", b""), DEAD))

    def test_a_fault_of_a_program_with_no_signal_directive_is_refused(self):
        # run/pointers/015 reads dead storage and must go on: a fault of its build is a difference
        self.assertFalse(stops(Program("p.ft", ""), Run(FAULT, b"", b""), DEAD))

    def test_a_fault_of_a_program_that_expects_another_signal_is_refused(self):
        for name in ("ABRT", "TRAP", "ILL", "FPE"):
            self.assertFalse(stops(Program("p.ft", name), Run(FAULT, b"", b""), DEAD))

    def test_a_fault_with_no_dead_access_is_refused(self):
        self.assertFalse(stops(Program("p.ft", "SEGV"), Run(FAULT, b"", b""), []))
        events = ["fort: event invalid release in m.main at 25:5"]
        self.assertFalse(stops(Program("p.ft", "SEGV"), Run(FAULT, b"", b""), events))

    def test_another_end_is_refused(self):
        for status in (0, 1, -signal.SIGABRT, -signal.SIGTRAP):
            self.assertFalse(stops(Program("p.ft", "SEGV"), Run(status, b"", b""), DEAD))

    def test_the_output_of_the_build_is_a_start_of_the_output_of_the_interpreter(self):
        program = Program("p.ft", "SEGV")
        self.assertTrue(stops(program, Run(FAULT, b"before\n", b""), DEAD))
        self.assertFalse(stops(program, Run(FAULT, b"other\n", b""), DEAD))
        self.assertFalse(stops(program, Run(FAULT, b"", b"a line\n"), DEAD))
        self.assertTrue(stops(program, Run(FAULT, b"", b"a li"), DEAD, got_stderr=b"a line\n"))


class PinnedDifference(unittest.TestCase):
    PINNED = (
        "reason",
        (b"one\n", b":3:5: runtime error: x\n"),
        (b"two\n", b":5:5: runtime error: x\n"),
    )
    ABORT = -signal.SIGABRT

    def holds(self, got, want):
        return fir_run_test.holds_pinned(self.PINNED, got, got.stderr, want, want.stderr)

    def test_the_pinned_texts_hold(self):
        got = Run(self.ABORT, b"one\n", b"p.ft:3:5: runtime error: x\n")
        want = Run(self.ABORT, b"two\n", b"p.ft:5:5: runtime error: x\n")
        self.assertTrue(self.holds(got, want))

    def test_a_change_of_one_run_is_a_difference(self):
        got = Run(self.ABORT, b"one\n", b"p.ft:3:5: runtime error: x\n")
        want = Run(self.ABORT, b"two\n", b"p.ft:5:5: runtime error: x\n")
        # the two runs agree: the pin is then wrong, and the script must say so
        self.assertFalse(self.holds(got, got))
        self.assertFalse(self.holds(want, want))
        self.assertFalse(self.holds(got._replace(stdout=b"one\nmore\n"), want))
        self.assertFalse(self.holds(got, want._replace(stderr=b"p.ft:6:5: runtime error: x\n")))
        self.assertFalse(self.holds(got, want._replace(returncode=0)))
        self.assertFalse(self.holds(got._replace(returncode=-signal.SIGSEGV), want))

    def test_each_pinned_program_stands_in_the_event_table(self):
        # a pinned program is a program with a defect, so its events are held too
        for path, (reason, got, want) in fir_run_test.PINNED_DIFFERENCES.items():
            self.assertIn(path, fir_run_test.EVENT_TABLE)
            self.assertTrue((fir_run_test.LANG / path).is_file(), path)
            self.assertTrue(reason)
            self.assertNotEqual(got, want)

    def test_the_table_holds_1_program(self):
        self.assertEqual(len(fir_run_test.PINNED_DIFFERENCES), 1)


class EventTable(unittest.TestCase):
    def test_each_program_of_the_table_is_a_file_with_events(self):
        for path, (defect, lines) in fir_run_test.EVENT_TABLE.items():
            self.assertTrue((fir_run_test.LANG / path).is_file(), path)
            self.assertTrue(defect)
            self.assertTrue(lines, path)

    def test_a_program_that_must_fault_expects_the_signal(self):
        # each program of the table that expects a memory fault has an access to dead storage
        import run_tests

        for path, (_, lines) in fir_run_test.EVENT_TABLE.items():
            test = run_tests.Test(path, path, "run")
            run_tests.load_test(fir_run_test.LANG, test)
            if test.signal_name in fir_run_test.FAULT_NAMES:
                self.assertTrue(any(line.startswith("use after release ") for line in lines), path)


if __name__ == "__main__":
    unittest.main()
