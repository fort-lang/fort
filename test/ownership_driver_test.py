#!/usr/bin/env python3
"""Check actual compiler ownership reports and selected driver behavior."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import re
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ownership_audit as audit


CHECKOUT = Path(__file__).resolve().parents[1]
OPTIONS = None


class OwnershipDriverTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fort-ownership-driver-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.standard = self.root / "std"
        self.standard.mkdir()
        (self.standard / "rt.ft").write_text("// Empty fixture runtime.\n")
        self.entry = self.write("main.ft", "fn main() i32 { return 0; }\n")
        self.report = self.root / "report.json"
        self.version = subprocess.check_output([OPTIONS.fort, "--version"], text=True).strip()
        self.target = "x86_64-linux-gnu" if sys.platform == "linux" else "arm64-apple-macosx11.0.0"

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text if isinstance(text, bytes) else text.encode("utf-8"))
        return path

    def invoke(self, *flags, selected=True, report=True, check=True, standard=None, entry=None,
               process_setup=None, sources=()):
        self.standard_used = standard or self.standard
        self.entry_used = entry or self.entry
        argv = [OPTIONS.fort, "--std-dir", str(self.standard_used)]
        if check:
            argv.append("--check")
        if selected:
            argv.append("--ownership-check")
        if report:
            argv.extend(("--ownership-report", str(self.report)))
        argv.extend(flags)
        argv.append(str(self.entry_used))
        self.argv = argv
        self.candidates = audit.snapshot([self.root, self.standard_used, *sources])
        self.result = subprocess.run(
            argv, cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            timeout=120, preexec_fn=process_setup,
        )
        for name, contents in self.candidates.items():
            self.assertEqual(Path(name).read_bytes(), contents, name)
        return self.result

    def evidence(self, *, complete=True, target=None, cfg=()):
        self.assertTrue(self.report.is_file(), self.result.stderr.decode())
        document = audit.read_json(self.report)[0]
        arguments = dict(
            argv=self.argv, cwd=self.root,
            root=audit.normalize(str(self.entry_used), self.root, CHECKOUT, self.standard_used),
            target=target or self.target,
            cfg=audit.effective_configuration(target or self.target, cfg), checkout=CHECKOUT,
            standard=self.standard_used, exit_status=self.result.returncode,
            candidates=self.candidates, limits=audit.production_limits(CHECKOUT),
            compiler_version=self.version,
        )
        if complete:
            audit.validate_report(document, **arguments)
        else:
            with self.assertRaises(audit.FailedReportAttempt):
                audit.validate_report(document, **arguments)
        self.assertFalse(list(self.root.rglob("*.tmp.*")))
        return document

    def body(self, document, name):
        rows = [row for row in document["bodies"] if row["source"]["name"] == name]
        self.assertEqual(len(rows), 1, name)
        return rows[0]

    def test_bare_selection_requires_complete_proof(self):
        result = self.invoke(report=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"ownership proof is incomplete", result.stderr)
        self.assertEqual(result.stdout, b"")
        self.assertFalse(self.report.exists())

    def test_actual_report_separates_solver_and_proof(self):
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        body = self.body(document, "main")
        self.assertEqual(body["analyses"][0]["solver"], "complete")
        self.assertEqual(body["analyses"][0]["proof"], "incomplete")
        self.assertEqual(body["ownership"], "incomplete")
        self.assertEqual(document["verdict"], "rejected")
        self.assertEqual(document["totals"]["violations"], 0)

    def test_empty_closure_runs_each_producer(self):
        self.entry.write_text("i32 VALUE = 1;\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertEqual(document["bodies"], [])
        self.assertEqual(document["enumeration"]["selected_bodies"], 0)
        self.assertEqual(len(document["analyses"]), 7)
        # The local, stored-borrow and raw analyses have no closure facts outside bodies, so no
        # body leaves no gap of theirs.
        self.assertEqual(document["analyses"][2], {"name": "local", "producer": "integrated",
                                                   "status": "complete"})
        self.assertEqual(document["analyses"][3], {"name": "stored_borrows",
                                                   "producer": "integrated",
                                                   "status": "complete"})
        self.assertEqual(document["analyses"][4], {"name": "raw", "producer": "integrated",
                                                   "status": "complete"})
        # The calls_heap solvers run on the empty closure and solve no component.
        self.assertEqual(document["analyses"][5], {"name": "calls_heap",
                                                   "producer": "integrated",
                                                   "status": "complete"})
        # The process-exit closure holds the boundary too. A library closure without an owning
        # global needs no cleanup body.
        self.assertEqual(document["analyses"][6], {"name": "process_exit",
                                                   "producer": "integrated",
                                                   "status": "complete"})
        self.assertEqual(document["boundary"], {"kind": "library", "leaves": 0,
                                                "correspondence": "complete",
                                                "proof": "complete",
                                                "first_incomplete": None})
        # The call-graph step of the boundary runs on the empty closure, as the graph build does.
        self.assertEqual([row["name"] for row in document["meters"]],
                         ["graph_private", "process_exit_fills"])

    def test_diagnostic_document_stays_version_one(self):
        self.assertEqual(self.invoke("--json").returncode, 1)
        coverage = self.evidence()
        diagnostic = json.loads(self.result.stdout)
        self.assertEqual(set(diagnostic), {"version", "files", "diagnostics", "symbols"})
        self.assertEqual(diagnostic["version"], 1)
        self.assertEqual(diagnostic["symbols"], [])
        self.assertIn("ownership proof is incomplete", diagnostic["diagnostics"][0]["message"])
        self.assertEqual(coverage["kind"], "fort-ownership-report")
        # Version 2 added the W scale and FIR sizes, version 3 the local ledgers, version 4 the
        # raw ledgers, version 5 the stored-borrow ledgers, version 6 the process-exit ledgers
        # and the boundary, and version 7 the calls_heap ledgers.
        self.assertEqual(coverage["version"], 7)
        self.assertIn("w_scale", coverage["limits"])

    def test_index_still_populates_identifier_records(self):
        self.assertEqual(self.invoke("--index").returncode, 1)
        self.evidence()
        diagnostic = json.loads(self.result.stdout)
        self.assertEqual(diagnostic["version"], 1)
        self.assertTrue(diagnostic["symbols"])

    def test_report_requires_selection(self):
        result = self.invoke(selected=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"--ownership-report requires --ownership-check", result.stderr)
        self.assertFalse(self.report.exists())

    def test_report_argument_is_required(self):
        argv = [OPTIONS.fort, str(self.entry), "--ownership-check", "--ownership-report"]
        result = subprocess.run(argv, cwd=self.root, capture_output=True, check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"missing argument for option", result.stderr)

    def test_inspection_modes_refuse_selection(self):
        for flag in ("--tokens", "--ast", "--fir", "--fir-after=lower",
                     "--fir-after=build-mode", "--fir-test", "--fir-verify-report"):
            with self.subTest(flag=flag):
                result = self.invoke(flag, check=False)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")
                self.assertFalse(self.report.exists())

    def test_no_stage_selection_option_exists(self):
        result = self.invoke("--ownership-stage=local")
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"unknown option", result.stderr)
        self.assertFalse(self.report.exists())

    def test_last_report_path_wins(self):
        last = self.root / "last.json"
        self.assertEqual(self.invoke("--ownership-report", str(last)).returncode, 1)
        self.assertFalse(self.report.exists())
        self.report = last
        self.evidence()

    def test_configuration_and_original_arguments_are_retained(self):
        flags = ("--cfg", "zeta=last,alpha=first", "--cfg", "middle=",
                 "-I", str(self.root), "-Xcc", "-g", "-lm")
        self.assertEqual(self.invoke(*flags).returncode, 1)
        document = self.evidence(cfg=("zeta=last,alpha=first", "middle="))
        self.assertEqual(document["invocation"]["argv"], self.argv[1:])
        keys = [row["key"] for row in document["invocation"]["configuration"]]
        self.assertEqual(keys, sorted(keys))
        self.assertIn({"key": "middle", "value": ""}, document["invocation"]["configuration"])

    def test_selected_build_creates_no_output_or_build_temporary(self):
        temporary = self.root / "build-temporary"
        temporary.mkdir()
        previous = os.environ.get("TMPDIR")
        os.environ["TMPDIR"] = str(temporary)
        self.addCleanup(self.restore_environment, "TMPDIR", previous)
        for flags in ((), ("-S",), ("-c",), ("-S", "-c"), ("--fir-stats",)):
            with self.subTest(flags=flags):
                if self.report.exists():
                    self.report.unlink()
                self.assertEqual(self.invoke(*flags, check=False).returncode, 1)
                self.evidence()
                self.assertEqual(list(temporary.iterdir()), [])
                for name in ("main.ll", "main.o", "a.out"):
                    self.assertFalse((self.root / name).exists())

    def test_selected_build_does_not_start_the_c_compiler(self):
        marker = self.root / "cc-started"
        cc = self.write("cc", '#!/bin/sh\nprintf started > "' + str(marker) + '"\nexit 0\n')
        cc.chmod(0o700)
        result = self.invoke("--cc", str(cc), check=False)
        self.assertEqual(result.returncode, 1)
        self.evidence()
        self.assertFalse(marker.exists())

    def test_rejected_build_preserves_existing_output(self):
        output = self.write("main.ll", b"retained output\n")
        self.assertEqual(self.invoke("-S", check=False).returncode, 1)
        self.evidence()
        self.assertEqual(output.read_bytes(), b"retained output\n")

    def test_mode_options_leave_proof_outcomes_identical(self):
        projections = []
        for flags in ((), ("--release",), ("--no-bounds-check",),
                      ("--release", "--no-bounds-check")):
            self.assertEqual(self.invoke(*flags).returncode, 1)
            document = self.evidence()
            projections.append({key: document[key] for key in
                                ("analyses", "bodies", "totals", "first_incomplete", "failure")})
        self.assertTrue(all(value == projections[0] for value in projections))

    def test_unselected_ir_still_verifies(self):
        result = self.invoke("-S", selected=False, report=False, check=False,
                             standard=Path(OPTIONS.std_dir))
        self.assertEqual(result.returncode, 0, result.stderr)
        path = self.root / "main.ll"
        verified = subprocess.run([OPTIONS.opt, "-passes=verify", "-disable-output", str(path)],
                                  capture_output=True, check=False)
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertNotIn(b"ownership_", path.read_bytes())

    def test_unselected_check_retains_existing_success(self):
        result = self.invoke(selected=False, report=False)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(result.stderr, b"")

    def test_uncalled_import_and_runtime_bodies_remain_selected(self):
        self.write("other.ft", "fn uncalled() void {}\nfn another() void {}\n")
        (self.standard / "rt.ft").write_text("fn runtime_uncalled() void {}\n")
        self.entry.write_text("import other;\nfn main() i32 { return 0; }\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertEqual({row["source"]["name"] for row in document["bodies"]},
                         {"uncalled", "another", "runtime_uncalled", "main"})
        self.assertEqual(len(document["files"]), 3)

    def test_available_runtime_uses_the_selected_standard_root(self):
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        runtime = [row for row in document["files"] if row["module"] == "std.rt"]
        self.assertEqual(len(runtime), 1)
        self.assertEqual(Path(runtime[0]["path"]).resolve(), Path(OPTIONS.std_dir) / "rt.ft")
        self.assertTrue(any(row["source"]["module"] == "std.rt" for row in document["bodies"]))

    def test_target_selection_excludes_inactive_bodies(self):
        self.entry.write_text(
            '$if ($cfg(target_os) == "linux") { fn linux_only() void {} }\n'
            'else { fn mac_only() void {} }\nfn main() i32 { return 0; }\n'
        )
        for target, selected in (("x86_64-linux-gnu", "linux_only"),
                                 ("arm64-apple-macosx11.0.0", "mac_only")):
            self.assertEqual(self.invoke("--target", target).returncode, 1)
            document = self.evidence(target=target)
            self.assertEqual({row["source"]["name"] for row in document["bodies"]},
                             {selected, "main"})
            self.assertEqual(document["enumeration"]["inactive_declarations"], 1)

    def test_direct_access_branches_and_loops_use_actual_source(self):
        self.entry.write_text(
            "fn main() i32 { i32 mut value = 0; while (value < 2) { value += 1; }\n"
            "if (value == 2) { value = 3; } return value; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        row = self.body(self.evidence(), "main")
        self.assertEqual(row["analyses"][1]["correspondence"], "complete")
        self.assertEqual(row["analyses"][1]["solver"], "complete")

    def test_unknown_fort_call_effects_remain_incomplete(self):
        self.entry.write_text(
            "fn target(i32 value) i32 { return value; }\n"
            "fn main() i32 { return target(2); }\n"
        )
        self.assertEqual(self.invoke().returncode, 1)
        row = self.body(self.evidence(), "main")
        self.assertEqual(row["analyses"][1]["correspondence"], "incomplete")
        self.assertEqual(row["analyses"][1]["proof"], "incomplete")

    def test_trusted_extern_needs_no_foreign_body(self):
        self.entry.write_text(
            "extern fn external(i32 value) i32;\n"
            "fn main() i32 { return external(2); }\n"
        )
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertEqual(len(document["bodies"]), 1)
        row = self.body(document, "main")
        self.assertEqual(row["analyses"][1]["correspondence"], "complete")
        self.assertNotIn("missing_ffi", self.report.read_text())

    def test_indirect_access_keeps_incomplete_correspondence(self):
        self.entry.write_text("fn read(i32* value) i32 { return *value; }\n")
        self.assertEqual(self.invoke().returncode, 1)
        row = self.body(self.evidence(), "read")
        self.assertEqual(row["analyses"][1]["correspondence"], "incomplete")
        self.assertEqual(row["analyses"][1]["proof"], "incomplete")

    def test_canonical_keys_include_non_body_declarations(self):
        self.entry.write_text(
            "extern fn external() void;\nstruct item { i32 value; }\ni32 VALUE = 1;\n"
            "fn first() void {}\nfn second() void {}\n"
        )
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertEqual(self.body(document, "first")["key"]["declaration"], 3)
        self.assertEqual(self.body(document, "second")["key"]["declaration"], 4)

    def test_lone_carriage_return_and_tabs_keep_byte_columns(self):
        self.entry.write_bytes(b"\r\tfn main() i32 { return 0; }\n")
        self.assertEqual(self.invoke().returncode, 1)
        row = self.body(self.evidence(), "main")
        self.assertEqual((row["source"]["line"], row["source"]["col"]), (1, 6))

    def test_overlapping_closures_keep_source_identity_and_local_keys(self):
        shared = self.write("shared.ft", "fn shared() void {}\n")
        self.entry.write_text("import shared;\nfn main() i32 { return 0; }\n")
        self.assertEqual(self.invoke().returncode, 1)
        first = self.evidence()
        original = self.body(first, "shared")
        self.write("earlier.ft", "fn earlier() void {}\n")
        other = self.write("other.ft", "import earlier;\nimport shared;\nfn other() void {}\n")
        self.assertEqual(self.invoke(entry=other).returncode, 1)
        second = self.evidence()
        shifted = self.body(second, "shared")
        names = ("line", "col", "end_line", "end_col", "name", "instance")
        self.assertEqual(tuple(original["source"][key] for key in names),
                         tuple(shifted["source"][key] for key in names))
        self.assertNotEqual(original["key"]["module"], shifted["key"]["module"])
        self.assertEqual(shared.read_text(), "fn shared() void {}\n")

    def test_checking_failure_retains_failed_and_unexecuted_rows(self):
        self.entry.write_text("fn good() void {}\nfn bad() void { missing(); }\n")
        self.assertEqual(self.invoke("--json").returncode, 1)
        document = self.evidence(complete=False)
        self.assertFalse(document["enumeration"]["complete"])
        self.assertEqual(len(document["bodies"]), 2)
        self.assertEqual(document["meters"], [])
        self.assertEqual(document["failure"]["code"], "source_error")
        for row in document["bodies"]:
            self.assertEqual(row["checking"], "failed")
            self.assertEqual(row["lowering"], "unexecuted")
            self.assertEqual(row["key"], None)
            self.assertTrue(all(value["proof"] == "unexecuted" for value in row["analyses"]))
        self.assertEqual(json.loads(self.result.stdout)["version"], 1)

    def test_parse_failure_preserves_an_earlier_body(self):
        self.entry.write_text("fn discovered() void {}\nfn malformed( void {}\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence(complete=False)
        self.assertIn("discovered", [row["source"]["name"] for row in document["bodies"]])
        self.assertEqual(document["meters"], [])

    def test_missing_import_retains_incomplete_enumeration(self):
        self.entry.write_text("import absent;\nfn discovered() void {}\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence(complete=False)
        self.assertFalse(document["enumeration"]["complete"])
        self.assertEqual(document["failure"]["code"], "source_error")

    def test_checking_failure_without_a_body_ledger_keeps_required_outcomes(self):
        self.entry.write_text("i32 VALUE = missing;\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence(complete=False)
        self.assertEqual(document["bodies"], [])
        self.assertEqual(document["meters"], [])
        self.assertEqual([row["status"] for row in document["analyses"]][3:],
                         ["unexecuted", "unexecuted", "unexecuted", "unexecuted"])
        # The local, stored-borrow, raw, calls_heap and process-exit analyses ran on no body and
        # the denominator is incomplete. The boundary reads every body, so it did not run either.
        self.assertEqual(document["analyses"][2]["status"], "unexecuted")
        self.assertEqual((document["boundary"]["correspondence"], document["boundary"]["proof"]),
                         ("unexecuted", "unexecuted"))

    def test_actual_verifier_failure_exits_two_and_retains_unexecuted_rows(self):
        # The empty runtime lacks the checked arithmetic trap entry.
        self.entry.write_text("fn main() i32 { i32 mut value = 1; value += 1; return value; }\n")
        self.assertEqual(self.invoke("--json").returncode, 2)
        document = self.evidence(complete=False)
        self.assertEqual(document["failure"]["code"], "verification_failure")
        self.assertEqual(document["verdict"], "failed")
        row = self.body(document, "main")
        self.assertEqual(row["verification"], "failed")
        self.assertTrue(all(value["proof"] == "unexecuted" for value in row["analyses"]))
        self.assertEqual(document["meters"], [])
        self.assertEqual(self.result.stdout, b"")

    def test_unsupported_lowering_retains_complete_denominator(self):
        self.entry.write_text("fn good() void {}\nfn unsupported() void { println(1); }\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertTrue(document["enumeration"]["complete"])
        self.assertEqual(document["failure"]["code"], "unsupported_lowering")
        row = self.body(document, "unsupported")
        self.assertEqual((row["lowering"], row["verification"]), ("failed", "unexecuted"))
        self.assertTrue(all(value["solver"] == "unexecuted" for value in row["analyses"]))

    def test_actual_service_work_refusal_keeps_the_first_location(self):
        # Liveness work grows with the square of the declarations, and the W bound with their
        # count, so 4000 declarations exhaust the bound of their FIR size.
        locals_text = "".join(f"i32 local{index} = 0;\n" for index in range(4000))
        self.entry.write_text("fn many_locals() void {\n" + locals_text + "}\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        meter = next(row for row in document["meters"] if row["name"] == "services")
        refusal = meter["first_refusal"]
        self.assertEqual(refusal["code"], "work_limit")
        # The body is one computation of 12000 stores in one block. Its W bound follows that
        # size.
        limits = document["limits"]
        self.assertEqual(meter["fir_size"], 12001)
        bound = limits["w"] + limits["w_scale"] * meter["fir_size"]
        self.assertEqual(refusal["source"]["line"], 2763)
        self.assertEqual(refusal["limit"]["used"], bound)
        self.assertEqual(refusal["limit"]["bound"], bound)
        self.assertIn(b"liveness: work_limit", self.result.stderr)
        expected = (f"ownership proof is incomplete (liveness: work_limit; category W; "
                    f"used {refusal['limit']['used']}; bound {refusal['limit']['bound']})")
        self.assertIn(expected.encode(), self.result.stderr)
        self.assertEqual(self.invoke("--json").returncode, 1)
        repeated = self.evidence()
        self.assertEqual(next(row for row in repeated["meters"] if row["name"] == "services")
                         ["first_refusal"], refusal)
        diagnostics = json.loads(self.result.stdout)["diagnostics"]
        matches = [row for row in diagnostics if row["message"] == expected]
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]["line"], 2763)

    LOCAL_CASES = (
        "fn leak() void {\n"
        "    i32 mut* own p = new(i32);\n"
        "}\n"
        "fn reuse() i32 {\n"
        "    i32 mut* own p = new(i32);\n"
        "    i32* q = p;\n"
        "    del(p);\n"
        "    return *q;\n"
        "}\n"
        "fn overwrite() void {\n"
        "    i32 mut* own mut p = new(i32);\n"
        "    p = new(i32);\n"
        "    del(p);\n"
        "}\n"
        "fn safe() i32 {\n"
        "    i32 mut* own p = new(i32);\n"
        "    *p = 7;\n"
        "    i32 v = *p;\n"
        "    del(p);\n"
        "    return v;\n"
        "}\n"
        "fn main() i32 { return 0; }\n"
    )

    def local_row(self, document, name):
        return self.body(document, name)["analyses"][2]

    def test_local_violations_are_rendered_and_counted(self):
        self.entry.write_text(self.LOCAL_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        self.assertIn("main.ft:3:1: error: lost ownership of 'p'", stderr)
        self.assertIn("main.ft:3:1: note: storage ends with a live owner", stderr)
        self.assertIn("main.ft:8:12: error: invalid use of 'q'", stderr)
        self.assertIn("main.ft:8:12: note: reference source is no longer live", stderr)
        self.assertIn("main.ft:12:7: error: lost ownership of 'p'", stderr)
        self.assertIn("main.ft:12:7: note: move would discard a live owner", stderr)
        # Violations come before the one closure reason, in body order.
        self.assertLess(stderr.index("lost ownership of 'p'"), stderr.index("invalid use"))
        self.assertLess(stderr.index("invalid use"), stderr.index("ownership proof is incomplete"))
        self.assertEqual(document["totals"]["violations"], 3)
        self.assertEqual(document["totals"]["analyses"][2]["violations"], 3)
        self.assertEqual(document["totals"]["analyses"][2]["proof"]["violated"], 3)
        for name in ("leak", "reuse", "overwrite"):
            with self.subTest(name=name):
                row = self.local_row(document, name)
                self.assertEqual(row, {"name": "local", "correspondence": "complete",
                                       "solver": "complete", "proof": "violated",
                                       "violations": 1})
                self.assertEqual(self.body(document, name)["ownership"], "violated")
                self.assertEqual(self.body(document, name)["violations"], 1)
        self.assertEqual(self.local_row(document, "safe")["proof"], "complete")
        self.assertEqual(self.body(document, "safe")["ownership"], "incomplete")
        self.assertEqual(document["analyses"][2]["producer"], "integrated")
        self.assertEqual(document["analyses"][2]["status"], "incomplete")
        self.assertEqual(document["verdict"], "rejected")

    def test_local_ledgers_pair_with_service_ledgers(self):
        self.entry.write_text(self.LOCAL_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        services = {json.dumps(row["owner"], sort_keys=True): row
                    for row in document["meters"] if row["name"] == "services"}
        local = [row for row in document["meters"] if row["name"] == "local"]
        self.assertEqual(len(local), len(services))
        for row in local:
            paired = services[json.dumps(row["owner"], sort_keys=True)]
            self.assertEqual(row["fir_size"], paired["fir_size"])
            counts = {count["category"]: count for count in row["counts"]}
            self.assertEqual(sorted(counts), sorted("DRPGHTEWV"))
            limits = document["limits"]
            self.assertEqual(counts["W"]["bound"], limits["w"] + limits["w_scale"] *
                             row["fir_size"])
            self.assertIsNone(row["first_refusal"])
        # The violated body retained one event and rendered an error and a note.
        leak = self.body(document, "leak")["key"]
        ledger = next(row for row in local if row["owner"] == leak)
        used = {count["category"]: count["used"] for count in ledger["counts"]}
        self.assertEqual(used["V"], 3)
        self.assertGreater(used["W"], 0)

    def test_local_diagnostics_keep_json_version_one(self):
        self.entry.write_text(self.LOCAL_CASES)
        self.assertEqual(self.invoke("--json", standard=Path(OPTIONS.std_dir)).returncode, 1)
        self.evidence()
        diagnostic = json.loads(self.result.stdout)
        self.assertEqual(diagnostic["version"], 1)
        messages = [(row["line"], row["message"]) for row in diagnostic["diagnostics"]]
        self.assertIn((3, "lost ownership of 'p'"), messages)
        self.assertIn((8, "invalid use of 'q'"), messages)
        self.assertIn((12, "lost ownership of 'p'"), messages)
        notes = [note["message"] for row in diagnostic["diagnostics"]
                 for note in row.get("notes", [])]
        self.assertIn("reference source is no longer live", notes)

    def test_local_verdicts_stay_identical_in_every_mode(self):
        self.entry.write_text(self.LOCAL_CASES)
        projections = []
        for flags in ((), ("--release",), ("--no-bounds-check",),
                      ("--release", "--no-bounds-check")):
            with self.subTest(flags=flags):
                self.assertEqual(self.invoke(*flags, standard=Path(OPTIONS.std_dir)).returncode,
                                 1)
                document = self.evidence()
                projections.append(([row["analyses"][2] for row in document["bodies"]],
                                    document["totals"]["violations"],
                                    self.result.stderr))
        self.assertTrue(all(value == projections[0] for value in projections))
        self.assertEqual(projections[0][1], 3)

    def test_local_failures_after_unknown_calls_stay_unproved(self):
        # The parameter of helper owns, so each call of it is a barrier (D17.9, D3.14).
        self.entry.write_text(
            "fn helper(i32 mut* own x) void { del(x); }\n"
            "fn after_call() void {\n"
            "    i32 mut* own p = new(i32);\n"
            "    helper(null);\n"
            "}\n"
            "fn caller_storage(i32* v) i32 {\n"
            "    helper(null);\n"
            "    return *v;\n"
            "}\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        # For the local analysis the leak after a fort call has only a possible
        # continuation. The failure stays in the local scope, because it fails without the
        # call's effects too.
        after = self.local_row(document, "after_call")
        self.assertEqual((after["correspondence"], after["proof"]), ("complete", "incomplete"))
        # The calls_heap analysis applies the complete summary of `helper`, so it validates
        # the leak: the one violation of the program.
        calls = self.body(document, "after_call")["analyses"][5]
        self.assertEqual((calls["correspondence"], calls["proof"], calls["violations"]),
                         ("complete", "violated", 1))
        self.assertEqual(document["totals"]["violations"], 1)
        self.assertIn(b"main.ft:5:1: error: lost ownership", self.result.stderr)
        # The read of caller storage fails only because of the call: that is call scope.
        caller = self.local_row(document, "caller_storage")
        self.assertEqual((caller["correspondence"], caller["proof"]),
                         ("incomplete", "incomplete"))

    def test_caller_storage_after_a_lending_call_stays_unproved(self):
        # The text of this test before the call rule: helper has no parameter, so the walk
        # crosses it. The leak after it is a violation, and the read of caller storage after
        # it still fails only because of the call: call scope, no violation.
        self.entry.write_text(
            "fn helper() void {}\n"
            "fn after_call() void {\n"
            "    i32 mut* own p = new(i32);\n"
            "    helper();\n"
            "}\n"
            "fn caller_storage(i32* v) i32 {\n"
            "    helper();\n"
            "    return *v;\n"
            "}\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        self.assertEqual(document["totals"]["violations"], 1)
        self.assertIn(b"lost ownership of 'p'", self.result.stderr)
        after = self.local_row(document, "after_call")
        self.assertEqual((after["correspondence"], after["proof"]), ("complete", "violated"))
        caller = self.local_row(document, "caller_storage")
        self.assertEqual((caller["correspondence"], caller["proof"]),
                         ("incomplete", "incomplete"))

    def test_local_leak_after_a_lending_call_is_a_violation(self):
        # A callee without an owning parameter takes no ownership (D17.9, D3.14): the walk
        # takes its normal return, and the leak after the call is a validated violation.
        self.entry.write_text(
            "fn consume(u8@ buf) u64 { return buf.len; }\n"
            "fn main() i32 {\n"
            "    u8 mut@ own buf = new(u8, 16);\n"
            "    consume(buf);\n"
            "    return 0;\n"
            "}\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        self.assertEqual(document["totals"]["violations"], 1)
        self.assertIn(b"lost ownership of 'buf'", self.result.stderr)
        row = self.local_row(document, "main")
        self.assertEqual((row["correspondence"], row["proof"]), ("complete", "violated"))

    CALLS_CASES = (
        "fn reader(i32* p) i32 { return *p; }\n"
        "fn released(i32 mut* own p) i32 { i32* v = p; del(p); return reader(v); }\n"
        "fn releaser(i32 mut* own p) void { del(p); }\n"
        "fn cross(i32 mut* own p) i32 { i32* v = p; releaser(move(p)); return *v; }\n"
        "fn noop() void { }\n"
        "fn known(i32 mut* own p) void { noop(); }\n"
        "fn sw(i32* p, i32* q, bool b) i32* { if (b) { return p; } return sw(q, p, true); }\n"
        "fn same(i32 mut* own a) i32 { i32* v = sw(a, a, false); del(a); return *v; }\n"
        "fn keep(fn () void g, i32 mut* own p) void { g(); }\n"
        "fn main() i32 { return 0; }\n"
    )

    def test_calls_heap_validates_cross_call_violations(self):
        self.entry.write_text(self.CALLS_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        # Each violation that only call summaries can see: a callee reads released storage, a
        # callee releases the owner of a view, and a known callee keeps the obligation.
        expected = (
            ("released", "2:68: error: invalid use of storage"),
            ("cross", "4:70: error: invalid use of storage"),
            ("known", "6:41: error: lost ownership of an allocation"),
        )
        for name, text in expected:
            with self.subTest(body=name):
                row = self.body(document, name)["analyses"][5]
                self.assertEqual((row["proof"], row["violations"]), ("violated", 1))
                self.assertEqual(self.local_row(document, name)["violations"], 0)
                self.assertIn("main.ft:" + text, stderr)
        # The heap witness crosses `releaser` and `noop` and assumes that each returns, so each
        # event names its call in a note at the call (D17.18, toolchain 4.2). Each note gives
        # the fact that the path needs and what happens without it, from the row of its kind.
        self.assertIn("main.ft:4:52: note: this path needs 'releaser' to return here; if it does"
                      " not return, this use of 'v' does not happen\n", stderr)
        self.assertIn("main.ft:4:70: note: this path needs 'v' to be non-null here; a null 'v'"
                      " faults at this read first\n", stderr)
        self.assertIn("main.ft:6:37: note: this path needs 'noop' to return here; if it does not"
                      " return, 'p' does not leak\n", stderr)
        self.assertNotIn("this path needs 'reader'", stderr)
        # An alias of two formals designates one allocation, but only the summary solver sees
        # it. The summary flow decides no condition and no call on the path, so its event is
        # not a violation (toolchain 4.2). The heap summary of `sw` is incomplete, so `same`
        # also leaves the declared scope and renders no record.
        same = self.body(document, "same")["analyses"][5]
        self.assertEqual((same["correspondence"], same["proof"], same["violations"]),
                         ("incomplete", "incomplete", 0))
        self.assertNotIn("main.ft:8:", stderr)
        self.assertEqual(document["totals"]["analyses"][5]["violations"], 3)
        self.assertEqual(document["totals"]["violations"], 3)
        # The unknown call of `keep` keeps a possible return: its loss is not validated.
        keep = self.body(document, "keep")["analyses"][5]
        self.assertEqual((keep["correspondence"], keep["proof"], keep["violations"]),
                         ("incomplete", "incomplete", 0))
        self.assertNotIn("main.ft:9:", stderr)
        self.assertEqual(document["analyses"][5]["producer"], "integrated")

    def test_calls_heap_verdicts_stay_identical_in_every_mode(self):
        # The solvers read FIR before the build-mode pass, so the calls_heap rows, the ledgers
        # and the rendered records do not change with the mode (D19.8).
        self.entry.write_text(self.CALLS_CASES)
        projections = []
        for flags in ((), ("--release",), ("--no-bounds-check",),
                      ("--release", "--no-bounds-check")):
            with self.subTest(flags=flags):
                self.assertEqual(self.invoke(*flags, standard=Path(OPTIONS.std_dir)).returncode,
                                 1)
                document = self.evidence()
                ledgers = [(row["name"], row["owner"], row["fir_size"])
                           for row in document["meters"]
                           if row["name"] in ("calls_summary", "calls_heap")]
                projections.append(([row["analyses"][5] for row in document["bodies"]],
                                    ledgers, document["totals"]["violations"],
                                    self.result.stderr))
        self.assertTrue(all(value == projections[0] for value in projections))
        self.assertEqual(projections[0][2], 3)

    def test_calls_heap_totals_count_each_body_and_component(self):
        # `apply` calls a residual fort target, and `through` calls `apply`: both lack a fact
        # and leave the declared scope. The totals count each body once, and each component
        # has one ledger of each solver.
        self.entry.write_text(
            "fn apply(fn (i32*) i32 g, i32* p) i32 { return g(p); }\n"
            "fn reader(i32* p) i32 { return *p; }\n"
            "fn through(i32 mut* own q) void { apply(reader, q); del(q); }\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        through = self.body(document, "through")
        self.assertEqual(through["analyses"][5]["correspondence"], "incomplete")
        totals = document["totals"]["analyses"][5]
        self.assertEqual(totals["name"], "calls_heap")
        self.assertEqual(sum(totals["correspondence"].values()), len(document["bodies"]))
        self.assertGreaterEqual(totals["correspondence"]["incomplete"], 2)
        self.assertEqual(self.body(document, "apply")["analyses"][5]["correspondence"],
                         "incomplete")
        summaries = [row for row in document["meters"] if row["name"] == "calls_summary"]
        self.assertEqual(len(summaries), len([row for row in document["meters"]
                                              if row["name"] == "calls_heap"]))

    def test_calls_heap_renders_a_withdrawn_component_once(self):
        # `f` and `g` call each other with false, so no case covers the call: the component
        # publishes no proof, and the member that holds the uncovered call renders it. The
        # prefix of `h` reads a view after its release before the uncovered self call.
        self.entry.write_text(
            "fn f(bool b) void { if (b) { return; } g(false); }\n"
            "fn g(bool b) void { f(b); }\n"
            "fn h(i32 mut* own p, i32* q, bool b) i32 { i32 x = *q;\n"
            "    if (b) { del(p); return x; } i32* v = p; del(p); return h(new(i32), v, false); }\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        self.assertEqual(stderr.count("main.ft:1:41: error: cannot prove the call and heap "
                                      "obligations here"), 1)
        self.assertEqual(stderr.count("main.ft:4:62: error: cannot prove the call and heap "
                                      "obligations here"), 1)
        self.assertEqual(stderr.count("main.ft:2:"), 0)
        for name in ("f", "g", "h"):
            row = self.body(document, name)["analyses"][5]
            self.assertEqual((row["correspondence"], row["proof"]), ("complete", "incomplete"))

    def test_calls_heap_budget_refusals_are_errors(self):
        # Fifty scalar declarations and one read exhaust the E bound of the summary. A self
        # call after forty declarations exhausts the E bound first and then the W bound of its
        # component. No refusal records an event, so each one renders a budget error. A W
        # charge of the solver names no source, so its error stands at the declaration.
        declarations = " ".join(f"i32 x{k} = 0;" for k in range(50))
        steps = " ".join(f"i32 y{k} = y{k - 1} + 1;" for k in range(1, 40))
        self.entry.write_text(
            f"fn many(i32* p) i32 {{ {declarations} return *p; }}\n"
            f"fn walk(i32* p, bool b) i32 {{ i32 y0 = *p; {steps} if (b) {{ return y39; }}"
            " return walk(p, true); }\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        self.assertRegex(stderr, r"main\.ft:1:[0-9]+: error: ownership proof is incomplete "
                                 r"\(calls summary: work_limit; category E; used 256; bound "
                                 r"256\)")
        self.assertRegex(stderr, r"main\.ft:2:4: error: ownership proof is incomplete \(calls "
                                 r"summary: work_limit; category W; used ([0-9]+); bound \1\)")
        walk = self.body(document, "walk")["analyses"][5]
        self.assertEqual((walk["correspondence"], walk["solver"], walk["proof"]),
                         ("complete", "incomplete", "incomplete"))
        refused = [row for row in document["meters"] if row["name"] == "calls_summary"
                   and row["first_refusal"] is not None]
        self.assertEqual(sorted(row["first_refusal"]["limit"]["category"] for row in refused),
                         ["E", "E"])
        counts = {count["category"]: count for count in refused[1]["counts"]}
        self.assertEqual(counts["W"]["used"], counts["W"]["bound"])

    WITNESS_CASES = (
        "i32 mut freed = 0;\n"
        "fn drop(i32 mut* own p) void { del(p); freed += 1; }\n"
        "fn moved() void { i32 mut* own a = new(i32); drop(move(a)); }\n"
        "fn deferred() i32 { i32 mut* own a = new(i32); defer drop(move(a)); return 1; }\n"
        "fn tested(i32 mut* own p) void { if (p == null) { return; } del(p); }\n"
        "struct node { node mut* own next; i32 v; }\n"
        "fn keep_tail(node mut* own n) node mut* own { node mut* own t = move(n->next); del(n);\n"
        "    return move(t); }\n"
        "fn tail() i32 { node mut* own a = new(node); a->next = new(node);\n"
        "    node mut* own t = keep_tail(move(a)); i32 r = t->v; del(t->next); del(t); "
        "return r; }\n"
        "fn passthru(i32* p) i32* { return p; }\n"
        "fn outer() i32* { i32 x = 1; return passthru(&x); }\n"
        "fn grow(i32 mut@ own mut* s) void { i32 mut@ own n = new(i32, 8); del(*s);\n"
        "    *s = move(n); }\n"
        "fn stale() i32 { i32 mut@ own mut buf = new(i32, 4); i32@ v = buf; grow(&buf);\n"
        "    i32 r = v[0]; del(buf); return r; }\n"
        "fn main() i32 { return 0; }\n"
    )

    def test_calls_heap_violations_need_a_witness_path(self):
        # A violation needs a validated path to its operation (toolchain 4.2). The summary
        # flow keeps the obligation of an owner that a call with an incomplete summary takes,
        # and it does not decide a null test of an owner, so no summary event is a violation.
        # A heap call that releases its argument and returns the tail keeps no obligation of
        # the released root.
        self.entry.write_text(self.WITNESS_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        for name in ("moved", "deferred", "tested", "tail"):
            with self.subTest(body=name):
                row = self.body(document, name)["analyses"][5]
                self.assertEqual((row["proof"], row["violations"]), ("incomplete", 0))
        self.assertNotIn("lost ownership", stderr)
        # The summary of `tested` is complete except for its own event, so the report renders
        # that event as incomplete proof with its own text.
        tested = self.body(document, "tested")["analyses"][5]
        self.assertEqual(tested["correspondence"], "complete")
        self.assertIn("main.ft:5:51: error: cannot prove the call and heap obligations here\n",
                      stderr)
        self.assertIn("main.ft:5:51: note: parameter storage ends with a live owner", stderr)
        # A validated event without a source name names the result (toolchain 4.2).
        outer = self.body(document, "outer")["analyses"][5]
        self.assertEqual((outer["proof"], outer["violations"]), ("violated", 1))
        self.assertIn("main.ft:12:30: error: invalid use of storage\n", stderr)
        self.assertIn("main.ft:12:30: note: the result\n", stderr)
        # The two solvers validate one source read: one diagnostic and one violation.
        stale = self.body(document, "stale")["analyses"][5]
        self.assertEqual((stale["proof"], stale["violations"]), ("violated", 1))
        self.assertEqual(stderr.count("main.ft:16:14: error: invalid use of storage"), 1)
        self.assertEqual(document["totals"]["violations"], 2)

    def test_calls_heap_deferred_records_name_their_exit(self):
        # A deferred call runs at each exit, so one source read gives one record at each exit
        # operation. Each record notes the deferred statement and its own exit (toolchain 4.2).
        self.entry.write_text(
            "fn releaser(i32 mut* own p) void { del(p); }\n"
            "fn touch(i32* q) i32 { return *q; }\n"
            "fn dd(i32 mut* own p, i32 mut* own q) i32 { i32* v = p; defer touch(v);\n"
            "    releaser(move(p)); if (q == null) { return 1; } del(q); return 2; }\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        row = self.body(document, "dd")["analyses"][5]
        self.assertEqual((row["proof"], row["violations"]), ("violated", 2))
        self.assertEqual(stderr.count("main.ft:3:68: error: invalid use of storage\n"), 2)
        self.assertEqual(stderr.count("main.ft:3:68: note: the deferred statement\n"), 2)
        self.assertIn("main.ft:4:41: note: the exit that runs the deferred statement\n", stderr)
        self.assertIn("main.ft:4:61: note: the exit that runs the deferred statement\n", stderr)

    def test_calls_heap_names_each_crossed_call(self):
        # The witness crosses a call of a fort callee and assumes that the callee returns:
        # nothing proves it, so a callee that panics keeps the witness too (D17.18). The error
        # names each crossed call in a note at the call, in source order. An extern call keeps
        # the witness without a note (D17.13). A kept check on a value that the analysis does
        # not know is assumed to pass, and its note names its kind.
        self.entry.write_text(
            "extern fn ext() void;\n"
            "fn stop(i32 x) void { if (x > 0) { panic(\"stop\"); } }\n"
            "fn noop() void { }\n"
            "fn leaky(i32 mut* own p) void { noop(); stop(1); }\n"
            "fn trusted(i32 mut* own p) void { ext(); }\n"
            "fn checked(u64 i, i32 mut* own p) void { u64[2] arr = {1, 2}; u64 x = arr[i]; }\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        for name in ("leaky", "trusted"):
            with self.subTest(body=name):
                row = self.body(document, name)["analyses"][5]
                self.assertEqual((row["proof"], row["violations"]), ("violated", 1))
        lines = [line[line.index("main.ft:"):] for line in stderr.splitlines()
                 if "main.ft:" in line]
        first = lines.index("main.ft:4:50: error: lost ownership of an allocation on this path")
        self.assertEqual(lines[first + 1:first + 4], [
            "main.ft:4:50: note: p",
            "main.ft:4:37: note: this path needs 'noop' to return here; if it does not return,"
            " 'p' does not leak",
            "main.ft:4:45: note: this path needs 'stop' to return here; if it does not return,"
            " 'p' does not leak",
        ])
        self.assertIn("main.ft:5:42: error: lost ownership of an allocation on this path\n",
                      stderr)
        self.assertNotIn("this path needs 'ext'", stderr)
        first = lines.index("main.ft:6:79: error: lost ownership of an allocation on this path")
        self.assertEqual(lines[first + 1:first + 3], [
            "main.ft:6:79: note: p",
            "main.ft:6:74: note: this path needs the bounds check to pass here; if it fails, the"
            " program stops",
        ])

    def test_check_failure_renders_no_empty_incomplete_reason(self):
        # A program that fails checking has its own errors. When no analysis gives a reason of
        # incomplete proof, the report adds no incomplete-proof error; before, it wrote
        # "ownership proof is incomplete (: )" at 1:1.
        self.entry.write_text(
            "extern fn ext() void;\n"
            "fn f(i32 mut* own p) void { (fn () void) g = ext; del(p); }\n"
            "fn main() i32 { return 0; }\n"
        )
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        stderr = self.result.stderr.decode()
        self.assertIn("main.ft:2:46: error: 'ext' is an extern function, which is not a value",
                      stderr)
        self.assertNotIn("ownership proof is incomplete", stderr)
        self.assertNotIn(": )", stderr)

    def qualification_cases(self):
        """The 109 qualification cases: (path, expected verdict, error lines). An approved
        fixture names its error positions; a linear or zero-element case marks them with
        `//! error:`."""
        qualification = CHECKOUT / "test" / "ownership" / "qualification" / "manifest.json"
        approved = CHECKOUT / "test" / "ownership" / "approved"
        fixtures = {}
        for name in ("manifest-01-10.json", "manifest-11-20.json"):
            for fixture in json.loads((approved / name).read_text())["fixtures"]:
                fixtures[fixture["file"][:-3]] = fixture
        out = []
        for case in json.loads(qualification.read_text())["cases"]:
            path = CHECKOUT / case["path"]
            lines = path.read_text().splitlines()
            fixture = fixtures.get(case["id"])
            verdict = None if fixture is None else fixture["expected_verdict"]
            if fixture is not None:
                errors = [p["line"] for p in fixture["expected_positions"] if p["kind"] == "error"]
            else:
                errors = [n + 1 for n, line in enumerate(lines) if "//! error:" in line]
            if verdict is None and case["group"] == "counterpart":
                verdict = "accept"
            if verdict is None and lines[0].strip() == "//! run":
                verdict = "accept"
            if verdict is None and lines[0].strip() == "//! fail":
                verdict = "reject"
            out.append((path, verdict, errors))
        return out

    def qualification_report(self, path):
        directory = self.root / path.stem
        directory.mkdir()
        entry = directory / path.name
        entry.write_bytes(path.read_bytes())
        report = directory / "report.json"
        result = subprocess.run(
            [OPTIONS.fort, "--std-dir", OPTIONS.std_dir, "--check", "--ownership-check",
             "--ownership-report", str(report), str(entry)],
            cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            timeout=120)
        self.assertIn(result.returncode, (0, 1), path.name)
        return audit.read_json(report)[0]

    def test_qualification_accept_cases_have_no_violation(self):
        # Each case that the qualification expects to accept gives no violation in any
        # analysis. A case can still give incomplete proof (toolchain 4.2).
        cases = self.qualification_cases()
        accept = [path for path, verdict, _ in cases if verdict == "accept"]
        self.assertEqual((len(cases), len(accept)), (109, 72))

        def violations(path):
            return path.stem, self.qualification_report(path)["totals"]["violations"]

        with ThreadPoolExecutor(max_workers=4) as pool:
            found = dict(pool.map(violations, accept))
        self.assertEqual({name: count for name, count in found.items() if count != 0}, {})

    def test_qualification_reject_cases_never_prove_the_faulty_body(self):
        # Each case that the qualification expects to reject holds a fault in the body of each
        # error line. Each case exits 1 for an unrelated gap of the closure, so the exit status
        # shows nothing: the calls_heap row of each faulty body must not be a complete proof
        # (D17.14, D17.18). It can be a violation or incomplete proof.
        cases = self.qualification_cases()
        reject = [(path, errors) for path, verdict, errors in cases if verdict == "reject"]
        self.assertEqual(len(reject), 37)
        self.assertTrue(all(errors for _, errors in reject))

        def rows(item):
            path, errors = item
            report = self.qualification_report(path)
            bodies = sorted((body["source"]["line"], body["source"]["name"],
                             body["analyses"][5]["proof"])
                            for body in report["bodies"]
                            if body["source"]["module"] == path.stem)
            faulty = []
            for line in errors:
                owners = [body for body in bodies if body[0] <= line]
                self.assertTrue(owners, f"{path.name}:{line}")
                faulty.append(owners[-1])
            return path.stem, faulty

        with ThreadPoolExecutor(max_workers=4) as pool:
            found = dict(pool.map(rows, reject))
        proved = {name: [body for body in faulty if body[2] == "complete"]
                  for name, faulty in found.items()}
        self.assertEqual({name: bodies for name, bodies in proved.items() if bodies}, {})
        verdicts = sorted(body[2] for faulty in found.values() for body in faulty)
        self.assertEqual(set(verdicts), {"incomplete", "violated"})

    def test_heap_solver_alone_proves_no_faulty_reject_body(self):
        # The calls_heap row joins the summary solver and the heap solver, so the row of a
        # faulty body can be incomplete because of the summary solver alone. This test runs the
        # heap solver alone (`test/ownership/heap_alone.ft`) over each reject case: it proves no
        # body that holds a fault (D17.18: no assumed fact removes a path). Before, it proved the
        # faulty `main` of linear cases 062 and 063, which pass an empty owner to a callee that
        # reads through it (D10.7).
        harness = self.root / "heap_alone"
        built = subprocess.run(
            [OPTIONS.fort, "--cc", OPTIONS.cc, "--std-dir", OPTIONS.std_dir,
             "-I", str(CHECKOUT / "src" / "fort"),
             "-I", str(CHECKOUT / "test" / "fort" / "support"),
             "-o", str(harness), str(CHECKOUT / "test" / "ownership" / "heap_alone.ft")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False, timeout=600)
        self.assertEqual(built.returncode, 0, built.stderr.decode())
        cases = self.qualification_cases()
        reject = [(path, errors) for path, verdict, errors in cases if verdict == "reject"]
        self.assertEqual(len(reject), 37)

        def verdicts(item):
            path, errors = item
            lines = path.read_text().splitlines()
            declared = [(n + 1, match.group(1)) for n, line in enumerate(lines)
                        for match in [re.match(r"fn (\w+)", line)] if match]
            faulty = sorted({[d for d in declared if d[0] <= line][-1][1] for line in errors})
            # The test environment writes its sandboxes into the working directory and reads
            # `std` there, so each case runs in a directory of its own.
            where = self.root / ("alone-" + path.stem)
            where.mkdir()
            (where / "std").symlink_to(CHECKOUT / "std")
            run = subprocess.run([str(harness), str(path)], cwd=where, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, check=False, timeout=120)
            self.assertEqual(run.returncode, 0, f"{path.name}: {run.stderr.decode()}")
            found = dict(line.split()[:2] for line in run.stdout.decode().splitlines())
            return path.stem, {name: found.get(name, "missing") for name in faulty}

        with ThreadPoolExecutor(max_workers=4) as pool:
            found = dict(pool.map(verdicts, reject))
        self.assertEqual(sum(len(faulty) for faulty in found.values()), 37)
        proved = {name: faulty for name, faulty in found.items() if "proved" in faulty.values()}
        self.assertEqual(proved, {})
        self.assertEqual({verdict for faulty in found.values() for verdict in faulty.values()},
                         {"failed", "unproved"})

    RAW_CASES = (
        "fn reconstruct() i32 {\n"
        "    i32 x = 5;\n"
        "    u64 bits = cast(&x, u64);\n"
        "    i32* q = cast(bits, i32*);\n"
        "    return *q;\n"
        "}\n"
        "fn end_pointer() u8 {\n"
        "    u8 mut@ own bytes = new(u8, 4);\n"
        "    defer del(bytes);\n"
        "    u8* tail = bytes[4..4].ptr;\n"
        "    return *tail;\n"
        "}\n"
        "fn symbolic(u64 n) u8 {\n"
        "    u8 mut@ own s = new(u8, n);\n"
        "    defer del(s);\n"
        "    u8* p = s.ptr;\n"
        "    return *p;\n"
        "}\n"
        "fn through_parameter(i32* p) i32 { return *p; }\n"
        "fn safe() i32 {\n"
        "    i32 x = 3;\n"
        "    i32* p = &x;\n"
        "    return *p;\n"
        "}\n"
        "fn main() i32 { return 0; }\n"
    )

    def raw_row(self, document, name):
        return self.body(document, name)["analyses"][4]

    def test_raw_violations_and_failures_are_rendered_and_counted(self):
        self.entry.write_text(self.RAW_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        self.assertIn("main.ft:5:12: error: invalid use of 'q'", stderr)
        self.assertIn("main.ft:5:12: note: raw access needs a proved original source", stderr)
        self.assertIn("main.ft:11:12: error: invalid use of 'tail'", stderr)
        self.assertIn("main.ft:11:12: note: typed access exceeds its inherited window", stderr)
        self.assertIn("main.ft:17:12: error: cannot prove that 'p' designates the accessed "
                      "storage", stderr)
        # A failure without a witness names its reason as possible (toolchain 4.2).
        self.assertIn("main.ft:17:12: note: typed access window remains unproved", stderr)
        # Raw records come before the one closure reason, in body order.
        self.assertLess(stderr.index("invalid use of 'q'"), stderr.index("invalid use of 'tail'"))
        self.assertLess(stderr.index("cannot prove that"),
                        stderr.index("ownership proof is incomplete"))
        self.assertEqual(document["totals"]["analyses"][4]["violations"], 2)
        self.assertEqual(document["totals"]["violations"], 2)
        for name in ("reconstruct", "end_pointer"):
            with self.subTest(name=name):
                self.assertEqual(self.raw_row(document, name),
                                 {"name": "raw", "correspondence": "complete",
                                  "solver": "complete", "proof": "violated", "violations": 1})
        self.assertEqual(self.raw_row(document, "symbolic")["proof"], "incomplete")
        self.assertEqual(self.raw_row(document, "symbolic")["correspondence"], "complete")
        parameter = self.raw_row(document, "through_parameter")
        self.assertEqual((parameter["correspondence"], parameter["proof"]),
                         ("incomplete", "incomplete"))
        self.assertEqual(self.raw_row(document, "safe")["proof"], "complete")
        self.assertEqual(document["analyses"][4]["producer"], "integrated")
        self.assertEqual(document["analyses"][4]["status"], "incomplete")

    # Process exit with the real runtime (D17.19): a direct extern exit with a live owner, the
    # runtime cleanup before extern exit, and the std.sys.exit wrapper.
    EXIT_CASES = (
        "import std.libc;\n"
        "import std.rt;\n"
        "import std.sys;\n"
        "fn leak_at_exit() void {\n"
        "    u8 mut@ own p = new(u8, 1);\n"
        "    libc.exit(0);\n"
        "}\n"
        "fn clean_exit() void {\n"
        "    rt.shutdown();\n"
        "    libc.exit(0);\n"
        "}\n"
        "fn wrapped_exit(i32 status) void {\n"
        "    sys.exit(status);\n"
        "}\n"
        "fn main() i32 {\n"
        "    return 0;\n"
        "}\n"
    )

    def exit_row(self, document, name):
        return self.body(document, name)["analyses"][6]

    def test_process_exit_with_the_real_runtime(self):
        self.entry.write_text(self.EXIT_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        self.assertIn(":6:14: error: lost ownership of 'p'", stderr)
        self.assertIn("normal process exit ends the process while this owner holds an allocation",
                      stderr)
        self.assertIn(":6:14: error: cannot prove that 'args_store' is empty", stderr)
        self.assertEqual(self.exit_row(document, "leak_at_exit"),
                         {"name": "process_exit", "correspondence": "complete",
                          "solver": "complete", "proof": "violated", "violations": 1})
        for name in ("clean_exit", "wrapped_exit", "main"):
            with self.subTest(name=name):
                self.assertEqual(self.exit_row(document, name)["proof"], "complete")
        # The runtime cleanup std.rt.shutdown empties args_store before std.rt.exit ends the
        # process, and before the generated return of main.
        runtime = [body for body in document["bodies"] if body["source"]["module"] == "std.rt"]
        for name in ("shutdown", "exit", "args_init", "args"):
            with self.subTest(name=name):
                row = next(body for body in runtime if body["source"]["name"] == name)
                self.assertEqual(row["analyses"][6]["proof"], "complete")
        self.assertEqual(document["boundary"], {"kind": "executable", "leaves": 1,
                                                "correspondence": "complete",
                                                "proof": "complete",
                                                "first_incomplete": None})
        self.assertEqual(document["analyses"][6]["producer"], "integrated")
        self.assertEqual(document["totals"]["analyses"][6]["violations"], 1)
        self.assertEqual(document["totals"]["violations"], 1)
        # The violated body rendered two records, each an error and a note.
        services = {json.dumps(row["owner"], sort_keys=True): row
                    for row in document["meters"] if row["name"] == "services"}
        ledgers = [row for row in document["meters"] if row["name"] == "process_exit"]
        self.assertEqual(len(ledgers), len(services))
        for row in ledgers:
            self.assertEqual(row["fir_size"],
                             services[json.dumps(row["owner"], sort_keys=True)]["fir_size"])
        key = self.body(document, "leak_at_exit")["key"]
        ledger = next(row for row in ledgers if row["owner"] == key)
        used = {count["category"]: count["used"] for count in ledger["counts"]}
        self.assertEqual(used["V"], 4)
        self.assertGreater(used["W"], 0)
        # The call-graph step of the boundary has one closure ledger: no owner, W only, and the
        # summed FIR size of the process_exit ledgers.
        fills = [row for row in document["meters"] if row["name"] == "process_exit_fills"]
        self.assertEqual(len(fills), 1)
        self.assertIsNone(fills[0]["owner"])
        self.assertEqual(fills[0]["fir_size"], sum(row["fir_size"] for row in ledgers))
        self.assertEqual([count["category"] for count in fills[0]["counts"]], ["W"])
        self.assertGreater(fills[0]["counts"][0]["used"], 0)
        self.assertIsNone(fills[0]["first_refusal"])

    def test_process_exit_verdicts_stay_identical_in_every_mode(self):
        self.entry.write_text(self.EXIT_CASES)
        projections = []
        for check in (True, False):
            for flags in ((), ("--release",), ("--no-bounds-check",),
                          ("--release", "--no-bounds-check")):
                with self.subTest(check=check, flags=flags):
                    result = self.invoke(*flags, check=check, standard=Path(OPTIONS.std_dir))
                    self.assertEqual(result.returncode, 1)
                    document = self.evidence()
                    projections.append(([row["analyses"][6] for row in document["bodies"]],
                                        document["boundary"], self.result.stderr))
        self.assertTrue(all(value == projections[0] for value in projections))

    def test_library_boundary_finds_the_runtime_cleanup(self):
        self.entry.write_text("fn helper() u64 { return 1; }\n")
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        self.assertEqual(document["boundary"]["kind"], "library")
        self.assertEqual(document["boundary"]["proof"], "complete")
        self.assertNotIn("is empty", self.result.stderr.decode())

    def test_process_exit_diagnostics_keep_json_version_one(self):
        self.entry.write_text(self.EXIT_CASES)
        self.assertEqual(self.invoke("--json", standard=Path(OPTIONS.std_dir)).returncode, 1)
        self.evidence()
        diagnostic = json.loads(self.result.stdout)
        self.assertEqual(diagnostic["version"], 1)
        messages = [(row["line"], row["message"]) for row in diagnostic["diagnostics"]]
        self.assertIn((6, "lost ownership of 'p'"), messages)
        self.assertIn((6, "cannot prove that 'args_store' is empty"), messages)

    def test_raw_ledgers_pair_with_service_ledgers(self):
        self.entry.write_text(self.RAW_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        services = {json.dumps(row["owner"], sort_keys=True): row
                    for row in document["meters"] if row["name"] == "services"}
        raw = [row for row in document["meters"] if row["name"] == "raw"]
        self.assertEqual(len(raw), len(services))
        for row in raw:
            paired = services[json.dumps(row["owner"], sort_keys=True)]
            self.assertEqual(row["fir_size"], paired["fir_size"])
            self.assertIsNone(row["first_refusal"])
        # The violated body retained one kernel event and rendered an error and a note.
        key = self.body(document, "reconstruct")["key"]
        ledger = next(row for row in raw if row["owner"] == key)
        used = {count["category"]: count["used"] for count in ledger["counts"]}
        self.assertEqual(used["V"], 3)
        self.assertGreater(used["W"], 0)

    def test_raw_diagnostics_keep_json_version_one(self):
        self.entry.write_text(self.RAW_CASES)
        self.assertEqual(self.invoke("--json", standard=Path(OPTIONS.std_dir)).returncode, 1)
        self.evidence()
        diagnostic = json.loads(self.result.stdout)
        self.assertEqual(diagnostic["version"], 1)
        messages = [(row["line"], row["message"]) for row in diagnostic["diagnostics"]]
        self.assertIn((5, "invalid use of 'q'"), messages)
        self.assertIn((11, "invalid use of 'tail'"), messages)
        self.assertIn((17, "cannot prove that 'p' designates the accessed storage"), messages)

    def test_raw_verdicts_stay_identical_in_every_mode(self):
        self.entry.write_text(self.RAW_CASES)
        projections = []
        for flags in ((), ("--release",), ("--no-bounds-check",),
                      ("--release", "--no-bounds-check")):
            with self.subTest(flags=flags):
                self.assertEqual(self.invoke(*flags, standard=Path(OPTIONS.std_dir)).returncode,
                                 1)
                document = self.evidence()
                projections.append(([row["analyses"][4] for row in document["bodies"]],
                                    document["totals"]["violations"],
                                    self.result.stderr))
        self.assertTrue(all(value == projections[0] for value in projections))
        self.assertEqual(projections[0][1], 2)

    STORED_CASES = (
        "struct box { i32 mut* p; i32 n; }\n"
        "struct pair { i32 mut* own a; i32 mut* own b; }\n"
        "fn field_use() void {\n"
        "    i32 mut* own p = new(i32);\n"
        "    box b = box{p, 1};\n"
        "    del(p);\n"
        "    *b.p = 1;\n"
        "}\n"
        "fn owner_leak() void { pair s = pair{new(i32), new(i32)}; del(s.a); }\n"
        "fn stack_return() i32* { i32 x = 1; return &x; }\n"
        "fn local_leak() void { i32 mut* own p = new(i32); }\n"
        "fn main() i32 { return 0; }\n"
    )

    def stored_row(self, document, name):
        return self.body(document, name)["analyses"][3]

    def test_stored_violations_are_rendered_and_counted(self):
        self.entry.write_text(self.STORED_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        stderr = self.result.stderr.decode()
        self.assertIn("main.ft:7:10: error: invalid use of 'b.p'", stderr)
        self.assertIn("main.ft:7:10: note: reference source is no longer live", stderr)
        self.assertIn("main.ft:9:69: error: lost ownership of 's.b'", stderr)
        self.assertIn("main.ft:9:69: note: storage ends with a live owner", stderr)
        self.assertIn("main.ft:10:37: error: invalid use of the result", stderr)
        self.assertIn("main.ft:10:37: note: returned view outlives its source", stderr)
        # The local run validates the leak of p, so it is one diagnostic in the local row.
        self.assertEqual(stderr.count("lost ownership of 'p'"), 1)
        self.assertEqual(document["totals"]["violations"], 4)
        self.assertEqual(document["totals"]["analyses"][2]["violations"], 1)
        self.assertEqual(document["totals"]["analyses"][3]["violations"], 3)
        self.assertEqual(document["totals"]["analyses"][3]["proof"]["violated"], 3)
        for name in ("field_use", "owner_leak", "stack_return"):
            with self.subTest(name=name):
                self.assertEqual(self.stored_row(document, name),
                                 {"name": "stored_borrows", "correspondence": "complete",
                                  "solver": "complete", "proof": "violated",
                                  "violations": 1})
                self.assertEqual(self.body(document, name)["violations"], 1)
        leak = self.body(document, "local_leak")
        self.assertEqual(leak["violations"], 1)
        self.assertEqual(self.local_row(document, "local_leak")["violations"], 1)
        self.assertEqual(self.stored_row(document, "local_leak")["violations"], 0)
        self.assertEqual(self.stored_row(document, "local_leak")["proof"], "incomplete")
        self.assertEqual(document["analyses"][3]["producer"], "integrated")
        # Violations render in body order: field_use, owner_leak, stack_return, local_leak.
        order = [stderr.index(text) for text in ("'b.p'", "'s.b'", "the result", "'p'")]
        self.assertEqual(order, sorted(order))

    # The stored diagnostics of the approved examples that the stored row decides. A position
    # is the FIR location of the operation; the approved design marks a later column of some
    # of them. Examples 9 and 18 stay outside the stored scope (unknown_source).
    APPROVED_STORED = {
        "ex05_local_address.ft": ("bad", "3:5: error: invalid use of the result"),
        "ex06_local_span.ft": ("bad", "3:5: error: invalid use of the result"),
        "ex12_partial_move.ft": ("demo", "11:18: error: invalid use of 'value.part.ptr'"),
        "ex14_aggregate_overwrite.ft": ("demo", "6:13: error: lost ownership of 'value.ptr'"),
        "ex09_retained_field.ft": ("demo", None),
        "ex18_retained_key.ft": ("demo", None),
    }
    # The stored analysis cannot follow a view that a call stores. The calls_heap analysis
    # applies the heap summary of the call and validates the use after the release.
    APPROVED_CALLS = {
        "ex09_retained_field.ft": "12:12: error: invalid use of storage",
        "ex18_retained_key.ft": "12:12: error: invalid use of storage",
    }

    def test_stored_diagnostics_of_the_approved_examples(self):
        approved = CHECKOUT / "test" / "ownership" / "approved"
        for name, (body, expected) in self.APPROVED_STORED.items():
            with self.subTest(example=name):
                self.entry.write_bytes((approved / name).read_bytes())
                self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
                document = self.evidence()
                stderr = self.result.stderr.decode()
                row = self.stored_row(document, body)
                if expected is None:
                    self.assertEqual((row["correspondence"], row["proof"]),
                                     ("incomplete", "incomplete"))
                    calls = self.body(document, body)["analyses"][5]
                    self.assertEqual((calls["proof"], calls["violations"]), ("violated", 1))
                    self.assertIn("main.ft:" + self.APPROVED_CALLS[name], stderr)
                    self.assertEqual(stderr.count(": error: invalid use"), 1)
                    self.assertEqual(document["totals"]["violations"], 1)
                    continue
                self.assertIn("main.ft:" + expected, stderr)
                self.assertEqual(stderr.count(": error: invalid use") +
                                 stderr.count(": error: lost ownership"), 1)
                self.assertEqual((row["proof"], row["violations"]), ("violated", 1))
                self.assertEqual(self.local_row(document, body)["violations"], 0)
                self.assertEqual(document["totals"]["analyses"][3]["violations"], 1)

    def test_stored_ledgers_pair_with_service_ledgers(self):
        self.entry.write_text(self.STORED_CASES)
        self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir)).returncode, 1)
        document = self.evidence()
        services = {json.dumps(row["owner"], sort_keys=True): row
                    for row in document["meters"] if row["name"] == "services"}
        stored = [row for row in document["meters"] if row["name"] == "stored_borrows"]
        self.assertEqual(len(stored), len(services))
        for row in stored:
            paired = services[json.dumps(row["owner"], sort_keys=True)]
            self.assertEqual(row["fir_size"], paired["fir_size"])
            limits = document["limits"]
            counts = {count["category"]: count for count in row["counts"]}
            self.assertEqual(counts["W"]["bound"], limits["w"] + limits["w_scale"] *
                             row["fir_size"])
            self.assertIsNone(row["first_refusal"])
        # Each body's ledgers stand in the order services, local, stored, raw.
        names = [row["name"] for row in document["meters"] if row["owner"] is not None]
        self.assertEqual(names[:4], ["services", "local", "stored_borrows", "raw"])

    def test_stored_verdicts_stay_identical_in_every_mode(self):
        self.entry.write_text(self.STORED_CASES)
        projections = []
        for flags in ((), ("--release",), ("--no-bounds-check",),
                      ("--release", "--no-bounds-check")):
            with self.subTest(flags=flags):
                self.assertEqual(self.invoke(*flags, standard=Path(OPTIONS.std_dir)).returncode,
                                 1)
                document = self.evidence()
                projections.append(([row["analyses"][3] for row in document["bodies"]],
                                    document["totals"]["violations"],
                                    self.result.stderr))
        self.assertTrue(all(value == projections[0] for value in projections))
        self.assertEqual(projections[0][1], 4)

    # Three compiler bodies: the root, the module and the declaration. At W_scale 64 the stored
    # ledger of ownership_events.clone refused W: it needs 107761 W and 64 gave it 97600.
    LEDGER_BODIES = (
        ("ownership_events.ft", "ownership_events", "clone"),
        ("ownership_events.ft", "fir", "func_clone"),
        ("ownership_state.ft", "ownership_state", "measure_regions"),
    )

    def test_compiler_bodies_keep_their_stored_ledgers(self):
        source = CHECKOUT / "src" / "fort"
        for root in sorted({row[0] for row in self.LEDGER_BODIES}):
            with self.subTest(root=root):
                self.assertEqual(self.invoke(standard=Path(OPTIONS.std_dir), entry=source / root,
                                             sources=(source,)).returncode, 1)
                document = self.evidence()
                limits = document["limits"]
                for _, module, declaration in (r for r in self.LEDGER_BODIES if r[0] == root):
                    rows = [row for row in document["bodies"]
                            if row["source"]["module"] == module
                            and row["source"]["name"] == declaration]
                    self.assertEqual(len(rows), 1, declaration)
                    ledgers = [meter for meter in document["meters"]
                               if meter["name"] == "stored_borrows"
                               and meter["owner"] == rows[0]["key"]]
                    self.assertEqual(len(ledgers), 1, declaration)
                    self.assertIsNone(ledgers[0]["first_refusal"], declaration)
                    counts = {count["category"]: count for count in ledgers[0]["counts"]}
                    self.assertEqual(counts["W"]["bound"], limits["w"] + limits["w_scale"] *
                                     ledgers[0]["fir_size"])
                    self.assertGreater(counts["W"]["used"], 0, declaration)
                    self.assertLess(counts["W"]["used"], counts["W"]["bound"], declaration)
                    self.assertLess(counts["V"]["used"], counts["V"]["bound"], declaration)
                    stored = rows[0]["analyses"][3]
                    self.assertEqual(stored["name"], "stored_borrows")
                    self.assertEqual((stored["solver"], stored["violations"]), ("complete", 0))

    # Loads out of the referent of a pointer parameter (spec/fir.md 14.1). A clean load borrows
    # the caller source of the loaded reference. The body requires that source live at entry,
    # and no caller checks that yet, so the compiler keeps each such load the gap: `row_at` and
    # `name` stay outside both scopes, as do a load after a store through the parameter, a
    # second loaded level, a use through the loaded reference and a load before a call.
    LOADED_CASES = (
        "struct item { u32 n; }\n"
        "struct items { item mut@ own data; u64 len; }\n"
        "struct table { items rows; u8@ name; }\n"
        "struct cell { i64 a; }\n"
        "struct holder { cell* p; }\n"
        "struct chain { holder* h; }\n"
        "fn helper() void {}\n"
        "fn row_at(table* t, u64 i) item* {\n"
        "    if (i >= t->rows.len) { panic(\"index out of range\"); }\n"
        "    return &t->rows.data[i];\n"
        "}\n"
        "fn name(table* t) u8@ { return t->name[..]; }\n"
        "fn stored(holder mut* h, cell* c) i64* { h->p = c; return &h->p->a; }\n"
        "fn deeper(chain* c) i64* { return &c->h->p->a; }\n"
        "fn used(holder* h) i64 { return h->p->a; }\n"
        "fn called(holder* h) i64 { i64* q = &h->p->a; helper(); return *q; }\n"
        "fn main() i32 { return 0; }\n"
    )

    def test_loaded_parameter_sources_stay_gated_in_every_mode(self):
        self.entry.write_text(self.LOADED_CASES)
        names = ("row_at", "name", "stored", "deeper", "used", "called")
        projections = []
        for flags in ((), ("--release",), ("--no-bounds-check",),
                      ("--release", "--no-bounds-check")):
            with self.subTest(flags=flags):
                self.assertEqual(self.invoke(*flags, standard=Path(OPTIONS.std_dir)).returncode,
                                 1)
                document = self.evidence()
                self.assertEqual(document["totals"]["violations"], 0)
                rows = {}
                for name in names:
                    local = self.local_row(document, name)
                    stored = self.stored_row(document, name)
                    rows[name] = ((local["correspondence"], local["proof"]),
                                  (stored["correspondence"], stored["proof"]))
                projections.append(rows)
        # The release and bounds-check options remove checks from the FIR, and the verdicts stay.
        self.assertTrue(all(value == projections[0] for value in projections))
        outside = (("incomplete", "incomplete"), ("incomplete", "incomplete"))
        self.assertEqual(projections[0], {name: outside for name in names})

    def test_report_can_replace_an_old_report_atomically(self):
        self.report.write_bytes(b"old report\n")
        self.assertEqual(self.invoke().returncode, 1)
        self.evidence()
        self.assertTrue(self.report.read_bytes().endswith(b"\n"))

    def test_report_write_failure_preserves_old_destination(self):
        self.report.mkdir()
        retained = self.report / "retained"
        retained.write_text("old bytes")
        self.assertEqual(self.invoke("--json").returncode, 2)
        self.assertEqual(self.result.stdout, b"")
        self.assertEqual(retained.read_text(), "old bytes")
        self.assertFalse(list(self.root.rglob("*.tmp.*")))

    def test_actual_report_write_failure_preserves_report_and_output(self):
        self.assertEqual(self.invoke().returncode, 1)
        self.evidence()
        previous = self.report.read_bytes()
        self.assertGreater(len(previous), 512)
        output = self.write("output.ll", b"previous LLVM output\n")

        def limit_report_write():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            _, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
            resource.setrlimit(resource.RLIMIT_FSIZE, (512, hard))

        for check in (True, False):
            with self.subTest(check=check):
                flags = ("--json",) if check else ()
                result = self.invoke(*flags, "-S", "-o", str(output), check=check,
                                     process_setup=limit_report_write)
                self.assertEqual(result.returncode, 2)
                self.assertIn(b"cannot write ownership report", result.stderr)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(self.report.read_bytes(), previous)
                self.assertEqual(output.read_bytes(), b"previous LLVM output\n")
                self.assertFalse(list(self.root.rglob("*.tmp.*")))

    def test_report_source_alias_is_rejected(self):
        aliases = [self.entry, self.root / "alias.ft"]
        aliases[1].symlink_to(self.entry)
        for alias in aliases:
            with self.subTest(alias=str(alias)):
                self.report = alias
                self.assertEqual(self.invoke("--json").returncode, 2)
                self.assertEqual(self.result.stdout, b"")
                self.assertEqual(self.entry.read_text(), "fn main() i32 { return 0; }\n")

    def test_report_import_alias_is_rejected(self):
        imported = self.write("other.ft", "fn uncalled() void {}\n")
        self.entry.write_text("import other;\nfn main() i32 { return 0; }\n")
        self.report = imported
        self.assertEqual(self.invoke().returncode, 2)
        self.assertEqual(imported.read_text(), "fn uncalled() void {}\n")

    def test_report_import_probe_alias_is_rejected(self):
        probed = self.write("util.ft", "fn unrelated() void {}\n")
        self.write("util/helper.ft", "fn help() void {}\n")
        self.entry.write_text("import util.helper;\nfn main() i32 { return 0; }\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertNotIn("util", [row["module"] for row in document["files"]])
        self.assertIn("util.helper", [row["module"] for row in document["files"]])
        alias = self.root / "probe-alias.ft"
        alias.symlink_to(probed)
        for selected in (probed, alias):
            with self.subTest(path=selected.name):
                self.report = selected
                self.assertEqual(self.invoke("--json").returncode, 2)
                self.assertEqual(self.result.stdout, b"")
                self.assertEqual(probed.read_bytes(), b"fn unrelated() void {}\n")
                self.assertFalse(list(self.root.rglob("*.tmp.*")))

    def test_failed_import_probe_preserves_its_source(self):
        probed = self.write("util.ft", "fn malformed( void {}\n")
        self.write("util/helper.ft", "fn help() void {}\n")
        self.entry.write_text("import util.helper;\nfn main() i32 { return 0; }\n")
        self.report = probed
        self.assertEqual(self.invoke("--json").returncode, 2)
        self.assertEqual(self.result.stdout, b"")
        self.assertEqual(probed.read_bytes(), b"fn malformed( void {}\n")

    def test_report_runtime_alias_is_rejected(self):
        self.report = self.standard / "rt.ft"
        self.assertEqual(self.invoke().returncode, 2)
        self.assertEqual(self.report.read_text(), "// Empty fixture runtime.\n")

    def test_report_output_alias_is_rejected_before_output(self):
        output = self.write("output.ll", "retained output\n")
        alias = self.root / "alias.ll"
        alias.symlink_to(output)
        self.report = alias
        self.assertEqual(self.invoke("-S", "-o", str(output), check=False).returncode, 2)
        self.assertEqual(output.read_text(), "retained output\n")

    def unresolved_output_links(self, output, links):
        original = {str(path): os.readlink(path) for path in links}
        result = self.invoke("-S", "-o", str(output), check=False)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn(b"ownership report path names an input, an output, "
                      b"or an invalid destination", result.stderr)
        self.assertEqual(result.stdout, b"")
        self.assertFalse(self.report.exists())
        for path in links:
            self.assertTrue(path.is_symlink())
            self.assertEqual(os.readlink(path), original[str(path)])
        self.assertFalse(list(self.root.rglob("*.tmp.*")))

    def test_dangling_output_link_to_report_in_same_directory_is_rejected(self):
        output = self.root / "output.ll"
        output.symlink_to(self.report.name)
        self.unresolved_output_links(output, [output])
        self.assertFalse(output.exists())

    def test_dangling_output_link_to_report_in_other_directory_is_rejected(self):
        directory = self.root / "reports"
        directory.mkdir()
        self.report = directory / "report.json"
        output = self.root / "output.ll"
        output.symlink_to("reports/report.json")
        self.unresolved_output_links(output, [output])
        self.assertFalse(output.exists())
        self.assertEqual(list(directory.iterdir()), [])

    def test_dangling_output_link_chain_is_rejected(self):
        directory = self.root / "reports"
        directory.mkdir()
        self.report = directory / "report.json"
        middle = self.root / "middle.ll"
        middle.symlink_to("reports/report.json")
        output = self.root / "output.ll"
        output.symlink_to(middle.name)
        self.unresolved_output_links(output, [output, middle])
        self.assertFalse(output.exists())
        self.assertFalse(middle.exists())

    def test_looped_output_links_are_rejected(self):
        output = self.root / "output.ll"
        middle = self.root / "middle.ll"
        output.symlink_to(middle.name)
        middle.symlink_to(output.name)
        self.unresolved_output_links(output, [output, middle])

    def test_dangling_report_link_is_rejected_conservatively(self):
        output = self.root / "output.ll"
        self.report.symlink_to(output.name)
        self.unresolved_output_links(output, [self.report])
        self.assertFalse(output.exists())

    def test_resolved_output_link_keeps_distinct_report_and_output(self):
        target = self.write("retained.ll", b"previous LLVM output\n")
        output = self.root / "output.ll"
        output.symlink_to(target.name)
        self.assertEqual(self.invoke("-S", "-o", str(output), check=False).returncode, 1)
        self.evidence()
        self.assertTrue(output.is_symlink())
        self.assertEqual(os.readlink(output), target.name)
        self.assertEqual(target.read_bytes(), b"previous LLVM output\n")

    def test_absent_output_alias_through_parent_is_rejected(self):
        directory = self.root / "out"
        directory.mkdir()
        alias = self.root / "alias"
        alias.symlink_to(directory, target_is_directory=True)
        self.report = alias / "new.ll"
        result = self.invoke("-S", "-o", str(directory / "new.ll"), check=False)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(list(directory.iterdir()), [])

    def absent_name_comparison(self, output_name, report_name):
        output = self.write(output_name, b"filesystem name probe\n")
        selected = self.root / report_name
        aliases = selected.exists()
        if aliases:
            self.assertTrue(selected.samefile(output))
        output.unlink()
        self.report = selected
        result = self.invoke("-S", "-o", str(output), check=False)
        self.assertEqual(result.returncode, 2 if aliases else 1, result.stderr)
        self.assertFalse(output.exists())
        self.assertFalse(list(self.root.rglob("*.tmp.*")))
        if aliases:
            self.assertFalse(selected.exists())
        else:
            self.evidence()
            selected.unlink()

    def test_absent_output_comparison_uses_native_case_rules(self):
        self.absent_name_comparison("case-output.ll", "CASE-OUTPUT.ll")

    def test_absent_output_comparison_uses_native_unicode_normalization(self):
        self.absent_name_comparison("\u00e9-output.ll", "e\u0301-output.ll")

    def test_absent_output_comparison_uses_native_unicode_case_rules(self):
        self.absent_name_comparison("\u00e9-output.ll", "\u00c9-output.ll")

    def test_absent_output_comparison_keeps_distinct_names(self):
        self.absent_name_comparison("output.part.ll", "report.part.json")

    def test_absent_trailing_dots_and_spaces_refuse_uncertain_comparison(self):
        for output_name, report_name in (("out.ll.", "report.json"),
                                         ("out.ll ", "report.json"),
                                         ("out.ll", "report.json."),
                                         ("out.ll", "report.json ")):
            with self.subTest(output=output_name, report=report_name):
                output = self.root / output_name
                self.report = self.root / report_name
                self.assertEqual(self.invoke("-S", "-o", str(output), check=False).returncode, 2)
                self.assertFalse(output.exists())
                self.assertFalse(self.report.exists())
                self.assertFalse(list(self.root.rglob("*.tmp.*")))

    def test_absent_probe_lookup_error_is_not_a_missing_alias(self):
        maximum = os.pathconf(self.root, "PC_NAME_MAX")
        output = self.root / ("o" * maximum)
        output.write_bytes(b"valid output name\n")
        output.unlink()
        self.assertEqual(self.invoke("-S", "-o", str(output), check=False).returncode, 2)
        self.assertFalse(output.exists())
        self.assertFalse(self.report.exists())
        self.assertFalse(list(self.root.rglob("*.tmp.*")))

    def test_report_empty_path_is_rejected(self):
        result = self.invoke("--ownership-report", "")
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.report.exists())

    def test_missing_entry_before_initialization_creates_no_report(self):
        missing = self.root / "missing.ft"
        self.assertEqual(self.invoke("--json", entry=missing).returncode, 2)
        self.assertEqual(self.result.stdout, b"")
        self.assertFalse(self.report.exists())

    def test_help_names_the_two_options(self):
        result = subprocess.run([OPTIONS.fort, "--help"], capture_output=True, check=False)
        self.assertEqual(result.returncode, 0)
        self.assertIn(b"--ownership-check", result.stdout)
        self.assertIn(b"--ownership-report <file>", result.stdout)

    @staticmethod
    def restore_environment(key, value):
        if value is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = value


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
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
