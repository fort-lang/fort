#!/usr/bin/env python3
"""Check actual compiler ownership reports and selected driver behavior."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import difflib
import json
import os
from pathlib import Path
import re
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


# The soundness gates of the solvers alone: test/ownership/heap_rows.ft prints the heap summary of
# each function of a file, and test/ownership/solvers_alone.ft prints the verdict of each solver
# alone for each faulty body (D17.14, D17.18).
OWNERSHIP = CHECKOUT / "test" / "ownership"
HEAP_ROWS_GOLDEN = OWNERSHIP / "heap_rows.txt"
FAULTY_PROBES = OWNERSHIP / "solvers" / "faulty.json"
FALSE_ACCEPTS = OWNERSHIP / "solvers" / "false_accepts.txt"
HEAP_ROWS_COLUMNS = "# name verdict reason returns interface post_slots post_atoms events\n"
# The manifest programs that heap_rows cannot read, each with its reason. lower_env checks a
# text in a sandbox whose `std` holds only a runtime. A skipped program that heap_rows reads
# fails the test, as a listed pass of test/lang/xfail.txt does.
HEAP_ROWS_SKIPPED = {
    "test/ownership/qualification/counterparts/range_owning_string_ok.ft": "imports std.str",
    "test/lang/run/ownership/056_own_string_round_trip.ft": "imports std.str",
}
SOLVERS = ("summary", "heap")
LEDGER_CLASSES = ("A", "B", "D", "E")
TEXT_ARRAY = re.compile(r"^(\s*)string\[[^\]]*\] (?:mut )?(\w+) = \{(.*)$")
STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# A function of a heap test that copies a base text and then a tail into `out`.
TEXT_JOINER = re.compile(r"^fn (\w+)\(string@ tail, string mut@ out\) u64 \{\n"
                         r"    u64 mut n = 0;\n    while \(n < (\w+)\.len\)", re.M)
LEDGER_ENTRY = re.compile(r"^(\S+):(\w+) (\w+) # (\w+)$")
LEDGER_BOUND = re.compile(r"^# at most (\d+) entries$", re.M)


def unescape(literal):
    """The text of the body of one fort string literal."""
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)),
                  literal)


def heap_texts(source):
    """The source texts of one heap test, as (label, lines) in source order.

    A text is a `string[N]` array whose literals are not all identifiers (a list of names is all
    identifiers). A top-level array has its name as label; an array inside a function has the
    label `<function>.<name>`. A local array named `tail` goes to a joiner function, which puts
    a top-level base text before it, so its text is the base and then the tail. The first call
    that takes `tail[..]` before the `}` that ends the function names the joiner.
    """
    joiners = dict(TEXT_JOINER.findall(source))
    lines = source.splitlines()
    owner = None
    arrays = []
    i = 0
    while i < len(lines):
        declared = re.match(r"fn (\w+)", lines[i])
        if declared:
            owner = declared.group(1)
        start = TEXT_ARRAY.match(lines[i])
        if not start:
            i += 1
            continue
        indent, name, rest = start.groups()
        body = [rest]
        while not re.search(r"\}\s*;\s*$", body[-1]):
            i += 1
            body.append(lines[i])
        elements = [unescape(text) for part in body for text in STRING_LITERAL.findall(part)]
        base = None
        if indent and name == "tail":
            end = next((n for n in range(i + 1, len(lines)) if lines[n].startswith("}")),
                       len(lines))
            calls = [m.group(1) for line in lines[i + 1:end]
                     for m in [re.search(r"(\w+)\(tail\[\.\.\]", line)] if m]
            if not calls or calls[0] not in joiners:
                raise ValueError(f"{owner}.tail: no joiner takes it")
            base = joiners[calls[0]]
        if elements and not all(re.fullmatch(r"\w*", text) for text in elements):
            arrays.append((owner if indent else None, name, elements, base))
        i += 1
    top = {name: elements for owner, name, elements, _ in arrays if owner is None}
    out = []
    for owner, name, elements, base in arrays:
        label = name if owner is None else f"{owner}.{name}"
        if label in (known for known, _ in out):
            raise ValueError(f"{label}: two texts have this label")
        out.append((label, (top[base] if base else []) + elements))
    return out


def faulty_bodies(lines, errors):
    """The sorted names of the functions that hold the error lines: for each line, the last
    function that a line at or before it declares."""
    declared = [(n + 1, m.group(1)) for n, line in enumerate(lines)
                for m in [re.match(r"fn (\w+)", line)] if m]
    out = set()
    for line in errors:
        owners = [name for at, name in declared if at <= line]
        if not owners:
            raise ValueError(f"line {line}: no function holds it")
        out.add(owners[-1])
    return sorted(out)


def read_ledger(text):
    """The entries of a false-accept ledger: {(file, body, solver): class}.

    A line is blank, a `#` comment, or `<file>:<body> <solver> # <class>`. Any other line, an
    unknown solver or class, and a repeated entry raise ValueError.
    """
    out = {}
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        entry = LEDGER_ENTRY.match(line)
        if not entry:
            raise ValueError(f"line {number}: not '<file>:<body> <solver> # <class>'")
        path, body, solver, kind = entry.groups()
        if solver not in SOLVERS:
            raise ValueError(f"line {number}: unknown solver {solver}")
        if kind not in LEDGER_CLASSES:
            raise ValueError(f"line {number}: unknown class {kind}")
        if (path, body, solver) in out:
            raise ValueError(f"line {number}: repeated entry")
        out[(path, body, solver)] = kind
    return out


def ledger_bound(text):
    """The N of the one `# at most N entries` line of a ledger. The bound only falls, so a
    change that lowers it is a one-line diff, and a ledger longer than N fails."""
    bounds = LEDGER_BOUND.findall(text)
    if len(bounds) != 1:
        raise ValueError("the ledger needs one '# at most N entries' line")
    return int(bounds[0])


def qualification_cases():
    """The cases of the qualification manifest: (path, expected verdict, error lines). An
    approved fixture names its error positions; a linear or zero-element case marks them with
    a `//! error:` directive."""
    fixtures = {}
    for name in json.loads((OWNERSHIP / "qualification" / "manifest.json").read_text())[
            "approved_manifests"]:
        for fixture in json.loads((CHECKOUT / name).read_text())["fixtures"]:
            fixtures[fixture["file"][:-3]] = fixture
    out = []
    for case in json.loads((OWNERSHIP / "qualification" / "manifest.json").read_text())["cases"]:
        lines = (CHECKOUT / case["path"]).read_text().splitlines()
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
        out.append((case["path"], verdict, errors))
    return out


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

    def test_empty_closure_keeps_unavailable_producers(self):
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
        self.assertEqual(document["analyses"][5], {"name": "calls_heap",
                                                   "producer": "unavailable",
                                                   "status": "incomplete"})
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
        # raw ledgers, version 5 the stored-borrow ledgers and version 6 the process-exit
        # ledgers and the boundary.
        self.assertEqual(coverage["version"], 6)
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
                         ["unexecuted", "unexecuted", "incomplete", "unexecuted"])
        # The local, stored-borrow, raw and process-exit analyses ran on no body and the
        # denominator is incomplete. The boundary reads every body, so it did not run either.
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
        self.assertEqual(document["totals"]["violations"], 0)
        self.assertNotIn(b"lost ownership", self.result.stderr)
        # The leak after an unknown call has only a possible continuation. The failure stays
        # in the local scope, because it fails without the call's effects too.
        after = self.local_row(document, "after_call")
        self.assertEqual((after["correspondence"], after["proof"]), ("complete", "incomplete"))
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
                    self.assertNotIn("invalid use", stderr)
                    self.assertEqual((row["correspondence"], row["proof"]),
                                     ("incomplete", "incomplete"))
                    self.assertEqual(document["totals"]["violations"], 0)
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


class GateInputTest(unittest.TestCase):
    """The pure helpers of the soundness gates, without a compiler."""

    HEAP_TEST = (
        "string[2] BASE = {\n"
        '    "struct node { i32 v; }",\n'
        '    "fn base() void { }",\n'
        "};\n"
        "fn joined(string@ tail, string mut@ out) u64 {\n"
        "    u64 mut n = 0;\n"
        "    while (n < BASE.len) { out[n] = BASE[n]; n += 1; }\n"
        "    return n;\n"
        "}\n"
        "fn first() void {\n"
        '    string[1] tail = {"fn one() void { panic(\\"no\\"); }"};\n'
        "    string[4] mut text = {};\n"
        "    u64 n = joined(tail[..], text[..]);\n"
        '    string[2] names = {"base", "one"};\n'
        "}\n"
        "fn second() void {\n"
        "    string[1] text = {\n"
        '        "fn two() void { }",\n'
        "    };\n"
        "}\n"
    )

    def test_heap_texts_join_each_tail_to_its_base(self):
        texts = heap_texts(self.HEAP_TEST)
        self.assertEqual([label for label, _ in texts], ["BASE", "first.tail", "second.text"])
        self.assertEqual(texts[1][1], ["struct node { i32 v; }", "fn base() void { }",
                                       'fn one() void { panic("no"); }'])
        self.assertEqual(texts[2][1], ["fn two() void { }"])

    def test_heap_texts_refuse_a_tail_without_a_joiner(self):
        text = self.HEAP_TEST.replace("joined(tail", "other(tail")
        with self.assertRaisesRegex(ValueError, "first.tail: no joiner takes it"):
            heap_texts(text)

    def test_heap_texts_look_for_the_joiner_only_in_the_function_of_the_tail(self):
        text = self.HEAP_TEST.replace("    u64 n = joined(tail[..], text[..]);\n", "")
        text += "fn later() void {\n    u64 n = joined(tail[..], text[..]);\n}\n"
        with self.assertRaisesRegex(ValueError, "first.tail: no joiner takes it"):
            heap_texts(text)

    def test_heap_texts_refuse_two_texts_with_one_label(self):
        text = self.HEAP_TEST.replace("fn second()", "fn first()")
        with self.assertRaisesRegex(ValueError, "first.text: two texts have this label"):
            heap_texts(text + text[text.index("fn first() void {\n    string[1] text"):])

    def test_heap_texts_skip_a_list_of_names(self):
        texts = heap_texts('string[2] NAMES = {"base", ""};\n')
        self.assertEqual(texts, [])

    def test_faulty_bodies_take_the_last_function_at_or_before_each_error(self):
        lines = ["struct s { i32 v; }", "fn a() void {", "}", "fn b() void {", "}"]
        self.assertEqual(faulty_bodies(lines, [2, 3]), ["a"])
        self.assertEqual(faulty_bodies(lines, [5, 3]), ["a", "b"])
        with self.assertRaisesRegex(ValueError, "line 1: no function holds it"):
            faulty_bodies(lines, [1])

    def test_ledger_reads_entries_and_skips_comments(self):
        ledger = read_ledger("# rule\n\nf.ft:main heap # E\ng.ft:x summary # A\n")
        self.assertEqual(ledger, {("f.ft", "main", "heap"): "E", ("g.ft", "x", "summary"): "A"})

    def test_ledger_refuses_a_bad_line(self):
        for text, message in (("f.ft:main heap\n", "line 1: not"),
                              ("f.ft:main both # A\n", "line 1: unknown solver both"),
                              ("f.ft:main heap # C\n", "line 1: unknown class C"),
                              ("f.ft:m heap # A\nf.ft:m heap # B\n", "line 2: repeated entry")):
            with self.subTest(text=text):
                with self.assertRaisesRegex(ValueError, message):
                    read_ledger(text)

    def test_ledger_of_comments_only_is_empty(self):
        self.assertEqual(read_ledger("# rule\n# at most 0 entries\n\n"), {})
        self.assertEqual(ledger_bound("# rule\n# at most 0 entries\n\n"), 0)

    def test_ledger_bound_needs_one_line(self):
        self.assertEqual(ledger_bound("# at most 13 entries\nf.ft:m heap # A\n"), 13)
        for text in ("f.ft:m heap # A\n", "# at most 1 entries\n# at most 2 entries\n"):
            with self.subTest(text=text):
                with self.assertRaisesRegex(ValueError, "one '# at most N entries' line"):
                    ledger_bound(text)

    def test_the_checked_in_ledger_stays_within_its_bound(self):
        text = FALSE_ACCEPTS.read_text()
        self.assertLessEqual(len(read_ledger(text)), ledger_bound(text))

    def test_each_probe_program_has_its_faulty_bodies(self):
        probes = json.loads(FAULTY_PROBES.read_text())["faulty"]
        files = sorted(path.name for path in (OWNERSHIP / "probes").glob("*.ft"))
        self.assertEqual(sorted(probes), files)
        for name, bodies in probes.items():
            lines = (OWNERSHIP / "probes" / name).read_text().splitlines()
            names = {m.group(1) for line in lines for m in [re.match(r"fn (\w+)", line)] if m}
            with self.subTest(probe=name):
                self.assertTrue(bodies)
                self.assertLessEqual(set(bodies), names)


class SoundnessGateTest(unittest.TestCase):
    """Builds the two gate programs once and runs them over their inputs."""

    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="fort-ownership-gates-")
        cls.root = Path(cls.temporary.name)
        cls.programs = {}
        for name in ("heap_rows", "solvers_alone"):
            binary = cls.root / name
            built = subprocess.run(
                [OPTIONS.fort, "--cc", OPTIONS.cc, "--std-dir", OPTIONS.std_dir,
                 "-I", str(CHECKOUT / "src" / "fort"),
                 "-I", str(CHECKOUT / "test" / "fort" / "support"),
                 "-o", str(binary), str(OWNERSHIP / f"{name}.ft")],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False, timeout=900)
            if built.returncode != 0:
                cls.temporary.cleanup()
                raise AssertionError(f"{name}.ft does not build: {built.stderr.decode()}")
            cls.programs[name] = binary

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_gate(self, name, path, *bodies):
        """Runs gate program `name` over `path`. The test environment writes its sandboxes into
        the working directory, so each run gets a directory."""
        where = Path(tempfile.mkdtemp(dir=self.root))
        return subprocess.run([str(self.programs[name]), str(path), *bodies], cwd=where,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
                              timeout=600)

    def gate(self, name, path, *bodies):
        """The stdout of run_gate, which must exit 0."""
        run = self.run_gate(name, path, *bodies)
        self.assertEqual(run.returncode, 0, f"{name} {path}: {run.stderr.decode()}")
        return run.stdout.decode()

    def heap_rows_inputs(self):
        """(label, path) of each input of heap_rows: the programs of the qualification manifest
        but HEAP_ROWS_SKIPPED, the probe programs, and each text of the 11 heap tests."""
        programs = sorted(path for path, _, _ in qualification_cases())
        self.assertEqual(len(programs), 109)
        self.assertTrue(set(HEAP_ROWS_SKIPPED) <= set(programs))
        out = [(path, CHECKOUT / path) for path in programs if path not in HEAP_ROWS_SKIPPED]
        probes = sorted((OWNERSHIP / "probes").glob("*.ft"))
        self.assertEqual(len(probes), 10)
        out += [(path.relative_to(CHECKOUT).as_posix(), path) for path in probes]
        heap_tests = sorted((CHECKOUT / "test" / "fort").glob("ownership_heap*_test.ft"))
        self.assertEqual(len(heap_tests), 11)
        texts = self.root / "texts"
        texts.mkdir(exist_ok=True)
        for test in heap_tests:
            found = heap_texts(test.read_text())
            self.assertTrue(found, test.name)
            for label, lines in found:
                text = texts / f"{test.stem}.{label}.ft"
                text.write_text("".join(line + "\n" for line in lines))
                out.append((f"{test.relative_to(CHECKOUT).as_posix()}:{label}", text))
        return out

    def test_heap_rows_match_the_golden(self):
        # Each row is the heap summary of one function. A change of the heap solver changes
        # rows, and the diff of the golden shows each changed verdict. `--bless` writes it.
        inputs = self.heap_rows_inputs()
        with ThreadPoolExecutor(max_workers=4) as pool:
            rows = list(pool.map(lambda item: self.gate("heap_rows", item[1]), inputs))
        found = HEAP_ROWS_COLUMNS + "".join(
            f"== {label}\n{text}" for (label, _), text in zip(inputs, rows))
        self.assertTrue(all(text for text in rows))
        if OPTIONS.bless:
            HEAP_ROWS_GOLDEN.write_text(found)
        expected = HEAP_ROWS_GOLDEN.read_text()
        diff = "".join(difflib.unified_diff(expected.splitlines(True), found.splitlines(True),
                                            "heap_rows.txt", "found", n=1))
        self.assertEqual(diff, "", "heap_rows changed; rerun with --bless and read the diff")

    def test_heap_rows_still_cannot_read_each_skipped_program(self):
        # A skip whose program heap_rows reads is stale: delete it and bless its rows.
        for path, reason in HEAP_ROWS_SKIPPED.items():
            with self.subTest(path=path):
                run = self.run_gate("heap_rows", CHECKOUT / path)
                self.assertNotEqual(run.returncode, 0, f"{path} reads; drop its skip")
                self.assertIn("not found", run.stderr.decode(), reason)

    def test_solvers_alone_prove_only_the_ledger(self):
        # A body is faulty because its source has the expected error: the error line of a
        # reject case, or the entry of faulty.json for a probe. Neither solver alone may prove
        # one outside the ledger, each entry must still prove, and the ledger holds at most the
        # entries that its bound line names.
        cases = [(path, errors) for path, verdict, errors in qualification_cases()
                 if verdict == "reject"]
        self.assertEqual(len(cases), 37)
        items = [(path, faulty_bodies((CHECKOUT / path).read_text().splitlines(), errors))
                 for path, errors in cases]
        self.assertEqual(sum(len(bodies) for _, bodies in items), 37)
        probes = json.loads(FAULTY_PROBES.read_text())["faulty"]
        items += [(f"test/ownership/probes/{name}", bodies) for name, bodies in probes.items()]
        self.assertEqual(sum(len(bodies) for _, bodies in items), 62)

        def verdicts(item):
            path, bodies = item
            rows = [line.split() for line in self.gate("solvers_alone", CHECKOUT / path,
                                                       *bodies).splitlines()]
            self.assertEqual([row[0] for row in rows], bodies, path)
            return [(path, row[0], solver, verdict) for row in rows
                    for solver, verdict in zip(SOLVERS, row[1:])]

        with ThreadPoolExecutor(max_workers=4) as pool:
            found = [row for rows in pool.map(verdicts, items) for row in rows]
        self.assertEqual(len(found), 2 * 62)
        proved = {(path, body, solver) for path, body, solver, verdict in found
                  if verdict == "proved"}
        ledger = read_ledger(FALSE_ACCEPTS.read_text())
        self.assertLessEqual(len(ledger), ledger_bound(FALSE_ACCEPTS.read_text()))
        self.assertEqual({entry for entry in proved if entry[2] == "summary"}, set())
        self.assertEqual(sorted(proved - set(ledger)), [], "a false accept outside the ledger")
        self.assertEqual(sorted(set(ledger) - proved), [], "a ledger entry no longer proves")


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True)
    parser.add_argument("--std-dir", required=True)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--opt", required=True)
    parser.add_argument("--bless", action="store_true",
                        help="write test/ownership/heap_rows.txt from the rows of heap_rows")
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.fort = str(Path(OPTIONS.fort).resolve())
    OPTIONS.std_dir = str(Path(OPTIONS.std_dir).resolve())
    unittest.main(argv=[sys.argv[0], *remaining])


if __name__ == "__main__":
    main()
