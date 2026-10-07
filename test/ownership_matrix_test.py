#!/usr/bin/env python3
"""Test the ownership qualification matrix with a fake compiler and synthetic manifests.

A fake compiler supplies every verdict here. These tests establish no ownership verdict.
"""

from contextlib import redirect_stderr, redirect_stdout
import collections
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ownership_matrix", REPOSITORY / "tools/ownership_matrix.py"
)
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)

# The fake compiler reads its plan from FAKE_FORT_PLAN. A plan maps a file name to an entry.
# An entry gives the exit status, stderr, the report members and the emitted IR. Its
# "variants" member overrides the entry for one option name or one mode.
FAKE_FORT = r"""#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
if args == ["--version"]:
    print("fort fake 1")
    sys.exit(0)
plan = json.load(open(os.environ["FAKE_FORT_PLAN"]))
log = os.environ.get("FAKE_FORT_LOG")
if log:
    with open(log, "a") as stream:
        stream.write(json.dumps(args) + "\n")
report = out = None
flags = set()
i = 0
while i < len(args):
    arg = args[i]
    if arg in ("--ownership-report", "-o", "--std-dir", "-I"):
        if arg == "--ownership-report":
            report = args[i + 1]
        if arg == "-o":
            out = args[i + 1]
        i += 2
        continue
    flags.add(arg)
    i += 1
name = args[-1]
if name == "main.ft":
    name = open(name).read().split()[1].rstrip(";") + ".ft"
entry = dict(plan.get(name, {}))
option = {(False, False): "checked", (True, False): "release", (False, True): "no_bounds",
          (True, True): "release_no_bounds"}[("--release" in flags, "--no-bounds-check" in flags)]
mode = "check" if "--check" in flags else "build"
for key in (option, mode, mode + ":" + option):
    entry.update(entry.get("variants", {}).get(key, {}))
selected = "--ownership-check" in flags
status = entry.get("exit", 0) if selected else entry.get("plain_exit", 0)
sys.stderr.write(entry.get("stderr", "") if selected else "")
if report and not entry.get("no_report"):
    with open(report, "w") as stream:
        json.dump({"verdict": entry.get("verdict", "accepted" if status == 0 else "rejected"),
                   "exit_status": entry.get("report_exit", status),
                   "totals": {"violations": entry.get("violations", 0)},
                   "first_incomplete": entry.get("first_incomplete"),
                   "failure": entry.get("failure"), "files": entry.get("files", [])}, stream)
if out and status == 0:
    with open(out, "w") as stream:
        stream.write(entry.get("ir", "ir\n") if selected else entry.get("plain_ir",
                                                                         entry.get("ir", "ir\n")))
sys.exit(status)
"""

ORIGINAL = "fn demo() i32 {\n    return 1;\n}\n"
ORIGINAL_REJECT = "fn bad() i32 {\n    return 2;\n}\n"
COUNTERPART = "//! run\n// A counterpart.\nfn main() i32 {\n    return 0;\n}\n"
REJECT = "//! fail\n// A reject case.\nfn main() i32 {\n    return 0; //! error: 'x'\n}\n"
INCOMPLETE = {"stage": "graph", "code": "source_coverage", "source": None, "limit": None}


