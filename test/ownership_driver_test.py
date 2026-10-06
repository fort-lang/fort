#!/usr/bin/env python3
"""Check actual compiler ownership reports and selected driver behavior."""

import argparse
import json
import os
from pathlib import Path
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

    def invoke(self, *flags, selected=True, report=True, check=True, standard=None, entry=None):
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
        self.candidates = audit.snapshot([self.root, self.standard_used])
        self.result = subprocess.run(
            argv, cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
            timeout=120,
        )
        for name, contents in self.candidates.items():
            self.assertEqual(Path(name).read_bytes(), contents, name)
        return self.result

    def evidence(self, *, complete=True, target=None, cfg=()):
        self.assertTrue(self.report.is_file(), self.result.stderr.decode())
        document = audit.read_json(self.report)[0]
        arguments = dict(
            argv=self.argv, cwd=self.root, root=str(self.entry_used.resolve()),
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

    def test_empty_closure_keeps_unavailable_producers(self):
        self.entry.write_text("i32 VALUE = 1;\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        self.assertEqual(document["bodies"], [])
        self.assertEqual(document["enumeration"]["selected_bodies"], 0)
        self.assertEqual(len(document["analyses"]), 7)
        self.assertEqual([row["producer"] for row in document["analyses"]][2:],
                         ["unavailable"] * 5)
        self.assertEqual([row["status"] for row in document["analyses"]][2:],
                         ["incomplete"] * 5)
        self.assertEqual([row["name"] for row in document["meters"]], ["graph_private"])

    def test_diagnostic_document_stays_version_one(self):
        self.assertEqual(self.invoke("--json").returncode, 1)
        coverage = self.evidence()
        diagnostic = json.loads(self.result.stdout)
        self.assertEqual(set(diagnostic), {"version", "files", "diagnostics", "symbols"})
        self.assertEqual(diagnostic["version"], 1)
        self.assertEqual(diagnostic["symbols"], [])
        self.assertIn("ownership proof is incomplete", diagnostic["diagnostics"][0]["message"])
        self.assertEqual(coverage["kind"], "fort-ownership-report")

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
        self.assertEqual([row["status"] for row in document["analyses"]][2:],
                         ["incomplete"] * 5)

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
        locals_text = "".join(f"i32 local{index} = 0;\n" for index in range(400))
        self.entry.write_text("fn many_locals() void {\n" + locals_text + "}\n")
        self.assertEqual(self.invoke().returncode, 1)
        document = self.evidence()
        meter = next(row for row in document["meters"] if row["name"] == "services")
        refusal = meter["first_refusal"]
        self.assertEqual(refusal["code"], "work_limit")
        self.assertEqual(refusal["source"]["line"], 2)
        self.assertEqual(refusal["limit"]["used"], document["limits"]["w"])
        self.assertEqual(refusal["limit"]["bound"], document["limits"]["w"])
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
        self.assertEqual(matches[0]["line"], 2)

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
