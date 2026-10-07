#!/usr/bin/env python3
"""Check three selected driver groups and the ownership pass of the FIR harness.

The groups are check against build, no C compiler, and pre-transform FIR.

Each group uses one program whose selected proof exits 1. The entry module holds checked
arithmetic and an overwrite, so the build-mode pass changes its FIR. An imported module holds a
function that no call reaches and that loses a local owner. These groups need no accepted
proof, so no case here expects exit 0 from a selected run.
"""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ownership_driver_test as base


OPTIONS = None

ENTRY = (
    "import lib;\n"
    "fn count(i32 n) i32 {\n"
    "    i32 mut* own mut p = new(i32);\n"
    "    p = new(i32);\n"
    "    del(p);\n"
    "    i32 mut value = n;\n"
    "    value += 1;\n"
    "    return value;\n"
    "}\n"
    "fn main() i32 { return count(1); }\n"
)

LIBRARY = (
    "fn uncalled() void {\n"
    "    i32 mut* own p = new(i32);\n"
    "}\n"
)

# The keys of a report that the selected analysis decides. The invocation differs by mode.
PROOF_KEYS = ("files", "enumeration", "analyses", "limits", "meters", "bodies", "totals",
              "first_incomplete", "failure", "verdict", "exit_status")

MODES = ((), ("--release",), ("--no-bounds-check",), ("--release", "--no-bounds-check"))


def fir_sizes(text):
    """Return the FIR size of each function in a FIR module text.

    The size counts the statements and the terminator of each block, as
    ownership_limits.fir_size does. Each statement and each terminator is one line.
    """
    sizes = {}
    name = None
    inside = False
    for line in text.splitlines():
        header = re.match(r"fn ([\w.]+)\(", line)
        if header:
            name = header.group(1)
            sizes[name] = 0
        elif re.match(r"\s+bb\d+: \{$", line):
            inside = True
        elif inside and line.strip() == "}":
            inside = False
        elif inside and line.strip():
            sizes[name] += 1
    return sizes