def sha(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def diagnostic(path, line, col, kind, message):
    return {"path": path, "line": line, "col": col, "kind": kind, "message": message}


def result(verdict="accepted", violations=0, diagnostics=(), mode="check", option="checked"):
    return {
        "mode": mode,
        "option": option,
        "exit": 0,
        "verdict": verdict,
        "violations": violations,
        "first_incomplete": None,
        "diagnostics": list(diagnostics),
        "ir_equal": None,
    }


class Tree:
    """A temporary checkout with one approved manifest, two originals and three cases."""

    def __init__(self, base):
        self.root = Path(base) / "root"
        self.write("test/ownership/approved/ok.ft", ORIGINAL)
        self.write("test/ownership/approved/bad.ft", ORIGINAL_REJECT)
        self.approved = {
            "fixtures": [
                {
                    "group": 1,
                    "file": "ok.ft",
                    "source_sha256": sha(ORIGINAL),
                    "expected_verdict": "accept",
                    "expected_positions": [],
                },
                {
                    "group": 2,
                    "file": "bad.ft",
                    "source_sha256": sha(ORIGINAL_REJECT),
                    "expected_verdict": "reject",
                    "expected_positions": [
                        {
                            "file": "bad.ft",
                            "line": 2,
                            "column": 5,
                            "kind": "error",
                            "message": "returned value is bad",
                        },
                        {
                            "file": "bad.ft",
                            "line": 1,
                            "column": 4,
                            "kind": "note",
                            "message": "the function starts here",
                        },
                    ],
                },
            ]
        }
        self.write_json("test/ownership/approved/manifest.json", self.approved)
        self.write("q/counterparts/bad_ok.ft", COUNTERPART)
        self.write("q/zero/leak.ft", REJECT)
        self.manifest = {
            "kind": matrix.KIND,
            "version": 1,
            "registered": False,
            "approved_manifests": ["test/ownership/approved/manifest.json"],
            "counts": {},
            "cases": [
                {
                    "id": "ok",
                    "group": "original",
                    "path": "test/ownership/approved/ok.ft",
                    "modes": ["check", "build"],
                },
                {
                    "id": "bad",
                    "group": "original",
                    "path": "test/ownership/approved/bad.ft",
                    "modes": ["check"],
                },
                {
                    "id": "bad_ok",
                    "group": "counterpart",
                    "path": "q/counterparts/bad_ok.ft",
                    "modes": ["check", "build"],
                    "pairs": "bad",
                },
                {
                    "id": "leak",
                    "group": "zero_element",
                    "path": "q/zero/leak.ft",
                    "modes": ["check", "build"],
                },
            ],
        }
        self.manifest["counts"] = {
            "original": 2,
            "counterpart": 1,
            "zero_element": 1,
            "linear": 0,
            "approved_groups": 2,
            "approved_positions": 2,
            "paired": 1,
            "executions": 28,
        }

    def write(self, relative, text):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def write_json(self, relative, value):
        return self.write(relative, json.dumps(value))

    def save(self, manifest=None):
        return self.write_json("manifest.json", manifest or self.manifest)


class MatrixCase(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fort-matrix-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.tree = Tree(self.base)
        self.fort = self.base / "fake-fort"
        self.fort.write_text(FAKE_FORT, encoding="utf-8")
        self.fort.chmod(self.fort.stat().st_mode | stat.S_IXUSR)
        self.std = self.base / "std"
        self.std.mkdir()
        self.plan_path = self.base / "plan.json"
        self.log = self.base / "invocations.log"
        self.plan({})
        environment = {"FAKE_FORT_PLAN": str(self.plan_path), "FAKE_FORT_LOG": str(self.log)}
        self.saved = {key: os.environ.get(key) for key in environment}
        os.environ.update(environment)
        self.addCleanup(self.restore)

    def restore(self):
        for key, value in self.saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    def plan(self, value):
        self.plan_path.write_text(json.dumps(value), encoding="utf-8")

    def validate(self, manifest=None):
        return matrix.validate(self.tree.root, manifest or self.tree.manifest)

    def rejects(self, mutate, text):
        manifest = copy.deepcopy(self.tree.manifest)
        mutate(manifest)
        with self.assertRaisesRegex(matrix.ManifestError, text):
            self.validate(manifest)

    def run_matrix(self, *filters):
        return matrix.run_matrix(self.fort, self.std, self.tree.root, self.tree.save(), filters)

    def case(self, report, case_id):
        return next(c for c in report["cases"] if c["id"] == case_id)

    def invocations(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]


class CheckedInManifestTest(unittest.TestCase):
    def test_the_checked_in_manifest_has_its_counts(self):
        counts = matrix.validate(REPOSITORY, matrix.read_json(REPOSITORY / matrix.MANIFEST))
        self.assertEqual(counts["original"], 24)
        self.assertEqual(counts["approved_groups"], 20)
        self.assertEqual(counts["approved_positions"], 38)
        self.assertEqual(counts["counterpart"], 21)
        self.assertEqual(counts["paired"], 16)
        self.assertEqual(counts["zero_element"], 3)
        self.assertEqual(counts["linear"], 61)
        self.assertEqual(counts["executions"], 808)

    def test_the_checked_in_manifest_is_not_registered(self):
        manifest = matrix.read_json(REPOSITORY / matrix.MANIFEST)
        self.assertIs(manifest["registered"], False)

    def test_each_counterpart_is_an_accept_case_and_each_pair_is_a_reject_original(self):
        manifest = matrix.read_json(REPOSITORY / matrix.MANIFEST)
        approved, _, _ = matrix.approved_fixtures(REPOSITORY, manifest)
        by_id = {case["id"]: case for case in manifest["cases"]}
        for case in manifest["cases"]:
            if case["group"] != "counterpart":
                continue
            expected = matrix.expectation(REPOSITORY, case, approved)
            self.assertEqual(expected["verdict"], "accept", case["id"])
            if "pairs" in case:
                original = matrix.expectation(REPOSITORY, by_id[case["pairs"]], approved)
                self.assertEqual(original["verdict"], "reject", case["id"])

    def test_the_zero_element_group_has_one_leak_one_del_and_one_move(self):
        manifest = matrix.read_json(REPOSITORY / matrix.MANIFEST)
        approved, _, _ = matrix.approved_fixtures(REPOSITORY, manifest)
        verdicts = {
            case["id"]: matrix.expectation(REPOSITORY, case, approved)["verdict"]
            for case in manifest["cases"]
            if case["group"] == "zero_element"
        }
        self.assertEqual(
            verdicts, {"zero_leak": "reject", "zero_del": "accept", "zero_move": "accept"}
        )

    def test_the_linear_group_keeps_the_fail_and_run_directories(self):
        manifest = matrix.read_json(REPOSITORY / matrix.MANIFEST)
        approved, _, _ = matrix.approved_fixtures(REPOSITORY, manifest)
        verdicts = [
            matrix.expectation(REPOSITORY, case, approved)["verdict"]
            for case in manifest["cases"]
            if case["group"] == "linear"
        ]
        self.assertEqual(verdicts.count("reject"), 20)
        self.assertEqual(verdicts.count("accept"), 41)

    def test_the_accept_originals_also_run_in_build_mode(self):
        manifest = matrix.read_json(REPOSITORY / matrix.MANIFEST)
        approved, _, _ = matrix.approved_fixtures(REPOSITORY, manifest)
        modes = collections.Counter(
            (matrix.expectation(REPOSITORY, c, approved)["verdict"], tuple(c["modes"]))
            for c in manifest["cases"]
            if c["group"] == "original"
        )
        self.assertEqual(modes, {("accept", ("check", "build")): 8, ("reject", ("check",)): 16})

    def test_the_patterns_equal_those_of_the_language_harness(self):
        spec = importlib.util.spec_from_file_location(
            "run_tests", REPOSITORY / "test/lang/run_tests.py"
        )
        harness = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(harness)
        self.assertEqual(matrix.ANNOTATION.pattern, harness.ANNOTATION_RE.pattern)
        self.assertEqual(matrix.HEADER.pattern, harness.REPORT_RE.pattern)
        self.assertEqual(matrix.RENDERED_PREFIX, harness.RENDERED_PREFIX)


class ValidateTest(MatrixCase):
    def test_a_valid_manifest_gives_its_counts(self):
        counts = self.validate()
        self.assertEqual(counts, self.tree.manifest["counts"])

    def test_the_kind_and_the_version_are_fixed(self):
        self.rejects(lambda m: m.update(kind="other"), "kind is not")
        self.rejects(lambda m: m.update(version=2), "version is not 1")

    def test_a_registered_manifest_is_refused(self):
        self.rejects(lambda m: m.update(registered=True), "not registered yet")

    def test_a_duplicate_id_or_path_is_refused(self):
        def duplicate_id(m):
            m["cases"][3]["id"] = "bad_ok"

        self.rejects(duplicate_id, "case id bad_ok appears twice")

        def duplicate_path(m):
            m["cases"][3]["path"] = "q/counterparts/bad_ok.ft"

        self.rejects(duplicate_path, "appears twice")

    def test_an_unknown_group_member_or_mode_is_refused(self):
        self.rejects(lambda m: m["cases"][2].update(group="extra"), "unknown group")
        self.rejects(lambda m: m["cases"][2].update(note="x"), "unknown or missing members")
        self.rejects(lambda m: m["cases"][2].pop("modes"), "unknown or missing members")
        self.rejects(lambda m: m["cases"][2].update(modes=["link"]), "bad modes")
        self.rejects(lambda m: m["cases"][2].update(modes=[]), "bad modes")
        self.rejects(lambda m: m["cases"][2].update(modes=["check", "check"]), "bad modes")

    def test_a_bad_case_id_is_refused(self):
        self.rejects(lambda m: m["cases"][2].update(id="Bad-Id"), "bad case id")

    def test_a_missing_file_is_refused(self):
        self.rejects(lambda m: m["cases"][3].update(path="q/zero/none.ft"), "is missing")

    def test_a_changed_original_is_refused(self):
        self.tree.write("test/ownership/approved/ok.ft", ORIGINAL + "\n")
        with self.assertRaisesRegex(matrix.ManifestError, "differs from its approved hash"):
            self.validate()

    def test_an_original_without_an_approved_row_is_refused(self):
        self.tree.write("test/ownership/approved/extra.ft", ORIGINAL)

        def extra(m):
            m["cases"].append(
                {
                    "id": "extra",
                    "group": "original",
                    "path": "test/ownership/approved/extra.ft",
                    "modes": ["check"],
                }
            )

        self.rejects(extra, "no approved manifest row")

    def test_an_approved_file_without_a_case_is_refused(self):
        self.rejects(lambda m: m["cases"].pop(0), "differ from the approved files")

    def test_an_approved_file_named_twice_is_refused(self):
        approved = copy.deepcopy(self.tree.approved)
        approved["fixtures"].append(approved["fixtures"][0])
        self.tree.write_json("test/ownership/approved/manifest.json", approved)
        with self.assertRaisesRegex(matrix.ManifestError, "ok.ft appears twice"):
            self.validate()

    def test_a_count_that_differs_is_refused(self):
        self.rejects(lambda m: m["counts"].update(counterpart=2), "differ from the measured")
        self.rejects(lambda m: m["counts"].update(executions=25), "differ from the measured")
        self.rejects(lambda m: m["counts"].pop("linear"), "differ from the measured")

    def test_a_pair_must_join_a_counterpart_to_an_original(self):
        self.rejects(lambda m: m["cases"][3].update(pairs="bad"), "only a counterpart pairs")
        self.rejects(lambda m: m["cases"][2].update(pairs="leak"), "pairs with no original")
        self.rejects(lambda m: m["cases"][2].update(pairs="none"), "pairs with no original")

    def test_an_accept_case_with_an_annotation_is_refused(self):
        self.tree.write("q/counterparts/bad_ok.ft", COUNTERPART + "// x //! error: y\n")
        with self.assertRaisesRegex(matrix.ManifestError, "a reject case needs"):
            self.validate()

    def test_a_reject_case_without_an_annotation_is_refused(self):
        self.tree.write("q/zero/leak.ft", "//! fail\nfn main() i32 {\n    return 0;\n}\n")
        with self.assertRaisesRegex(matrix.ManifestError, "a reject case needs"):
            self.validate()

    def test_a_case_without_a_directive_line_is_refused(self):
        self.tree.write("q/zero/leak.ft", "fn main() i32 {\n    return 0;\n}\n")
        with self.assertRaisesRegex(matrix.ManifestError, "first line is not"):
            self.validate()

    def test_paired_and_execution_counts_follow_the_cases(self):
        manifest = copy.deepcopy(self.tree.manifest)
        manifest["cases"][2].pop("pairs")
        manifest["cases"][3]["modes"] = ["check"]
        manifest["counts"].update(paired=0, executions=24)
        self.assertEqual(self.validate(manifest)["executions"], 24)


class ExpectationTest(MatrixCase):
    def test_annotations_give_lines_and_texts_and_skip_directive_lines(self):
        path = self.tree.write(
            "a.ft",
            "//! fail\n//! stderr: x //! error: no\nfn f() {\n"
            "    g(); //! error: first\n    h(); //! error:  second \n}\n",
        )
        self.assertEqual(matrix.annotations(path), [(4, "first"), (5, "second")])

    def test_an_annotation_needs_code_before_it(self):
        path = self.tree.write("b.ft", "//! fail\nfn f() {\n    //! error: alone\n}\n")
        self.assertEqual(matrix.annotations(path), [])

    def test_an_original_takes_its_errors_notes_and_columns_from_the_approved_row(self):
        approved, _, _ = matrix.approved_fixtures(self.tree.root, self.tree.manifest)
        expected = matrix.expectation(self.tree.root, self.tree.manifest["cases"][1], approved)
        self.assertEqual(expected["verdict"], "reject")
        self.assertEqual(expected["errors"], [(2, "returned value is bad")])
        self.assertEqual(expected["notes"], [(1, "the function starts here")])
        self.assertEqual(expected["columns"], [(2, 5)])

    def test_a_directive_case_takes_its_verdict_from_the_first_line(self):
        approved, _, _ = matrix.approved_fixtures(self.tree.root, self.tree.manifest)
        accept = matrix.expectation(self.tree.root, self.tree.manifest["cases"][2], approved)
        reject = matrix.expectation(self.tree.root, self.tree.manifest["cases"][3], approved)
        self.assertEqual((accept["verdict"], accept["errors"]), ("accept", []))
        self.assertEqual((reject["verdict"], reject["errors"]), ("reject", [(4, "'x'")]))


class DiagnosticsTest(unittest.TestCase):
    def test_header_lines_become_rows_and_rendered_lines_are_ignored(self):
        stderr = (
            "a.ft:5:12: error: invalid use of 'q'\n        return *q;\n"
            "               ^~\na.ft:5:12: note: reference source is no longer live\n"
            "fort: error: usage\n"
        )
        rows = matrix.parse_diagnostics(stderr, None)
        self.assertEqual(
            rows,
            [
                diagnostic("a.ft", 5, 12, "error", "invalid use of 'q'"),
                diagnostic("a.ft", 5, 12, "note", "reference source is no longer live"),
            ],
        )

    def test_the_standard_root_becomes_std(self):
        stderr = "/x/std/libc.ft:55:4: error: ownership proof is incomplete (graph: g)\n"
        rows = matrix.parse_diagnostics(stderr, Path("/x/std"))
        self.assertEqual(rows[0]["path"], "<std>/libc.ft")
        rows = matrix.parse_diagnostics(stderr, Path("/y/std"))
        self.assertEqual(rows[0]["path"], "/x/std/libc.ft")

    def test_an_indented_header_is_not_a_diagnostic(self):
        self.assertEqual(matrix.parse_diagnostics(" a.ft:1:1: error: x\n", None), [])


class LocatedReasonTest(unittest.TestCase):
    FILES = [{"id": 0, "path": "/w/case.ft"}, {"id": 1, "path": "/host/std/libc.ft"}]

    def report(self, file):
        reason = dict(INCOMPLETE, source={"file": file, "line": 55, "col": 4})
        return {"files": self.FILES, "first_incomplete": reason}

    def test_a_standard_file_is_named_below_the_root(self):
        found = matrix.located_reason(self.report(1), Path("/host/std"))
        self.assertEqual(found["source"], {"file": "<std>/libc.ft", "line": 55, "col": 4})

    def test_another_file_is_named_by_its_name(self):
        found = matrix.located_reason(self.report(0), Path("/host/std"))
        self.assertEqual(found["source"]["file"], "case.ft")

    def test_an_unknown_file_number_names_no_file(self):
        found = matrix.located_reason(self.report(7), Path("/host/std"))
        self.assertIsNone(found["source"]["file"])

    def test_a_missing_reason_or_source_stays_as_it_is(self):
        self.assertIsNone(matrix.located_reason(None, None))
        self.assertIsNone(matrix.located_reason({"first_incomplete": None}, None))
        found = matrix.located_reason({"first_incomplete": INCOMPLETE}, None)
        self.assertEqual(found, INCOMPLETE)

    def test_two_host_prefixes_give_one_reason(self):
        other = {
            "files": [{"id": 3, "path": "/guest/std/libc.ft"}],
            "first_incomplete": dict(INCOMPLETE, source={"file": 3, "line": 55, "col": 4}),
        }
        row = {"first_incomplete": matrix.located_reason(self.report(1), Path("/host/std"))}
        guest = {"first_incomplete": matrix.located_reason(other, Path("/guest/std"))}
        self.assertEqual(matrix.reason(row), matrix.reason(guest))


class ClassifyTest(unittest.TestCase):
    def report(self, **members):
        value = {
            "verdict": "rejected",
            "exit_status": 1,
            "totals": {"violations": 0},
            "first_incomplete": None,
            "failure": None,
        }
        value.update(members)
        return value

    def test_exit_0_with_an_accepted_report_is_accepted(self):
        report = self.report(verdict="accepted", exit_status=0)
        self.assertEqual(matrix.classify(0, report), "accepted")

    def test_exit_0_without_a_report_or_with_another_verdict_failed(self):
        self.assertEqual(matrix.classify(0, None), "failed")
        self.assertEqual(matrix.classify(0, self.report(exit_status=0)), "failed")

    def test_exit_2_a_timeout_or_a_signal_failed(self):
        self.assertEqual(matrix.classify(2, self.report(exit_status=2)), "failed")
        self.assertEqual(matrix.classify(None, None), "failed")
        self.assertEqual(matrix.classify(-11, None), "failed")

    def test_a_report_whose_status_differs_from_the_exit_failed(self):
        self.assertEqual(matrix.classify(1, self.report(exit_status=0)), "failed")
        self.assertEqual(matrix.classify(1, self.report(verdict="failed")), "failed")

    def test_exit_1_without_a_report_failed(self):
        self.assertEqual(matrix.classify(1, None), "failed")

    def test_a_source_failure_in_the_report_is_a_source_error(self):
        for code in ("source_error", "unsupported_lowering"):
            report = self.report(failure={"code": code})
            self.assertEqual(matrix.classify(1, report), "source_error")

    def test_violations_win_over_incomplete_proof(self):
        report = self.report(totals={"violations": 2}, first_incomplete=INCOMPLETE)
        self.assertEqual(matrix.classify(1, report), "violated")

    def test_incomplete_proof_without_a_violation_is_incomplete(self):
        report = self.report(first_incomplete=INCOMPLETE)
        self.assertEqual(matrix.classify(1, report), "incomplete")

    def test_exit_1_with_no_reason_failed(self):
        self.assertEqual(matrix.classify(1, self.report()), "failed")
        report = self.report(failure={"code": "analysis_failure"})
        self.assertEqual(matrix.classify(1, report), "failed")


class JudgeTest(unittest.TestCase):
    ACCEPT = {"verdict": "accept", "errors": [], "notes": [], "columns": []}
    REJECT = {"verdict": "reject", "errors": [(4, "'x'")], "notes": [], "columns": [(4, 5)]}

    def test_an_accept_case_matches_only_an_accepted_result(self):
        self.assertEqual(matrix.judge(self.ACCEPT, result(), "a.ft")["outcome"], "match")
        for verdict in ("violated", "incomplete", "source_error", "failed"):
            row = result(verdict)
            self.assertEqual(matrix.judge(self.ACCEPT, row, "a.ft")["outcome"], "mismatch")

    def test_a_reject_case_needs_a_violation_on_each_expected_line_and_no_other(self):
        good = result("violated", 1, [diagnostic("a.ft", 4, 5, "error", "lost 'x'")])
        self.assertEqual(
            matrix.judge(self.REJECT, good, "a.ft"),
            {"outcome": "match", "lines": True, "notes": True, "wording": True, "columns": True},
        )
        extra = result(
            "violated",
            2,
            [
                diagnostic("a.ft", 4, 5, "error", "lost 'x'"),
                diagnostic("a.ft", 6, 1, "error", "lost 'y'"),
            ],
        )
        self.assertEqual(matrix.judge(self.REJECT, extra, "a.ft")["outcome"], "mismatch")
        missing = result("violated", 1, [diagnostic("a.ft", 5, 1, "error", "lost 'x'")])
        self.assertEqual(matrix.judge(self.REJECT, missing, "a.ft")["outcome"], "mismatch")

    def test_a_reject_case_with_incomplete_proof_does_not_match(self):
        row = result("incomplete", 0, [diagnostic("a.ft", 4, 5, "error", "lost 'x'")])
        self.assertEqual(matrix.judge(self.REJECT, row, "a.ft")["outcome"], "mismatch")

    def test_a_violation_beside_incomplete_proof_does_not_match(self):
        row = result("violated", 1, [diagnostic("a.ft", 4, 5, "error", "lost 'x'")])
        row["first_incomplete"] = INCOMPLETE
        self.assertEqual(
            matrix.judge(self.REJECT, row, "a.ft"),
            {"outcome": "mismatch", "lines": True, "notes": True, "wording": True, "columns": True},
        )

    def test_an_accept_case_has_lines_only_without_errors(self):
        row = result("violated", 1, [diagnostic("a.ft", 2, 1, "error", "x")])
        self.assertFalse(matrix.judge(self.ACCEPT, row, "a.ft")["lines"])
        self.assertTrue(matrix.judge(self.ACCEPT, result(), "a.ft")["lines"])

    def test_errors_of_other_files_do_not_count_against_the_case(self):
        row = result(
            "violated",
            1,
            [
                diagnostic("a.ft", 4, 5, "error", "lost 'x'"),
                diagnostic("<std>/libc.ft", 9, 1, "error", "other"),
            ],
        )
        self.assertEqual(matrix.judge(self.REJECT, row, "a.ft")["outcome"], "match")

    def test_wording_and_columns_are_reported_apart_from_the_outcome(self):
        row = result("violated", 1, [diagnostic("a.ft", 4, 9, "error", "lost 'y'")])
        self.assertEqual(
            matrix.judge(self.REJECT, row, "a.ft"),
            {"outcome": "match", "lines": True, "notes": True, "wording": False, "columns": False},
        )

    def test_a_strict_judgement_needs_the_expected_notes(self):
        expected = dict(self.REJECT, notes=[(2, "starts here")])
        error = diagnostic("a.ft", 4, 5, "error", "lost 'x'")
        bare = result("violated", 1, [error])
        self.assertEqual(matrix.judge(expected, bare, "a.ft")["outcome"], "match")
        judged = matrix.judge(expected, bare, "a.ft", strict=True)
        self.assertEqual((judged["outcome"], judged["notes"]), ("mismatch", False))
        noted = result("violated", 1, [error, diagnostic("a.ft", 2, 1, "note", "starts here")])
        self.assertEqual(matrix.judge(expected, noted, "a.ft", strict=True)["outcome"], "match")
        moved = result("violated", 1, [error, diagnostic("a.ft", 3, 1, "note", "starts here")])
        self.assertEqual(matrix.judge(expected, moved, "a.ft", strict=True)["outcome"], "mismatch")

    def test_a_strict_judgement_needs_the_expected_wording(self):
        expected = dict(self.REJECT, notes=[(2, "starts here")])
        reworded = result(
            "violated",
            1,
            [
                diagnostic("a.ft", 4, 5, "error", "lost 'x'"),
                diagnostic("a.ft", 2, 1, "note", "begins here"),
            ],
        )
        judged = matrix.judge(expected, reworded, "a.ft", strict=True)
        self.assertEqual((judged["outcome"], judged["notes"]), ("mismatch", True))
        self.assertFalse(judged["wording"])
        self.assertEqual(matrix.judge(expected, reworded, "a.ft")["outcome"], "match")
        error_text = result("violated", 1, [diagnostic("a.ft", 4, 5, "error", "lost 'y'")])
        self.assertEqual(
            matrix.judge(self.REJECT, error_text, "a.ft", strict=True)["outcome"], "mismatch"
        )

    def test_a_strict_judgement_does_not_change_an_accept_case(self):
        self.assertEqual(
            matrix.judge(self.ACCEPT, result(), "a.ft", strict=True)["outcome"], "match"
        )

    def test_notes_do_not_satisfy_an_expected_error_line(self):
        row = result("violated", 1, [diagnostic("a.ft", 4, 5, "note", "lost 'x'")])
        self.assertEqual(matrix.judge(self.REJECT, row, "a.ft")["outcome"], "mismatch")


class RunTest(MatrixCase):
    def accepted_plan(self):
        return {
            "ok.ft": {},
            "bad.ft": {
                "exit": 1,
                "violations": 1,
                "stderr": "bad.ft:2:5: error: returned value is bad\n"
                "bad.ft:1:4: note: the function starts here\n",
            },
            "bad_ok.ft": {},
            "leak.ft": {"exit": 1, "violations": 1, "stderr": "leak.ft:4:5: error: lost 'x'\n"},
        }

    def test_each_case_runs_in_each_mode_and_option(self):
        self.plan(self.accepted_plan())
        report = self.run_matrix()
        self.assertEqual(report["totals"]["executions"], 28)
        self.assertEqual(len(self.invocations()), 28 + 8)  # eight unselected IR builds
        options = {(r["mode"], r["option"]) for r in self.case(report, "bad_ok")["results"]}
        self.assertEqual(options, {(m, o) for m in ("check", "build") for o, _ in matrix.OPTIONS})

    def test_the_options_reach_the_compiler(self):
        self.plan(self.accepted_plan())
        self.run_matrix("leak")
        calls = self.invocations()
        self.assertEqual(len(calls), 8)
        for call in calls:
            self.assertIn("--ownership-check", call)
            self.assertIn("--ownership-report", call)
            self.assertEqual(call[-1], "leak.ft")
        self.assertEqual(sum("--release" in c for c in calls), 4)
        self.assertEqual(sum("--no-bounds-check" in c for c in calls), 4)
        self.assertEqual(sum("--check" in c for c in calls), 4)
        self.assertEqual(sum("-S" in c for c in calls), 4)
        self.assertEqual(sum("--release" in c and "--no-bounds-check" in c for c in calls), 2)

    def test_expected_results_match_and_are_counted(self):
        self.plan(self.accepted_plan())
        report = self.run_matrix()
        totals = report["totals"]
        self.assertEqual((totals["match"], totals["mismatch"], totals["inconsistent"]), (4, 0, 0))
        self.assertEqual(totals["verdicts"]["accepted"], 16)
        self.assertEqual(totals["verdicts"]["violated"], 12)
        self.assertEqual(
            totals["groups"], {"original": 2, "counterpart": 1, "zero_element": 1, "linear": 0}
        )
        self.assertEqual(report["kind"], matrix.REPORT_KIND)
        self.assertEqual(report["compiler_version"], "fort fake 1")
        self.assertTrue(report["complete"])
        self.assertEqual(
            report["manifest_sha256"],
            hashlib.sha256((self.tree.root / "manifest.json").read_bytes()).hexdigest(),
        )

    def test_an_accepted_build_compares_selected_and_unselected_ir(self):
        self.plan(self.accepted_plan())
        report = self.run_matrix()
        rows = self.case(report, "bad_ok")["results"]
        self.assertEqual([r["ir_equal"] for r in rows if r["mode"] == "build"], [True] * 4)
        self.assertEqual([r["ir_equal"] for r in rows if r["mode"] == "check"], [None] * 4)
        self.assertEqual(report["totals"]["ir_compared"], 8)

    def test_different_ir_is_counted(self):
        plan = self.accepted_plan()
        plan["bad_ok.ft"] = {"ir": "selected\n", "plain_ir": "plain\n"}
        self.plan(plan)
        report = self.run_matrix()
        self.assertEqual(report["totals"]["ir_different"], 4)

    def test_an_unselected_build_failure_gives_unequal_ir(self):
        plan = self.accepted_plan()
        plan["bad_ok.ft"] = {"plain_exit": 1}
        self.plan(plan)
        report = self.run_matrix("bad_ok")
        self.assertEqual(report["totals"]["ir_different"], 4)

    def test_an_option_that_changes_the_verdict_is_inconsistent(self):
        plan = self.accepted_plan()
        plan["bad_ok.ft"] = {
            "variants": {
                "release": {
                    "exit": 1,
                    "first_incomplete": INCOMPLETE,
                    "stderr": "bad_ok.ft:3:1: error: ownership proof is incomplete\n",
                }
            }
        }
        self.plan(plan)
        report = self.run_matrix()
        case = self.case(report, "bad_ok")
        self.assertFalse(case["consistent"])
        self.assertEqual(case["outcome"], "mismatch")
        self.assertEqual(report["totals"]["inconsistent"], 1)

    def test_an_option_that_changes_only_a_diagnostic_is_inconsistent(self):
        plan = self.accepted_plan()
        plan["leak.ft"]["variants"] = {"no_bounds": {"stderr": "leak.ft:4:6: error: lost 'x'\n"}}
        self.plan(plan)
        report = self.run_matrix("leak")
        self.assertFalse(self.case(report, "leak")["consistent"])

    def test_modes_with_different_diagnostics_disagree(self):
        plan = self.accepted_plan()
        plan["leak.ft"]["variants"] = {"build": {"stderr": "leak.ft:4:5: error: lost 'y'\n"}}
        self.plan(plan)
        report = self.run_matrix("leak")
        case = self.case(report, "leak")
        self.assertTrue(case["consistent"])
        self.assertFalse(case["modes_agree"])

    def test_modes_with_different_incomplete_reasons_disagree(self):
        plan = self.accepted_plan()
        other = dict(INCOMPLETE, code="work_limit")
        plan["bad_ok.ft"] = {
            "exit": 1,
            "first_incomplete": INCOMPLETE,
            "variants": {"build": {"first_incomplete": other}},
        }
        self.plan(plan)
        report = self.run_matrix("bad_ok")
        self.assertFalse(self.case(report, "bad_ok")["modes_agree"])

    def test_a_run_names_the_reason_file_through_the_files_table(self):
        plan = self.accepted_plan()
        std_path = str(self.std.resolve() / "libc.ft")
        plan["bad_ok.ft"] = {
            "exit": 1,
            "files": [{"id": 0, "path": "bad_ok.ft"}, {"id": 1, "path": std_path}],
            "first_incomplete": dict(INCOMPLETE, source={"file": 1, "line": 55, "col": 4}),
        }
        self.plan(plan)
        report = matrix.run_matrix(
            self.fort, self.std.resolve(), self.tree.root, self.tree.save(), ("bad_ok",)
        )
        row = self.case(report, "bad_ok")["results"][0]
        self.assertEqual(row["first_incomplete"]["source"]["file"], "<std>/libc.ft")

    def test_modes_that_disagree_are_counted(self):
        plan = self.accepted_plan()
        plan["bad_ok.ft"] = {"variants": {"build": {"exit": 1, "first_incomplete": INCOMPLETE}}}
        self.plan(plan)
        report = self.run_matrix()
        case = self.case(report, "bad_ok")
        self.assertTrue(case["consistent"])
        self.assertFalse(case["modes_agree"])
        self.assertEqual(report["totals"]["modes_disagree"], 1)

    def test_violations_beside_incomplete_proof_are_counted_apart(self):
        plan = self.accepted_plan()
        plan["bad.ft"]["first_incomplete"] = INCOMPLETE
        self.plan(plan)
        report = self.run_matrix()
        totals = report["totals"]
        self.assertEqual(self.case(report, "bad")["outcome"], "mismatch")
        self.assertEqual((totals["violated_on_expected_lines"], totals["with_incomplete"]), (12, 4))

    def test_incomplete_proof_is_a_mismatch_for_both_verdicts(self):
        incomplete = {
            "exit": 1,
            "first_incomplete": INCOMPLETE,
            "stderr": "/s/libc.ft:55:4: error: ownership proof is incomplete\n",
        }
        self.plan({name: incomplete for name in ("ok.ft", "bad.ft", "bad_ok.ft", "leak.ft")})
        report = self.run_matrix()
        self.assertEqual(report["totals"]["mismatch"], 4)
        self.assertEqual(report["totals"]["verdicts"]["incomplete"], 28)
        self.assertEqual(report["totals"]["inconsistent"], 0)

    def test_a_missing_report_failed_and_a_reported_source_failure_is_a_source_error(self):
        plan = self.accepted_plan()
        plan["bad_ok.ft"] = {
            "exit": 1,
            "no_report": True,
            "stderr": "bad_ok.ft:3:1: error: cannot change storage\n",
        }
        self.plan(plan)
        report = self.run_matrix("bad_ok")
        self.assertEqual(report["totals"]["verdicts"]["failed"], 8)
        plan["bad_ok.ft"] = {
            "exit": 1,
            "failure": {"code": "source_error"},
            "stderr": "bad_ok.ft:3:1: error: cannot change storage\n",
        }
        self.plan(plan)
        report = self.run_matrix("bad_ok")
        self.assertEqual(report["totals"]["verdicts"]["source_error"], 8)

    def test_an_original_builds_through_a_generated_entry(self):
        plan = self.accepted_plan()
        plan["ok.ft"] = {
            "variants": {
                "build": {"stderr": "/abs/ok.ft:2:1: note: seen\n"},
                "check": {"stderr": "ok.ft:2:1: note: seen\n"},
            }
        }
        self.plan(plan)
        report = self.run_matrix("approved/ok")
        builds = [c for c in self.invocations() if "-S" in c]
        self.assertEqual(len(builds), 8)
        approved = str(self.tree.root.resolve() / "test/ownership/approved")
        for call in builds:
            self.assertEqual(call[-1], "main.ft")
            self.assertEqual(call[call.index("-I") + 1], approved)
        case = self.case(report, "ok")
        self.assertTrue(case["modes_agree"])
        self.assertEqual(case["outcome"], "match")
        self.assertEqual(report["totals"]["ir_compared"], 4)

    def test_the_generated_entry_imports_the_original(self):
        place = matrix.target(
            {"group": "original"},
            self.tree.root / "test/ownership/approved/ok.ft",
            "build",
            self.base,
        )
        self.assertEqual(place[1], ["-I", str(self.tree.root / "test/ownership/approved")])
        self.assertEqual(place[2], "main.ft")
        text = (place[0] / "main.ft").read_text()
        self.assertTrue(text.startswith("import ok;\n"))
        self.assertIn("fn main() i32", text)
        check = matrix.target({"group": "original"}, self.tree.root / "x/ok.ft", "check", self.base)
        self.assertEqual(check, (self.tree.root / "x", [], "ok.ft"))

    def test_filters_select_cases_and_mark_the_report_partial(self):
        self.plan(self.accepted_plan())
        report = self.run_matrix("q/zero")
        self.assertEqual([c["id"] for c in report["cases"]], ["leak"])
        self.assertFalse(report["complete"])
        with self.assertRaisesRegex(matrix.ManifestError, "select no case"):
            self.run_matrix("nothing")

    def test_run_validates_the_manifest_first(self):
        self.tree.manifest["counts"]["original"] = 3
        with self.assertRaises(matrix.ManifestError):
            self.run_matrix()
        self.assertFalse(self.log.exists())


class CompareTest(unittest.TestCase):
    def report(self, host, verdict="violated", own=None, std=None):
        own = own or [diagnostic("a.ft", 4, 5, "error", "lost 'x'")]
        std = std or [diagnostic("<std>/libc.ft", 55, 4, "error", "incomplete")]
        rows = [
            result(verdict, 1, own + std, mode, option)
            for mode in ("check", "build")
            for option, _ in matrix.OPTIONS
        ]
        return {
            "kind": matrix.REPORT_KIND,
            "version": 1,
            "host": host,
            "manifest_sha256": "0" * 64,
            "source_revision": "a" * 40,
            "source_modified": False,
            "complete": True,
            "cases": [{"id": "a", "path": "q/a.ft", "results": rows}],
        }

    def test_equal_reports_of_two_hosts_have_no_difference(self):
        self.assertEqual(matrix.compare(self.report("Linux"), self.report("Darwin")), [])

    def test_standard_library_positions_may_differ_between_hosts(self):
        darwin = self.report("Darwin", std=[diagnostic("<std>/libc.ft", 61, 4, "error", "x")])
        self.assertEqual(matrix.compare(self.report("Linux"), darwin), [])

    def test_a_different_verdict_or_case_diagnostic_is_a_difference(self):
        problems = matrix.compare(self.report("Linux"), self.report("Darwin", "incomplete"))
        self.assertEqual(len(problems), 8)
        self.assertIn("a check checked: violated on Linux, incomplete on Darwin", problems)
        moved = self.report("Darwin", own=[diagnostic("a.ft", 4, 6, "error", "lost 'x'")])
        self.assertEqual(len(matrix.compare(self.report("Linux"), moved)), 8)

    def test_one_host_twice_is_a_difference(self):
        problems = matrix.compare(self.report("Linux"), self.report("Linux"))
        self.assertEqual(problems, ["both reports come from host Linux"])

    def test_different_manifests_and_case_sets_are_differences(self):
        other = self.report("Darwin")
        other["manifest_sha256"] = "1" * 64
        other["cases"][0]["id"] = "b"
        problems = matrix.compare(self.report("Linux"), other)
        self.assertEqual(
            problems,
            [
                "the reports use different manifests",
                "a: only one report has this case",
                "b: only one report has this case",
            ],
        )

    def test_different_execution_sets_are_a_difference(self):
        other = self.report("Darwin")
        other["cases"][0]["results"].pop()
        problems = matrix.compare(self.report("Linux"), other)
        self.assertEqual(problems, ["a: the reports ran different executions"])

    def test_reports_must_name_one_clean_revision(self):
        other = self.report("Darwin")
        other["source_revision"] = "b" * 40
        problems = matrix.compare(self.report("Linux"), other)
        self.assertEqual(problems, ["the reports do not name one source revision"])
        other["source_revision"] = None
        problems = matrix.compare(self.report("Linux"), other)
        self.assertEqual(problems, ["the reports do not name one source revision"])
        other = self.report("Darwin")
        other["source_modified"] = True
        problems = matrix.compare(self.report("Linux"), other)
        self.assertEqual(problems, ["the Darwin report ran a modified or unknown tree"])
        other["source_modified"] = None
        self.assertEqual(len(matrix.compare(self.report("Linux"), other)), 1)

    def test_a_filtered_report_is_a_difference(self):
        other = self.report("Darwin")
        other["complete"] = False
        problems = matrix.compare(self.report("Linux"), other)
        self.assertEqual(problems, ["the Darwin report ran a filtered manifest"])

    def test_ir_equality_and_incomplete_reasons_are_compared(self):
        other = self.report("Darwin")
        other["cases"][0]["results"][4]["ir_equal"] = False
        self.assertEqual(len(matrix.compare(self.report("Linux"), other)), 1)
        other = self.report("Darwin")
        other["cases"][0]["results"][0]["first_incomplete"] = INCOMPLETE
        self.assertEqual(len(matrix.compare(self.report("Linux"), other)), 1)

    def located(self, line=55, limit=None):
        source = {"file": "<std>/libc.ft", "line": line, "col": 4}
        return dict(INCOMPLETE, source=source, limit=limit)

    def test_equal_located_reasons_are_no_difference(self):
        left = self.report("Linux")
        right = self.report("Darwin")
        left["cases"][0]["results"][0]["first_incomplete"] = self.located()
        right["cases"][0]["results"][0]["first_incomplete"] = self.located()
        self.assertEqual(matrix.compare(left, right), [])

    def test_a_different_std_source_line_is_a_difference(self):
        left = self.report("Linux")
        right = self.report("Darwin")
        left["cases"][0]["results"][0]["first_incomplete"] = self.located(55)
        right["cases"][0]["results"][0]["first_incomplete"] = self.located(61)
        self.assertEqual(len(matrix.compare(left, right)), 1)

    def test_a_different_w_limit_is_a_difference(self):
        limit = {"category": "W", "used": 10, "bound": 10}
        left = self.report("Linux")
        right = self.report("Darwin")
        left["cases"][0]["results"][0]["first_incomplete"] = self.located(limit=limit)
        right["cases"][0]["results"][0]["first_incomplete"] = self.located(
            limit=dict(limit, bound=12)
        )
        self.assertEqual(len(matrix.compare(left, right)), 1)

    def test_a_document_of_another_kind_is_refused(self):
        other = self.report("Darwin")
        other["kind"] = "fort-ownership-report"
        self.assertEqual(len(matrix.compare(self.report("Linux"), other)), 1)


class MainTest(MatrixCase):
    def call(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = matrix.main(list(argv))
        return status, out.getvalue(), err.getvalue()

    def run_args(self, *extra):
        output = self.base / "matrix.json"
        return self.call(
            "run",
            "--root",
            str(self.tree.root),
            "--manifest",
            str(self.tree.save()),
            "--fort",
            str(self.fort),
            "--std-dir",
            str(self.std),
            "--output",
            str(output),
            *extra,
        ), output

    def test_validate_prints_the_counts(self):
        status, out, _ = self.call(
            "validate", "--root", str(self.tree.root), "--manifest", str(self.tree.save())
        )
        self.assertEqual(status, 0)
        self.assertIn('"executions": 28', out)

    def test_validate_exits_2_on_a_bad_manifest(self):
        self.tree.manifest["registered"] = True
        status, _, err = self.call(
            "validate", "--root", str(self.tree.root), "--manifest", str(self.tree.save())
        )
        self.assertEqual(status, 2)
        self.assertIn("not registered yet", err)

    def test_run_exits_0_on_consistent_results_even_when_they_mismatch(self):
        self.plan({})
        (status, out, _), output = self.run_args()
        self.assertEqual(status, 0)
        self.assertIn("4 cases, 28 executions, 2 match, 2 mismatch, 0 inconsistent", out)
        self.assertIn("0 failed, 12 IR compared, 0 IR different", out)
        self.assertEqual(json.loads(output.read_text())["totals"]["mismatch"], 2)

    def test_require_expected_turns_a_mismatch_into_exit_1(self):
        self.plan({})
        (status, _, _), _ = self.run_args("--require-expected")
        self.assertEqual(status, 1)

    def test_run_exits_1_on_an_inconsistent_case(self):
        self.plan({"ok.ft": {"variants": {"release": {"exit": 1, "first_incomplete": INCOMPLETE}}}})
        (status, out, _), _ = self.run_args()
        self.assertEqual(status, 1)
        self.assertIn("1 inconsistent", out)

    def test_run_exits_1_on_different_ir(self):
        self.plan({"bad_ok.ft": {"ir": "a\n", "plain_ir": "b\n"}})
        (status, out, _), _ = self.run_args()
        self.assertEqual(status, 1)
        self.assertIn("4 IR different", out)

    def test_run_exits_1_when_an_execution_failed(self):
        self.plan({"ok.ft": {"exit": 2}})
        (status, out, _), _ = self.run_args()
        self.assertEqual(status, 1)
        self.assertIn("0 inconsistent, 0 modes disagree, 8 failed", out)
        self.plan({"leak.ft": {"exit": 1, "no_report": True}})
        (status, out, _), _ = self.run_args()
        self.assertEqual(status, 1)
        self.assertIn("8 failed", out)

    def test_the_report_records_the_judgement_and_the_revision(self):
        self.plan({})
        (_, _, _), output = self.run_args("--require-expected")
        report = json.loads(output.read_text())
        self.assertIs(report["require_expected"], True)
        self.assertIsNone(report["source_revision"])
        self.assertIsNone(report["source_modified"])

    def test_run_exits_2_when_no_case_is_selected(self):
        (status, _, err), output = self.run_args("nothing")
        self.assertEqual(status, 2)
        self.assertFalse(output.exists())

    def test_compare_exits_by_its_differences(self):
        left = self.base / "left.json"
        right = self.base / "right.json"
        report = CompareTest().report("Linux")
        left.write_text(json.dumps(report))
        right.write_text(json.dumps(dict(report, host="Darwin")))
        self.assertEqual(self.call("compare", str(left), str(right))[0], 0)
        status, out, _ = self.call("compare", str(left), str(left))
        self.assertEqual(status, 1)
        self.assertIn("compare: 1 differences", out)


if __name__ == "__main__":
    unittest.main()