class DriverGroupsTest(unittest.TestCase):
    setUp = base.OwnershipDriverTest.setUp
    write = base.OwnershipDriverTest.write
    invoke = base.OwnershipDriverTest.invoke
    evidence = base.OwnershipDriverTest.evidence
    body = base.OwnershipDriverTest.body

    def program(self):
        self.entry.write_text(ENTRY)
        self.write("lib.ft", LIBRARY)
        return Path(OPTIONS.std_dir)

    def selected(self, *flags, check):
        if self.report.exists():
            self.report.unlink()
        result = self.invoke(*flags, check=check, standard=Path(OPTIONS.std_dir))
        self.assertEqual(result.returncode, 1, result.stderr)
        document = self.evidence()
        self.assertEqual(document["invocation"]["mode"]["check"], check)
        return document

    def lowered_size(self, *flags, after):
        result = subprocess.run(
            [OPTIONS.fort, "--std-dir", OPTIONS.std_dir, *flags, "--fir-after=" + after,
             str(self.entry)], cwd=self.root, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        return fir_sizes(result.stdout)["main.count"]

    def ledger_sizes(self, document, name):
        key = self.body(document, name)["key"]
        return {row["name"]: row["fir_size"] for row in document["meters"]
                if row["owner"] == key}

    # ---- check against build ------------------------------------------------------------

    def test_check_and_build_report_one_analysis(self):
        self.program()
        checked = self.selected(check=True)
        checked_stderr = self.result.stderr
        self.assertIn(b"lib.ft:3:1: error: lost ownership of 'p'", checked_stderr)
        self.assertIn(b"main.ft:4:7: error: lost ownership of 'p'", checked_stderr)
        output = self.root / "output"
        for flags in ((), ("-c",), ("-S",), ("-o", str(output))):
            with self.subTest(flags=flags):
                built = self.selected(*flags, check=False)
                self.assertEqual(self.result.stderr, checked_stderr)
                for key in PROOF_KEYS:
                    self.assertEqual(built[key], checked[key], key)
                for name in ("main.ll", "main.o", "a.out", "output"):
                    self.assertFalse((self.root / name).exists(), name)

    def test_check_json_keeps_the_build_text_ranges(self):
        self.program()
        self.selected(check=False)
        text = self.result.stderr.decode()
        self.selected("--json", check=True)
        document = json.loads(self.result.stdout)
        self.assertEqual(document["version"], 1)
        losses = [row for row in document["diagnostics"]
                  if row["message"] == "lost ownership of 'p'"]
        self.assertEqual(len(losses), 2)
        for row in losses:
            with self.subTest(file=row["file"], line=row["line"]):
                header = "%s:%d:%d: error: %s" % (row["file"], row["line"], row["col"],
                                                   row["message"])
                self.assertIn(header, text)
                # D20.4: a storage end at a closing brace has the range of that one byte.
                self.assertLess((row["line"], row["col"]), (row["end_line"], row["end_col"]))
        brace = next(row for row in losses if Path(row["file"]).name == "lib.ft")
        self.assertEqual((brace["line"], brace["col"], brace["end_line"], brace["end_col"]),
                         (3, 1, 3, 2))
        self.assertEqual([(note["line"], note["col"], note["end_col"])
                          for note in brace["notes"]], [(3, 1, 2)])

    # ---- no C compiler -------------------------------------------------------------------

    def recording_compiler(self):
        marker = self.root / "cc-started"
        cc = self.write("cc", '#!/bin/sh\nprintf "%s\\n" "$*" >> "' + str(marker) + '"\nexit 0\n')
        cc.chmod(0o700)
        return cc, marker

    def test_selected_build_never_starts_the_c_compiler(self):
        cc, marker = self.recording_compiler()
        self.program()
        for flags in ((), ("-c",), ("-o", str(self.root / "output"))):
            with self.subTest(flags=flags):
                self.selected("--cc", str(cc), *flags, check=False)
                self.assertFalse(marker.exists())
        # --check with the same --cc runs the same analysis and starts no compiler either.
        self.selected("--cc", str(cc), check=True)
        self.assertFalse(marker.exists())
        # A program with no violation still has an incomplete proof, and starts no compiler.
        self.entry.write_text("fn main() i32 { return 0; }\n")
        self.selected("--cc", str(cc), check=False)
        self.assertFalse(marker.exists())

    def test_the_recording_compiler_sees_an_unselected_build(self):
        # The control of the group above: the probe observes a start when one occurs.
        cc, marker = self.recording_compiler()
        self.entry.write_text("fn main() i32 { return 0; }\n")
        result = self.invoke("--cc", str(cc), selected=False, report=False, check=False,
                             standard=Path(OPTIONS.std_dir))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(marker.exists())
        self.assertEqual(len(marker.read_text().splitlines()), 1)

    # ---- pre-transform placement ---------------------------------------------------------

    def test_analysis_reads_the_fir_before_the_build_mode_pass(self):
        self.program()
        lowered = self.lowered_size(after="lower")
        # The release build-mode pass removes checks, so a later placement has another size.
        self.assertNotEqual(self.lowered_size("--release", after="build-mode"), lowered)
        self.assertNotEqual(self.lowered_size("--no-bounds-check", "--release",
                                              after="build-mode"), lowered)
        first = None
        for check in (True, False):
            for flags in MODES:
                with self.subTest(check=check, flags=flags):
                    document = self.selected(*flags, check=check)
                    # Each ledger records the FIR size of the body that its computation
                    # reads. The raw analysis reads the lowered FIR as the local one does.
                    self.assertEqual(self.ledger_sizes(document, "count"),
                                     {"services": lowered, "local": lowered, "raw": lowered})
                    projection = ({key: document[key] for key in PROOF_KEYS},
                                  self.result.stderr)
                    if first is None:
                        first = projection
                    self.assertEqual(projection, first)

    def test_fir_sizes_reads_each_block_line(self):
        text = (
            "fn main.f() -> i32 {\n"
            "    let _0: i32;\n"
            "\n"
            "    bb0: {\n"
            "        _0 = const i32 1 #1:1 s0;\n"
            "        goto -> bb1 #1:1 s0;\n"
            "    }\n"
            "    bb1: {\n"
            "        return #1:1 s0;\n"
            "    }\n"
            "}\n"
            "\n"
            "fn main.g() -> void {\n"
            "    let _0: void;\n"
            "\n"
            "    bb0: {\n"
            "        return #2:1 s0;\n"
            "    }\n"
            "}\n"
        )
        self.assertEqual(fir_sizes(text), {"main.f": 3, "main.g": 1})


V5_BODY = (
    "fn main.f(_1 (x): i32) -> i32 {\n"
    "    bb0: {\n"
    "        _0 = move _1 #2:12 s1;\n"
    "        return #2:5 s1;\n"
    "    }\n"
    "}\n"
)

LEAK_BODY = (
    "fn main.leak() -> void {\n"
    "    let _1 (p): i32 mut* own #1:1;\n"
    "\n"
    "    bb0: {\n"
    "        live(_1) #1:1 s0;\n"
    "        _1 = alloc<i32>(const u64 1) #1:1 s0;\n"
    "        dead(_1) #1:1 s1;\n"
    "        return #1:1 s1;\n"
    "    }\n"
    "}\n"
)


class FirOwnershipPassTest(unittest.TestCase):
    """The pass `ownership-local` of `fort --fir-test` reports errors with an exit status.

    A broken verifier rule gives exit 2 and an ownership error gives exit 1; neither is a panic.
    """

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="fort-fir-ownership-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def run_test(self, passes, body):
        path = self.root / "case.fir"
        path.write_text("".join("//! pass: %s\n" % name for name in passes) + body)
        return subprocess.run([OPTIONS.fort, "--std-dir", OPTIONS.std_dir, "--fir-test",
                               str(path)], cwd=self.root, capture_output=True, text=True,
                              timeout=120, check=False)

    def test_a_broken_verifier_rule_exits_two_without_a_panic(self):
        result = self.run_test(["ownership-local"], V5_BODY)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("fort: error: fir.verify: V5 a move of a place whose type does not own: "
                      "main.f bb0 statement 0 (s1)", result.stderr)
        self.assertNotIn("panic", result.stderr)

    def test_the_verify_pass_still_panics_on_the_same_rule(self):
        # The control of the case above: the same text under `verify` ends with SIGABRT.
        result = self.run_test(["verify"], V5_BODY)
        self.assertLess(result.returncode, 0)
        self.assertIn("fir.verify: V5", result.stderr)

    def test_a_source_ownership_error_exits_one_with_no_module(self):
        result = self.run_test(["ownership-local"], LEAK_BODY)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("case.fir:1:1: error: lost ownership of 'p'", result.stderr)
        # The verifier finds no rule broken in the same text.
        self.assertEqual(self.run_test(["verify"], LEAK_BODY).returncode, 0)

    def test_the_pass_runs_before_the_build_mode_pass(self):
        result = self.run_test(["build-mode", "ownership-local"], LEAK_BODY)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertIn("fort: error: the pass 'ownership-local' runs before the pass 'build-mode'",
                      result.stderr)
        before = self.run_test(["ownership-local", "build-mode"], LEAK_BODY)
        self.assertEqual(before.returncode, 1, before.stderr)
        self.assertIn("case.fir:1:1: error: lost ownership of 'p'", before.stderr)

    def test_the_pass_takes_no_argument(self):
        result = self.run_test(["ownership-local --release"], LEAK_BODY)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("fort: error: the pass 'ownership-local' takes no argument", result.stderr)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True)
    parser.add_argument("--std-dir", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--opt", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.fort = str(Path(OPTIONS.fort).resolve())
    OPTIONS.std_dir = str(Path(OPTIONS.std_dir).resolve())
    base.OPTIONS = OPTIONS
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
