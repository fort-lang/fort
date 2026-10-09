#!/usr/bin/env python3
"""Test report integrity and runner evidence with synthetic compiler reports."""

from contextlib import redirect_stderr, redirect_stdout
import copy
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ownership_audit", REPOSITORY / "tools/ownership_audit.py"
)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)
LIMITS = {
    "version": 2,
    "d": 16,
    "r": 256,
    "p": 128,
    "g": 128,
    "h": 64,
    "t": 64,
    "e": 256,
    "w": 65536,
    "w_scale": 1024,
    "v": 512,
}
W_BASE = LIMITS["w"]
W_SCALE = LIMITS["w_scale"]
TARGET = "x86_64-linux-gnu"
# The analyses that version 5 integrates in each report, beside the graph and liveness.
PRODUCERS = ("local", "stored_borrows", "raw")


def incomplete(stage="graph", code="missing_producer", source=None, limit=None):
    return {"stage": stage, "code": code, "source": source, "limit": limit}


def make_report(argv, cwd, *, accepted=False, files=None):
    root = argv[-1]
    target = argv[argv.index("--target") + 1]
    standard = Path(argv[argv.index("--std-dir") + 1])
    cfg = [argv[index + 1] for index, value in enumerate(argv) if value == "--cfg"]
    files = files or [str(Path(cwd) / root), str(standard / "rt.ft")]
    rows = [
        {"id": index, "path": path, "module": Path(path).stem} for index, path in enumerate(files)
    ]
    bodies = []
    for index, row in enumerate(rows):
        for line_number, text in enumerate(Path(row["path"]).read_bytes().split(b"\n"), start=1):
            selected = text.lstrip(b" \t\r")
            if not selected.startswith(b"fn "):
                continue
            name_bytes = selected.split(b"(", 1)[0][3:]
            name = name_bytes.decode("utf-8")
            col = len(text) - len(selected) + 4
            source = {
                "file": index,
                "line": line_number,
                "col": col,
                "end_line": line_number,
                "end_col": col + len(name_bytes),
                "instance": 0,
                "module": row["module"],
                "name": name,
            }
            analyses = [
                {
                    "name": name,
                    "correspondence": "complete" if accepted else "unexecuted",
                    "solver": "complete" if accepted else "unexecuted",
                    "proof": "complete" if accepted else "incomplete",
                    "violations": 0,
                }
                for name in audit.ANALYSES
            ]
            bodies.append(
                {
                    "source": source,
                    "key": {"module": index, "declaration": len(bodies), "instance": 0},
                    "checking": "complete",
                    "lowering": "complete",
                    "verification": "complete",
                    "analyses": analyses,
                    "ownership": "complete" if accepted else "incomplete",
                    "violations": 0,
                    "first_incomplete": None if accepted else incomplete(),
                }
            )
    report = {
        "kind": "fort-ownership-report",
        "version": 5,
        "complete": True,
        "compiler_version": "fort synthetic 1",
        "invocation": {
            "entry": root,
            "cwd": str(cwd),
            "argv": argv[1:],
            "target": target,
            "configuration": audit.effective_configuration(target, cfg),
            "mode": {
                "check": True,
                "release": "--release" in argv,
                "no_bounds_check": "--no-bounds-check" in argv,
            },
        },
        "files": rows,
        "enumeration": {
            "complete": True,
            "selected_bodies": len(bodies),
            "inactive_declarations": 0,
        },
        "analyses": [
            {
                "name": name,
                "producer": "integrated" if accepted or name in PRODUCERS else "unavailable",
                "status": "complete" if accepted else "incomplete",
            }
            for name in audit.ANALYSES
        ],
        "limits": dict(LIMITS),
        "meters": [],
        "bodies": bodies,
        "totals": audit.derived_totals(bodies),
        "first_incomplete": None if accepted else incomplete(),
        "failure": None,
        "verdict": "accepted" if accepted else "rejected",
        "exit_status": 0 if accepted else 1,
    }
    fit_local_ledgers(report)
    return report


def body_ledger(index, name, owner, size, limits):
    bound = audit.work_bound(limits, size)
    return {
        "id": index,
        "name": name,
        "owner": dict(owner),
        "fir_size": size,
        "counts": [{"category": "W", "scope": "computation", "used": 0, "bound": bound}],
        "first_refusal": None,
    }


def owner_key(owner):
    return tuple(owner[name] for name in ("module", "declaration", "instance"))


def fit_local_ledgers(report):
    """Give each verified body of a report its service ledger and one ledger of each producer.

    A body ledger of a key that names no verified body goes. A verified body without a service
    ledger gets one of FIR size 0, so a graph sum keeps its value. The other meters keep their
    order, and the IDs follow it.
    """
    verified = [
        body["key"]
        for body in report["bodies"]
        if body["verification"] == "complete" and body["key"] is not None
    ]
    keys = {owner_key(key) for key in verified}
    meters = [
        meter
        for meter in report["meters"]
        if meter["name"] not in ("services",) + PRODUCERS
        or meter["owner"] is None
        or owner_key(meter["owner"]) in keys
    ]
    services = {
        owner_key(m["owner"]): m["fir_size"]
        for m in meters
        if m["name"] == "services" and m["owner"] is not None
    }
    for name in PRODUCERS:
        owned = {owner_key(m["owner"]) for m in meters if m["name"] == name and m["owner"]}
        for key in verified:
            if owner_key(key) in owned:
                continue
            if owner_key(key) not in services:
                meters.append(body_ledger(0, "services", key, 0, report["limits"]))
                services[owner_key(key)] = 0
            meters.append(body_ledger(0, name, key, services[owner_key(key)], report["limits"]))
    for index, meter in enumerate(meters):
        meter["id"] = index
    report["meters"] = meters


def localize(report, root, kind, stage=2):
    """Give the synthetic report an integrated local, stored or raw analysis at `stage`.

    Even bodies declare their obligations and prove them. Odd bodies have incomplete
    correspondence. `kind` puts a violation or an incomplete declared obligation into the
    first body of main.ft, or into the shared body of every root.
    """
    report["analyses"][stage].update(producer="integrated", status="incomplete")
    for index, body in enumerate(report["bodies"]):
        row = body["analyses"][stage]
        if index % 2 == 0:
            row.update(correspondence="complete", solver="complete", proof="complete")
        else:
            row.update(correspondence="incomplete", solver="complete", proof="incomplete")
        main_first = index == 0 and root.endswith("main.ft")
        shared = body["source"]["name"] == "shared"
        if (kind == "violation" and main_first) or (kind == "shared_violation" and shared):
            row.update(correspondence="complete", proof="violated", violations=1)
        if kind == "incomplete" and main_first:
            row.update(correspondence="complete", proof="incomplete")
        body["violations"] = row["violations"]
        proofs = [value["proof"] for value in body["analyses"]]
        body["ownership"] = "violated" if "violated" in proofs else "incomplete"
    report["totals"] = audit.derived_totals(report["bodies"])


class RepositoryFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fort audit ")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.checkout = self.base / "source"
        self.checkout.mkdir()
        self.standard = self.base / "standard"
        self.standard.mkdir()
        (self.standard / "rt.ft").write_text("fn rt() void {}\n")
        self.write("src/fort/main.ft", "fn main() void {}\n")
        self.write("src/fort/shared.ft", "fn shared() void {}\n")
        self.write(
            "src/fort/ownership_api.ft",
            f"u32 LIMIT_VERSION = {LIMITS['version']};\nfn api() void {{}}\n",
        )
        self.write(
            "src/fort/ownership_limits.ft",
            "".join(
                f"u64 PRODUCTION_{key.upper()} = {value};\n"
                for key, value in LIMITS.items()
                if key != "version"
            )
            + "fn limits() void {}\n",
        )
        self.git("init", "-q")
        self.git("config", "user.name", "Audit Test")
        self.git("config", "user.email", "audit@example.invalid")
        self.commit()
        self.compiler = self.base / "compiler"
        self.compiler.write_text("synthetic binary bytes\n")
        self.provenance = self.base / "compiler.json"
        self.refresh_provenance()
        self.report_path = self.base / "report.json"
        self.argv = audit.command(
            self.compiler,
            "src/fort/main.ft",
            self.report_path,
            TARGET,
            self.standard,
            [self.checkout / "src"],
            [],
            False,
            False,
        )
        self.report = make_report(self.argv, self.checkout)

    def write(self, path, text):
        path = self.checkout / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def git(self, *args):
        return (
            subprocess.check_output(
                ["git", "-C", str(self.checkout), *args], stderr=subprocess.PIPE
            )
            .decode()
            .strip()
        )

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-qm", "Create synthetic audit inputs")

    def clone(self, name):
        other = self.base / name
        subprocess.check_call(["git", "clone", "-q", str(self.checkout), str(other)])
        subprocess.check_call(["git", "-C", str(other), "config", "user.name", "Audit Test"])
        subprocess.check_call(
            ["git", "-C", str(other), "config", "user.email", "audit@example.invalid"]
        )
        return other

    def refresh_provenance(self):
        self.provenance.write_text(
            json.dumps(
                {
                    "revision": self.git("rev-parse", "HEAD"),
                    "sha256": audit.digest(self.compiler.read_bytes()),
                }
            )
        )

    def validate(self, report=None, **changes):
        options = {
            "argv": self.argv,
            "cwd": self.checkout,
            "root": "src/fort/main.ft",
            "target": TARGET,
            "cfg": audit.effective_configuration(TARGET, []),
            "checkout": self.checkout,
            "standard": self.standard,
            "exit_status": 1,
            "candidates": audit.snapshot([self.checkout / "src", self.standard]),
            "limits": LIMITS,
        }
        options.update(changes)
        return audit.validate_report(report or self.report, **options)

    def reject(self, mutate, pattern=None):
        report = copy.deepcopy(self.report)
        mutate(report)
        with self.assertRaisesRegex(audit.InvalidEvidence, pattern or ".+"):
            self.validate(report)

    def executable(self, mode="valid", limits=None, version="fort synthetic 1"):
        script = f"""#!{sys.executable}
import json
from pathlib import Path
import sys
sys.path.insert(0, {str(REPOSITORY / "test")!r})
from ownership_audit_test import fit_local_ledgers, make_report
if sys.argv[1:] == ["--version"]:
    print({version!r})
    sys.exit(0)
argv = sys.argv
report_path = Path(argv[argv.index("--ownership-report") + 1])
root = argv[-1]
shared = str(Path.cwd() / "src/fort/shared.ft")
standard = Path(argv[argv.index("--std-dir") + 1])
files = [str(Path.cwd() / root)]
if files[0] != shared:
    files.append(shared)
files.append(str(standard / "rt.ft"))
report = make_report(argv, Path.cwd(), accepted={mode == "accepted"!r}, files=files)
mode = {mode!r}
report["limits"] = {limits or LIMITS!r}
report["meters"] = []
fit_local_ledgers(report)
report["compiler_version"] = {version.strip()!r}
if mode == "version_mismatch":
    report["compiler_version"] = "wrong compiler version"
if mode.startswith("local_"):
    from ownership_audit_test import localize
    localize(report, root, mode[len("local_"):])
if mode.startswith("stored_"):
    from ownership_audit_test import localize
    localize(report, root, mode[len("stored_"):], stage=3)
if mode.startswith("raw_"):
    from ownership_audit_test import localize
    localize(report, root, mode[len("raw_"):], stage=4)
if mode == "incomplete":
    report["enumeration"]["complete"] = False
    report["first_incomplete"] = {{"stage": "enumeration", "code": "source_coverage",
                                     "source": None, "limit": None}}
if mode == "enumeration_work":
    report["enumeration"]["complete"] = False
    report["first_incomplete"] = {{"stage": "enumeration", "code": "work_limit",
        "source": None, "limit": {{"category": "W", "used": 0, "bound": {LIMITS["w"]}}}}}
if mode == "invalid_first" and root.endswith("main.ft"):
    report["totals"]["bodies"] += 1
if mode == "exit2":
    report["exit_status"] = 2
    report["verdict"] = "failed"
    report["failure"] = {{"stage": "tool", "code": "tool_failure", "source": None, "limit": None}}
if mode == "duplicate":
    report_path.write_text('{{"kind":1,"kind":2}}')
elif mode == "truncated":
    report_path.write_text('{{"kind":')
elif mode != "missing":
    report_path.write_text(json.dumps(report))
if mode == "source_change":
    path = Path(root)
    path.write_text(path.read_text() + "// changed bytes\\n")
if mode == "input_added":
    (standard / "new.ft").write_text("fn new_file() void {{}}\\n")
if mode == "input_deleted":
    (standard / "rt.ft").unlink(missing_ok=True)
if mode == "binary_change":
    path = Path(argv[0])
    path.write_text(path.read_text() + "# changed bytes\\n")
sys.exit(2 if mode == "exit2" else (0 if mode == "accepted" else 1))
"""
        self.compiler.write_text(script)
        self.compiler.chmod(0o755)
        self.refresh_provenance()

    def run_fixture(self, output="out", **changes):
        options = {
            "checkout": self.checkout,
            "compiler": self.compiler,
            "compiler_checkout": self.checkout,
            "compiler_provenance": self.provenance,
            "output": self.base / output,
            "target": TARGET,
            "standard": self.standard,
            "search_roots": [self.checkout / "src"],
        }
        options.update(changes)
        return audit.run_audit(**options)

    def validate_fixture(self, path=None, **changes):
        options = {
            "checkout": self.checkout,
            "compiler": self.compiler,
            "compiler_checkout": self.checkout,
            "compiler_provenance": self.provenance,
        }
        options.update(changes)
        return audit.validate_audit(path or self.base / "out/audit.json", **options)


class SchemaTests(RepositoryFixture):
    def test_valid_rejection_keeps_all_rows(self):
        inputs, identities, count = self.validate()
        self.assertEqual([row["path"] for row in inputs], ["src/fort/main.ft", "std/rt.ft"])
        self.assertEqual(len(identities), 2)
        self.assertEqual(count, 2)
        self.assertEqual(self.report["bodies"][0]["ownership"], "incomplete")

    def test_complete_proof_accepts_exit_zero(self):
        report = make_report(self.argv, self.checkout, accepted=True)
        self.assertEqual(self.validate(report, exit_status=0)[2], 2)

    def test_top_level_required_members(self):
        for member in self.report:
            with self.subTest(member=member):
                self.reject(lambda value, member=member: value.pop(member))

    def test_unknown_members_at_each_level(self):
        selectors = [
            lambda r: r,
            lambda r: r["invocation"],
            lambda r: r["invocation"]["mode"],
            lambda r: r["files"][0],
            lambda r: r["enumeration"],
            lambda r: r["analyses"][0],
            lambda r: r["limits"],
            lambda r: r["bodies"][0],
            lambda r: r["bodies"][0]["source"],
            lambda r: r["bodies"][0]["key"],
            lambda r: r["bodies"][0]["analyses"][0],
            lambda r: r["first_incomplete"],
            lambda r: r["totals"],
            lambda r: r["totals"]["analyses"][0],
        ]
        for index, select in enumerate(selectors):
            with self.subTest(index=index):
                self.reject(lambda value, select=select: select(value).update(extra=0))

    def test_unknown_versions_and_invalid_complete_types(self):
        # Version 3 adds local ledgers, version 4 raw ledgers and version 5 stored ledgers.
        # Earlier versions lack them.
        for version in (0, 1, 2, 3, 4, 6, True, -1, 5.0, "5", audit.U64_MAX + 1):
            with self.subTest(version=version):
                self.reject(lambda value: value.update(version=version))
        for complete in (False, 0, 1, "true", None):
            with self.subTest(complete=complete):
                self.reject(lambda value: value.update(complete=complete))

    def test_integer_boundaries(self):
        paths = [
            ("enumeration", "selected_bodies"),
            ("enumeration", "inactive_declarations"),
            ("limits", "d"),
            ("totals", "bodies"),
            ("totals", "violations"),
        ]
        for first, last in paths:
            for invalid in (-1, True, False, 1.5, "1", None, audit.U64_MAX + 1):
                with self.subTest(path=(first, last), invalid=invalid):
                    self.reject(lambda value: value[first].update({last: invalid}))
        self.report["enumeration"]["inactive_declarations"] = audit.U64_MAX
        self.validate()

    def test_names_and_statuses(self):
        for field, invalid in (
            ("kind", "wrong"),
            ("compiler_version", ""),
            ("verdict", "accepted"),
            ("exit_status", 0),
            ("exit_status", True),
        ):
            with self.subTest(field=field):
                self.reject(lambda value: value.update({field: invalid}))
        for field in ("checking", "lowering", "verification", "ownership"):
            self.reject(lambda value: value["bodies"][0].update({field: "unknown"}))
        for field in ("correspondence", "solver", "proof"):
            self.reject(lambda value: value["bodies"][0]["analyses"][0].update({field: "unknown"}))

    def test_invocation_mismatches(self):
        values = {
            "entry": "src/fort/shared.ft",
            "cwd": str(self.base),
            "argv": self.argv,
            "target": "arm64-apple-macosx11.0.0",
            "configuration": [],
        }
        for field, invalid in values.items():
            with self.subTest(field=field):
                self.reject(lambda value: value["invocation"].update({field: invalid}))
        self.reject(lambda value: value["invocation"]["mode"].update(release=True))
        self.reject(lambda value: value["invocation"]["mode"].update(check=1))

    def test_configuration_rejects_duplicates_unsorted_and_wrong_types(self):
        cfg = self.report["invocation"]["configuration"]
        for invalid in ([*cfg, cfg[0]], list(reversed(cfg)), [{"key": "x", "value": 1}]):
            with self.subTest(invalid=invalid):
                self.reject(lambda value: value["invocation"].update(configuration=invalid))

    def test_missing_or_wrong_analysis_rows(self):
        for selector in (
            lambda r: r["analyses"],
            lambda r: r["bodies"][0]["analyses"],
            lambda r: r["totals"]["analyses"],
        ):
            self.reject(lambda value: selector(value).pop())
            self.reject(lambda value: selector(value).reverse())
            self.reject(lambda value: selector(value).append(copy.deepcopy(selector(value)[0])))
            self.reject(lambda value: selector(value)[0].update(name="other"))

    def test_unavailable_producer_cannot_claim_complete(self):
        self.reject(lambda value: value["analyses"][0].update(status="complete"))
        self.reject(lambda value: value["analyses"][0].update(producer="other"))
        self.reject(lambda value: value["bodies"][0]["analyses"][0].update(proof="complete"))
        self.reject(
            lambda value: value["bodies"][0]["analyses"][0].update(correspondence="complete")
        )

    def test_incomplete_enumeration_never_proves_denominator(self):
        self.reject(
            lambda value: value["enumeration"].update(complete=False), "enumeration: incomplete"
        )
        self.reject(lambda value: value["enumeration"].update(selected_bodies=1))
        self.reject(lambda value: value["enumeration"].update(selected_bodies=3))

    def test_duplicate_file_and_body_identities(self):
        self.reject(lambda value: value["files"].append(dict(value["files"][0], id=2)))
        self.reject(lambda value: value["files"][1].update(id=2))
        self.reject(lambda value: value["bodies"].append(copy.deepcopy(value["bodies"][0])))
        self.reject(lambda value: value["bodies"][1].update(key=value["bodies"][0]["key"]))

    def test_paths_must_come_from_pre_invocation_snapshot(self):
        self.reject(lambda value: value["files"][0].update(path=str(self.base / "absent.ft")))
        self.reject(
            lambda value: value["files"][0].update(path=str(self.checkout / "src/fort/shared.ft"))
        )
        self.assertEqual(
            audit.normalize(
                str(self.standard / "rt.ft"), self.checkout, self.checkout, self.standard
            ),
            "std/rt.ft",
        )
        external = self.base / "other.ft"
        self.assertEqual(
            audit.normalize(str(external), self.checkout, self.checkout, self.standard),
            str(external),
        )

    def test_body_name_ranges_and_instances(self):
        mutations = [
            ("file", 9),
            ("line", 0),
            ("line", 999),
            ("col", 0),
            ("col", 5),
            ("end_col", 4),
            ("end_line", 2),
            ("module", "wrong"),
            ("name", "wrong"),
            ("instance", 1),
            ("instance", True),
        ]
        for field, invalid in mutations:
            with self.subTest(field=field, invalid=invalid):
                self.reject(lambda value: value["bodies"][0]["source"].update({field: invalid}))

    def test_function_keys_reject_boolean_and_u32_overflow(self):
        for field in ("module", "declaration", "instance"):
            for invalid in (True, -1, audit.U32_MAX + 1):
                with self.subTest(field=field, invalid=invalid):
                    self.reject(lambda value: value["bodies"][0]["key"].update({field: invalid}))
        self.reject(lambda value: value["bodies"][0].update(key=None))

    def test_keys_can_skip_declarations_without_bodies(self):
        self.report["bodies"][0]["key"] = {"module": 3, "declaration": 7, "instance": 0}
        self.report["bodies"][1]["key"] = {"module": 8, "declaration": 17, "instance": 0}
        fit_local_ledgers(self.report)
        self.validate()
        self.reject(lambda value: value["bodies"].reverse())

    def test_all_status_counters_and_totals_are_exact(self):
        for stage in (*audit.STAGES, "ownership"):
            for status in self.report["totals"][stage]:
                with self.subTest(stage=stage, status=status):
                    self.reject(lambda value: value["totals"][stage].update({status: True}))
                    self.reject(lambda value: value["totals"][stage].update({status: 3}))
                    self.reject(lambda value: value["totals"][stage].pop(status))
        for index, row in enumerate(self.report["totals"]["analyses"]):
            for field in ("correspondence", "solver", "proof"):
                for status in row[field]:
                    with self.subTest(index=index, field=field, status=status):
                        self.reject(
                            lambda value: value["totals"]["analyses"][index][field].update(
                                {status: 3}
                            )
                        )
            self.reject(lambda value: value["totals"]["analyses"][index].update(violations=1))

    def test_report_requires_first_incomplete_reason(self):
        self.reject(lambda value: value.update(first_incomplete=None))
        self.reject(lambda value: value["bodies"][0].update(first_incomplete=None))
        self.reject(lambda value: value["first_incomplete"].update(code="none"))
        self.reject(lambda value: value["first_incomplete"].update(stage="other"))

    def test_reason_ranges_and_limits(self):
        src = {
            key: value
            for key, value in self.report["bodies"][0]["source"].items()
            if key in ("file", "line", "col", "end_line", "end_col")
        }
        self.report["first_incomplete"] = incomplete(
            "liveness", "work_limit", src, {"category": "W", "used": 65536, "bound": 65536}
        )
        self.validate()
        self.reject(lambda value: value["first_incomplete"]["limit"].update(category="w"))
        self.reject(lambda value: value["first_incomplete"]["limit"].update(used=True))
        self.reject(lambda value: value["first_incomplete"]["source"].update(file=9))
        self.reject(lambda value: value["first_incomplete"]["source"].update(end_col=1))
        self.reject(lambda value: value.update(failure=incomplete()))

    def test_production_limit_table(self):
        self.assertEqual(audit.production_limits(self.checkout), LIMITS)
        self.reject(lambda value: value["limits"].update(w=65537))
        self.reject(lambda value: value["limits"].update(version=1))
        self.reject(lambda value: value["limits"].update(d=0))
        path = self.checkout / "src/fort/ownership_limits.ft"
        path.write_text(path.read_text().replace("65536", "65537"))
        with self.assertRaisesRegex(audit.InvalidEvidence, "git bytes"):
            audit.production_limits(self.checkout)

    def test_meters_use_separate_ledgers_and_valid_owners(self):
        owner = self.report["bodies"][0]["key"]
        self.report["analyses"][1]["producer"] = "integrated"
        self.report["bodies"][0]["analyses"][1].update(
            correspondence="complete", solver="incomplete"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.report["meters"] = [
            {
                "id": 0,
                "name": "graph_private",
                "owner": None,
                "fir_size": 0,
                "counts": [{"category": "W", "scope": "computation", "used": 10, "bound": 65536}],
                "first_refusal": None,
            },
            {
                "id": 1,
                "name": "services",
                "owner": dict(owner),
                "fir_size": 0,
                "counts": [
                    {"category": "W", "scope": "computation", "used": 65536, "bound": 65536}
                ],
                "first_refusal": incomplete(
                    "liveness", "work_limit", limit={"category": "W", "used": 65536, "bound": 65536}
                ),
            },
        ]
        fit_local_ledgers(self.report)
        self.validate()
        for mutation in (
            lambda r: r["meters"][1].update(id=3),
            lambda r: r["meters"][0].update(name="combined"),
            lambda r: r["meters"][1].update(owner=0),
            lambda r: r["meters"][1]["owner"].update(declaration=999),
            lambda r: r["meters"][0]["counts"][0].update(scope="other"),
            lambda r: r["meters"][0]["counts"][0].update(category="other"),
            lambda r: r["meters"][0]["counts"][0].update(bound=9),
            lambda r: r["meters"][0]["counts"][0].update(used=65537),
            lambda r: r["meters"][0]["counts"].append(r["meters"][0]["counts"][0]),
        ):
            self.reject(mutation)

    def test_work_bound_values_follow_the_fir_size_function(self):
        for bound, expected in (
            (65536, True),
            (W_BASE + W_SCALE, True),
            (W_BASE + W_SCALE * 77579, True),
            (audit.U64_MAX, True),
            (65535, False),
            (65537, False),
            (W_BASE + W_SCALE - 1, False),
            (0, False),
        ):
            with self.subTest(bound=bound):
                self.assertEqual(audit.is_work_bound(LIMITS, bound), expected)

    def test_production_limits_require_one_w_scale_declaration(self):
        path = self.checkout / "src/fort/ownership_limits.ft"
        original = path.read_text()
        for text in (
            original.replace(f"u64 PRODUCTION_W_SCALE = {W_SCALE};\n", ""),
            original + f"u64 PRODUCTION_W_SCALE = {W_SCALE};\n",
            original.replace(f"PRODUCTION_W_SCALE = {W_SCALE}", "PRODUCTION_W_SCALE = 0"),
        ):
            with self.subTest(text=text[-40:]):
                path.write_text(text)
                self.commit()
                with self.assertRaises(audit.InvalidEvidence):
                    audit.production_limits(self.checkout)
        path.write_text(original)
        self.commit()
        self.assertEqual(audit.production_limits(self.checkout)["w_scale"], W_SCALE)

    def test_graph_fir_size_is_the_sum_of_every_body_ledger(self):
        sizes = [3, 5, 9]
        bodies = self.report["bodies"][:2]
        self.report["analyses"][1]["producer"] = "integrated"
        for body in self.report["bodies"]:
            body["analyses"][1].update(correspondence="complete", solver="incomplete")
        self.report["totals"] = audit.derived_totals(self.report["bodies"])

        def ledger(index, name, owner, size):
            bound = W_BASE + W_SCALE * size
            return {
                "id": index,
                "name": name,
                "owner": owner,
                "fir_size": size,
                "counts": [{"category": "W", "scope": "computation", "used": 0, "bound": bound}],
                "first_refusal": None,
            }

        self.report["meters"] = [ledger(0, "graph_private", None, sizes[0] + sizes[1])] + [
            ledger(index + 1, "services", dict(body["key"]), sizes[index])
            for index, body in enumerate(bodies)
        ]
        fit_local_ledgers(self.report)
        self.validate()
        self.report["meters"].append(ledger(len(self.report["meters"]), "services", None, sizes[2]))
        with self.assertRaisesRegex(audit.InvalidEvidence, "graph FIR size differs"):
            self.validate()
        self.report["meters"].pop()
        self.report["meters"][1]["fir_size"] = sizes[2]
        self.report["meters"][1]["counts"][0]["bound"] = W_BASE + W_SCALE * sizes[2]
        # The producer ledgers of that body follow its service ledger.
        for meter in self.report["meters"]:
            if meter["name"] in PRODUCERS and meter["owner"] == bodies[0]["key"]:
                meter["fir_size"] = sizes[2]
                meter["counts"][0]["bound"] = W_BASE + W_SCALE * sizes[2]
        with self.assertRaisesRegex(audit.InvalidEvidence, "graph FIR size differs"):
            self.validate()
        self.report["meters"] = self.report["meters"][1:]
        for index, meter in enumerate(self.report["meters"]):
            meter["id"] = index
        self.validate()

    def test_local_ledgers_follow_the_service_ledger_of_their_body(self):
        self.check_flow_ledgers("local")

    def test_stored_ledgers_follow_the_service_ledger_of_their_body(self):
        self.check_flow_ledgers("stored_borrows")

    def check_flow_ledgers(self, flow):
        """The ledger rules of one flow analysis, with complete ledgers of the other producers."""
        stage = audit.ANALYSES.index(flow)
        label = audit.LEDGER_LABELS[flow]
        others = [name for name in PRODUCERS if name != flow]
        classification = [
            name for name, owner in audit.CLASSIFICATION_LEDGERS.items() if owner == flow
        ][0]
        sizes = [3, 5]
        bodies = self.report["bodies"][:2]
        for index in (1, stage):
            self.report["analyses"][index]["producer"] = "integrated"
        for body in self.report["bodies"]:
            body["analyses"][1].update(correspondence="complete", solver="incomplete")
            body["analyses"][stage].update(correspondence="complete", solver="incomplete")
        self.report["totals"] = audit.derived_totals(self.report["bodies"])

        def ledger(index, name, owner, size, refusal=None):
            bound = W_BASE + W_SCALE * size
            used = bound if refusal else 0
            return {
                "id": index,
                "name": name,
                "owner": owner,
                "fir_size": size,
                "counts": [{"category": "W", "scope": "computation", "used": used, "bound": bound}],
                "first_refusal": refusal,
            }

        def build(flow_sizes, owners=None, classified=()):
            owners = owners or [dict(body["key"]) for body in bodies]
            meters = [ledger(0, "graph_private", None, sum(sizes))]
            for index, body in enumerate(bodies):
                meters.append(ledger(len(meters), "services", dict(body["key"]), sizes[index]))
                for other in others:
                    meters.append(ledger(len(meters), other, dict(body["key"]), sizes[index]))
                if index < len(flow_sizes):
                    meters.append(ledger(len(meters), flow, owners[index], flow_sizes[index]))
                if index < len(classified):
                    meters.append(
                        ledger(len(meters), classification, owners[index], classified[index])
                    )
            self.report["meters"] = meters

        def meter_index(name, owner):
            for index, meter in enumerate(self.report["meters"]):
                if meter["name"] == name and meter["owner"] == owner:
                    return index
            raise AssertionError(name)

        # The flow ledgers repeat the FIR size of their body. The graph sums services only.
        build(sizes)
        self.validate()
        # Each verified body has one ledger of each flow analysis, and no other body one.
        build(sizes[:1])
        with self.assertRaisesRegex(audit.InvalidEvidence, f"exactly one {label} ledger"):
            self.validate()
        # The report integrates the flow producer, even where no body proves anything.
        report = make_report(self.argv, self.checkout)
        self.validate(report)
        report["analyses"][stage]["producer"] = "unavailable"
        with self.assertRaisesRegex(
            audit.InvalidEvidence, f"{label} analysis: producer is not integrated"
        ):
            self.validate(report)
        build([sizes[0], sizes[0]])
        with self.assertRaisesRegex(audit.InvalidEvidence, "differs from the service ledger"):
            self.validate()
        build(sizes, owners=[None, dict(bodies[1]["key"])])
        with self.assertRaisesRegex(audit.InvalidEvidence, f"{label} ledger: missing owner"):
            self.validate()
        build(sizes, owners=[dict(bodies[1]["key"]), dict(bodies[1]["key"])])
        with self.assertRaisesRegex(audit.InvalidEvidence, f"{label} ledger: duplicate owner"):
            self.validate()
        # A classification ledger repeats the FIR size of its flow ledger, once for each body.
        build(sizes, classified=sizes)
        self.validate()
        build(sizes, classified=[sizes[1]])
        with self.assertRaisesRegex(audit.InvalidEvidence, "classification ledger: FIR size"):
            self.validate()
        build(sizes, classified=sizes)
        self.report["meters"][meter_index(classification, bodies[1]["key"])]["owner"] = dict(
            bodies[0]["key"]
        )
        with self.assertRaisesRegex(audit.InvalidEvidence, "classification ledger: duplicate"):
            self.validate()
        self.report["meters"][meter_index(classification, bodies[0]["key"])]["owner"] = None
        with self.assertRaisesRegex(audit.InvalidEvidence, "classification ledger: missing"):
            self.validate()
        # A flow ledger of a body without a service ledger has no FIR size to match.
        build(sizes)
        self.report["meters"] = [
            meter
            for meter in self.report["meters"]
            if not (meter["name"] == "services" and meter["owner"] == bodies[1]["key"])
        ]
        self.report["meters"][0]["fir_size"] = sizes[0]
        self.report["meters"][0]["counts"][0]["bound"] = W_BASE + W_SCALE * sizes[0]
        for index, meter in enumerate(self.report["meters"]):
            meter["id"] = index
        with self.assertRaisesRegex(audit.InvalidEvidence, "differs from the service ledger"):
            self.validate()
        # A W refusal needs an incomplete closure and body row of its analysis.
        bound = W_BASE + W_SCALE * sizes[0]
        refusal = incomplete(
            flow, "work_limit", limit={"category": "W", "used": bound, "bound": bound}
        )
        build(sizes)
        at = meter_index(flow, bodies[0]["key"])
        self.report["meters"][at] = ledger(at, flow, dict(bodies[0]["key"]), sizes[0], refusal)
        self.validate()
        self.reject(lambda value: value["analyses"][stage].update(status="complete"))
        self.reject(lambda value: value["meters"][at].update(name="other"))

    def test_raw_ledgers_follow_the_service_ledger_of_their_body(self):
        sizes = [3, 5]
        bodies = self.report["bodies"][:2]

        def ledger(index, name, owner, size, refusal=None):
            bound = W_BASE + W_SCALE * size
            used = bound if refusal else 0
            return {
                "id": index,
                "name": name,
                "owner": owner,
                "fir_size": size,
                "counts": [{"category": "W", "scope": "computation", "used": used, "bound": bound}],
                "first_refusal": refusal,
            }

        def build(raw_sizes, owners=None):
            owners = owners or [dict(body["key"]) for body in bodies]
            meters = [ledger(0, "graph_private", None, sum(sizes))]
            for index, body in enumerate(bodies):
                meters.append(ledger(len(meters), "services", dict(body["key"]), sizes[index]))
                meters.append(ledger(len(meters), "local", dict(body["key"]), sizes[index]))
                meters.append(
                    ledger(len(meters), "stored_borrows", dict(body["key"]), sizes[index])
                )
                if index < len(raw_sizes):
                    meters.append(ledger(len(meters), "raw", owners[index], raw_sizes[index]))
            self.report["meters"] = meters

        # The raw ledger is the raw computation of its body: the FIR size of that body.
        build(sizes)
        self.validate()
        # Version 4 gives each verified body one raw ledger, and no other body one.
        build(sizes[:1])
        with self.assertRaisesRegex(audit.InvalidEvidence, "exactly one raw ledger"):
            self.validate()
        build([sizes[0], sizes[0]])
        with self.assertRaisesRegex(audit.InvalidEvidence, "raw ledger: FIR size differs"):
            self.validate()
        build(sizes, owners=[None, dict(bodies[1]["key"])])
        with self.assertRaisesRegex(audit.InvalidEvidence, "raw ledger: missing owner"):
            self.validate()
        build(sizes, owners=[dict(bodies[1]["key"]), dict(bodies[1]["key"])])
        with self.assertRaisesRegex(audit.InvalidEvidence, "raw ledger: duplicate owner"):
            self.validate()
        # Version 4 integrates the raw producer, even where no body proves anything.
        report = make_report(self.argv, self.checkout)
        self.validate(report)
        report["analyses"][4]["producer"] = "unavailable"
        with self.assertRaisesRegex(audit.InvalidEvidence, "raw analysis: producer"):
            self.validate(report)
        # A raw W refusal needs an incomplete raw closure and body row.
        bound = W_BASE + W_SCALE * sizes[0]
        refusal = incomplete(
            "raw", "work_limit", limit={"category": "W", "used": bound, "bound": bound}
        )
        build(sizes)
        self.report["analyses"][4].update(producer="integrated", status="incomplete")
        for body in self.report["bodies"]:
            body["analyses"][4].update(correspondence="complete", solver="incomplete")
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.report["meters"][4] = ledger(4, "raw", dict(bodies[0]["key"]), sizes[0], refusal)
        self.validate()
        self.reject(lambda value: value["analyses"][4].update(status="complete"))

        def complete_solver(value):
            value["bodies"][0]["analyses"][4].update(solver="complete")
            value["totals"] = audit.derived_totals(value["bodies"])

        self.reject(complete_solver, "complete affected computation")
        self.reject(lambda value: value["meters"][4].update(name="other"))

    def test_meter_w_bound_scales_with_its_fir_size(self):
        def grow_graph(report, size):
            report["meters"][0]["fir_size"] = size
            report["meters"][0]["counts"][0]["bound"] = W_BASE + W_SCALE * size

        self.assertEqual(audit.work_bound(LIMITS, 0), 65536)
        self.assertEqual(audit.work_bound(LIMITS, 77579), W_BASE + W_SCALE * 77579)
        self.assertEqual(audit.work_bound(LIMITS, audit.U64_MAX), audit.U64_MAX)
        self.assertEqual(
            audit.work_bound(LIMITS, (audit.U64_MAX - W_BASE) // W_SCALE + 1), audit.U64_MAX
        )
        owner = self.report["bodies"][0]["key"]
        self.report["analyses"][1]["producer"] = "integrated"
        self.report["bodies"][0]["analyses"][1].update(
            correspondence="complete", solver="incomplete"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        bound = W_BASE + W_SCALE * 1201
        refusal = incomplete(
            "liveness", "work_limit", limit={"category": "W", "used": bound, "bound": bound}
        )
        self.report["meters"] = [
            {
                "id": 0,
                "name": "graph_private",
                "owner": None,
                "fir_size": 1201,
                "counts": [
                    {
                        "category": "W",
                        "scope": "computation",
                        "used": 9000,
                        "bound": W_BASE + W_SCALE * 1201,
                    }
                ],
                "first_refusal": None,
            },
            {
                "id": 1,
                "name": "services",
                "owner": dict(owner),
                "fir_size": 1201,
                "counts": [
                    {"category": "W", "scope": "computation", "used": bound, "bound": bound},
                    {"category": "R", "scope": "state", "used": 0, "bound": 256},
                ],
                "first_refusal": refusal,
            },
        ]
        fit_local_ledgers(self.report)
        self.validate()
        for mutation, pattern in (
            (lambda r: r["meters"][0].update(fir_size=1200), "meter: bound mismatch"),
            (
                lambda r: r["meters"][1]["counts"][1].update(bound=256 + W_SCALE * 1201),
                "bound mismatch",
            ),
            (
                lambda r: r["meters"][1]["first_refusal"]["limit"].update(bound=65536),
                "differs from its ledger",
            ),
            (lambda r: r["meters"][1].update(fir_size=-1), "meter FIR size"),
            (lambda r: grow_graph(r, 1202), "graph FIR size differs"),
            (
                lambda r: r["meters"].append(dict(r["meters"][0], id=len(r["meters"]))),
                "two graph ledgers",
            ),
            (lambda r: r["meters"][1].update(fir_size=True), "meter FIR size"),
            (lambda r: r["meters"][1].pop("fir_size"), "incorrect members"),
            (lambda r: r["limits"].pop("w_scale"), "incorrect members"),
            (lambda r: r["limits"].update(w_scale=0), "W scale"),
            (lambda r: r["limits"].update(w_scale=W_SCALE + 1), "differs from production table"),
        ):
            self.reject(mutation, pattern)

    def test_empty_body_array_does_not_accept_missing_producers(self):
        self.report["bodies"] = []
        self.report["enumeration"]["selected_bodies"] = 0
        self.report["totals"] = audit.derived_totals([])
        fit_local_ledgers(self.report)
        self.assertEqual(self.validate()[2], 0)
        self.report.update(exit_status=0, verdict="accepted")
        with self.assertRaisesRegex(audit.InvalidEvidence, "incomplete ownership proof"):
            self.validate(exit_status=0)

    def test_empty_complete_closure_can_accept(self):
        report = make_report(self.argv, self.checkout, accepted=True)
        report["bodies"] = []
        report["enumeration"]["selected_bodies"] = 0
        report["totals"] = audit.derived_totals([])
        fit_local_ledgers(report)
        self.assertEqual(self.validate(report, exit_status=0)[2], 0)

    def test_count_overflow_fails_without_wrap(self):
        with self.assertRaises(audit.InvalidEvidence):
            audit.checked_sum((audit.U64_MAX, 1), "count")
        self.assertEqual(audit.checked_sum((audit.U64_MAX, 0), "count"), audit.U64_MAX)

    def test_duplicate_json_members_and_truncation(self):
        for contents in (
            '{"kind":1,"kind":2}',
            '{"nested":{"x":1,"x":2}}',
            '{"kind":',
            '{"x":1} trailing',
            "\xff",
        ):
            with self.subTest(contents=contents):
                self.report_path.write_bytes(contents.encode("latin1"))
                with self.assertRaises(audit.InvalidEvidence):
                    audit.read_json(self.report_path)

    def test_atomic_publication_keeps_prior_destination_after_failure(self):
        self.report_path.write_text("prior bytes")
        with patch.object(audit.os, "replace", side_effect=OSError("replace failed")):
            with self.assertRaises(OSError):
                audit.atomic_json(self.report_path, {"valid": True})
        self.assertEqual(self.report_path.read_text(), "prior bytes")
        self.assertEqual(list(self.base.glob(".report.json.*")), [])

    def test_source_file_aliases_do_not_add_unique_files(self):
        alias = self.checkout / "src/fort/alias.ft"
        alias.symlink_to("main.ft")
        self.report["files"].append({"id": 2, "path": str(alias), "module": "main"})
        with self.assertRaisesRegex(audit.InvalidEvidence, "duplicate normalized identity"):
            self.validate()

    def test_snapshot_follows_module_directory_links_and_stops_cycles(self):
        external = self.base / "external"
        external.mkdir()
        (external / "module.ft").write_text("fn external_body() void {}\n")
        (self.checkout / "src/fort/external").symlink_to(external, target_is_directory=True)
        (external / "cycle").symlink_to(self.checkout / "src", target_is_directory=True)
        inputs = audit.snapshot([self.checkout / "src"])
        self.assertIn(str(external / "module.ft"), inputs)
        self.assertEqual(len(inputs), 5)

    def test_invalid_unicode_cannot_crash_validation(self):
        self.reject(
            lambda value: value["bodies"][0]["source"].update(name="\ud800"), "invalid Unicode"
        )

    def test_zero_width_reason_range_is_valid(self):
        source = {"file": 0, "line": 1, "col": 1, "end_line": 1, "end_col": 1}
        self.report["first_incomplete"]["source"] = source
        self.validate()

    def test_first_refusal_uses_actual_category_bound(self):
        self.report["first_incomplete"]["limit"] = {"category": "W", "used": 0, "bound": 65536}
        self.validate()
        self.reject(
            lambda value: value["first_incomplete"]["limit"].update(bound=65537),
            "W bound of no FIR size",
        )
        self.reject(
            lambda value: value["first_incomplete"]["limit"].update(bound=65535),
            "W bound of no FIR size",
        )
        self.report["first_incomplete"]["limit"]["bound"] = W_BASE + W_SCALE * 3
        self.validate()
        self.report["first_incomplete"]["limit"]["bound"] = audit.U64_MAX
        self.validate()
        self.report["first_incomplete"]["limit"] = {"category": "R", "used": 0, "bound": 256}
        self.validate()
        self.reject(
            lambda value: value["first_incomplete"]["limit"].update(bound=320), "bound mismatch"
        )
        self.reject(
            lambda value: value["first_incomplete"]["limit"].update(used=65537), "exceeds bound"
        )

    def test_source_identity_excludes_context_module_names_and_keys(self):
        inputs, identities, _ = self.validate()
        self.report["files"][0]["module"] = "context_main"
        self.report["bodies"][0]["source"]["module"] = "context_main"
        self.report["bodies"][0]["key"]["declaration"] = 23
        fit_local_ledgers(self.report)
        after_inputs, after_identities, _ = self.validate()
        self.assertEqual(inputs, after_inputs)
        self.assertEqual(identities, after_identities)


class SourceEvidenceTests(RepositoryFixture):
    def test_complete_enumeration_requires_runtime_for_both_verdicts(self):
        for accepted in (False, True):
            with self.subTest(accepted=accepted):
                report = make_report(
                    self.argv,
                    self.checkout,
                    accepted=accepted,
                    files=[str(self.checkout / "src/fort/main.ft")],
                )
                with self.assertRaisesRegex(audit.InvalidEvidence, "missing selected runtime"):
                    self.validate(report, exit_status=report["exit_status"])

    def test_incomplete_enumeration_retains_rows_without_runtime(self):
        report = make_report(
            self.argv, self.checkout, files=[str(self.checkout / "src/fort/main.ft")]
        )
        report["enumeration"]["complete"] = False
        report["first_incomplete"] = incomplete("enumeration", "source_coverage")
        with self.assertRaises(audit.FailedReportAttempt) as error:
            self.validate(report)
        self.assertEqual(error.exception.bodies, 1)
        self.assertEqual(len(error.exception.identities), 1)
        self.assertEqual([row["path"] for row in error.exception.inputs], ["src/fort/main.ft"])

    def test_runtime_identity_alone_does_not_select_runtime(self):
        other = self.write("std/rt.ft", "fn other_runtime() void {}\n")
        report = make_report(
            self.argv,
            self.checkout,
            accepted=True,
            files=[str(self.checkout / "src/fort/main.ft"), str(other)],
        )
        self.assertEqual(
            audit.normalize(str(other), self.checkout, self.checkout, self.standard), "std/rt.ft"
        )
        with self.assertRaisesRegex(audit.InvalidEvidence, "missing selected runtime"):
            self.validate(
                report,
                exit_status=0,
                candidates=audit.snapshot([self.checkout / "src", other.parent, self.standard]),
            )

    def test_runtime_uses_selected_standard_root(self):
        selected = self.checkout / "std"
        self.write("std/rt.ft", "fn selected_runtime() void {}\n")
        self.argv[self.argv.index("--std-dir") + 1] = str(selected)
        report = make_report(self.argv, self.checkout, accepted=True)
        inputs, identities, count = self.validate(
            report,
            exit_status=0,
            standard=selected,
            candidates=audit.snapshot([self.checkout / "src", selected]),
        )
        self.assertEqual(inputs[1]["path"], "std/rt.ft")
        self.assertEqual(report["bodies"][1]["source"]["name"], "selected_runtime")
        self.assertEqual((len(identities), count), (2, 2))

    def test_runtime_symlink_keeps_resolved_identity(self):
        external = self.base / "external_runtime.ft"
        external.write_text("fn external_runtime() void {}\n")
        (self.standard / "rt.ft").unlink()
        (self.standard / "rt.ft").symlink_to(external)
        report = make_report(self.argv, self.checkout, accepted=True)
        inputs, identities, count = self.validate(report, exit_status=0)
        self.assertEqual(inputs[1]["path"], str(external))
        self.assertEqual((len(identities), count), (2, 2))

    def test_runtime_without_body_does_not_create_lexical_denominator(self):
        (self.standard / "rt.ft").write_text("extern fn runtime_entry() void;\n")
        report = make_report(self.argv, self.checkout, accepted=True)
        inputs, identities, count = self.validate(report, exit_status=0)
        self.assertEqual([row["path"] for row in inputs], ["src/fort/main.ft", "std/rt.ft"])
        self.assertEqual((len(identities), count), (1, 1))

    def test_name_end_at_line_boundary_and_eof_is_valid(self):
        for source in (b"fn main", b"fn main\n() void {}\n"):
            with self.subTest(source=source):
                (self.checkout / "src/fort/main.ft").write_bytes(source)
                report = make_report(self.argv, self.checkout, accepted=True)
                self.assertEqual(report["bodies"][0]["source"]["end_col"], 8)
                self.assertEqual(self.validate(report, exit_status=0)[2], 2)

    def test_overlong_name_ends_cannot_create_distinct_source_identities(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"fn main\n() void {}\n")
        report = make_report(self.argv, self.checkout, accepted=True)
        other = copy.deepcopy(report["bodies"][0])
        report["bodies"][0]["source"]["end_col"] = 999
        other["source"]["end_col"] = 1000
        other["key"]["declaration"] = 1
        report["bodies"].insert(1, other)
        report["enumeration"]["selected_bodies"] = 3
        report["totals"] = audit.derived_totals(report["bodies"])
        with self.assertRaisesRegex(audit.InvalidEvidence, "source columns outside input"):
            self.validate(report, exit_status=0)

    def test_name_columns_must_be_inside_byte_line(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"fn main")
        for col, end_col in ((4, 9), (4, audit.U64_MAX), (8, 8), (8, 9), (9, 10)):
            with self.subTest(col=col, end_col=end_col):
                report = make_report(self.argv, self.checkout, accepted=True)
                report["bodies"][0]["source"].update(col=col, end_col=end_col)
                with self.assertRaisesRegex(audit.InvalidEvidence, "source columns outside input"):
                    self.validate(report, exit_status=0)

    def test_lone_cr_increments_column_without_incrementing_line(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"\rfn main() void {}")
        report = make_report(self.argv, self.checkout, accepted=True)
        source = report["bodies"][0]["source"]
        self.assertEqual(
            (source["line"], source["col"], source["end_line"], source["end_col"]), (1, 5, 1, 9)
        )
        self.assertEqual(self.validate(report, exit_status=0)[2], 2)
        source.update(line=2, end_line=2, col=4, end_col=8)
        with self.assertRaisesRegex(audit.InvalidEvidence, "source line outside input"):
            self.validate(report, exit_status=0)

    def test_cr_between_declarations_stays_on_same_line(self):
        (self.checkout / "src/fort/main.ft").write_bytes(
            b"extern fn helper() void;\rfn main() void {}\n"
        )
        self.report["bodies"][0]["source"].update(col=29, end_col=33)
        self.assertEqual(self.validate()[2], 2)
        self.reject(
            lambda value: value["bodies"][0]["source"].update(line=2, end_line=2, col=4, end_col=8),
            "source columns outside input",
        )

    def test_crlf_and_tab_use_lf_lines_and_byte_columns(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"// first\r\n\r\tfn main() void {}\r\n")
        report = make_report(self.argv, self.checkout, accepted=True)
        source = report["bodies"][0]["source"]
        self.assertEqual(
            (source["line"], source["col"], source["end_line"], source["end_col"]), (2, 6, 2, 10)
        )
        self.assertEqual(self.validate(report, exit_status=0)[2], 2)
        source.update(col=5, end_col=9)
        with self.assertRaisesRegex(audit.InvalidEvidence, "source name range mismatch"):
            self.validate(report, exit_status=0)

    def test_only_lf_advances_to_an_empty_final_line(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"\nfn main() void {}\n")
        report = make_report(self.argv, self.checkout, accepted=True)
        self.assertEqual(report["bodies"][0]["source"]["line"], 2)
        self.assertEqual(self.validate(report, exit_status=0)[2], 2)
        report["bodies"][0]["source"].update(line=3, end_line=3)
        with self.assertRaisesRegex(audit.InvalidEvidence, "source columns outside input"):
            self.validate(report, exit_status=0)


class ReasonRangeTests(RepositoryFixture):
    CONTAINERS = ("report", "body", "meter", "failure")

    def report_for_container(self, container, source):
        report = copy.deepcopy(self.report)
        if container == "report":
            report["first_incomplete"]["source"] = source
        elif container == "body":
            report["bodies"][0]["first_incomplete"]["source"] = source
        elif container == "meter":
            report["meters"] = [
                {
                    "id": 0,
                    "name": "services",
                    "owner": None,
                    "fir_size": 0,
                    "counts": [],
                    "first_refusal": incomplete("graph", "missing_correspondence", source),
                }
            ]
            fit_local_ledgers(report)
        elif container == "failure":
            report.update(
                failure=incomplete("tool", "tool_failure", source), exit_status=2, verdict="failed"
            )
        else:
            raise AssertionError(container)
        return report

    def check_ranges(self, ranges, *, pattern=None):
        count = 0
        for container in self.CONTAINERS:
            for source in ranges:
                with self.subTest(container=container, source=source):
                    report = self.report_for_container(container, source)
                    if pattern is not None:
                        with self.assertRaisesRegex(audit.InvalidEvidence, pattern):
                            self.validate(report, exit_status=report["exit_status"])
                    elif report["exit_status"] == 2:
                        with self.assertRaises(audit.FailedReportAttempt) as error:
                            self.validate(report, exit_status=2)
                        self.assertEqual(error.exception.bodies, len(report["bodies"]))
                    else:
                        self.assertEqual(self.validate(report)[2], len(report["bodies"]))
                count += 1
        self.assertEqual(count, 4 * len(ranges))

    @staticmethod
    def position(line, col, end_line=None, end_col=None, file=0):
        return {
            "file": file,
            "line": line,
            "col": col,
            "end_line": line if end_line is None else end_line,
            "end_col": col if end_col is None else end_col,
        }

    def test_four_reason_containers_reject_nonexistent_start_lines(self):
        self.check_ranges(
            [self.position(3, 1), self.position(999, 1)], pattern="source line outside input"
        )

    def test_four_reason_containers_reject_nonexistent_end_lines(self):
        self.check_ranges(
            [self.position(1, 1, 3, 1), self.position(1, 1, 999, 1)],
            pattern="source line outside input",
        )

    def test_four_reason_containers_reject_start_columns_past_byte_line(self):
        self.check_ranges(
            [self.position(1, 19), self.position(1, 999), self.position(2, 2)],
            pattern="source columns outside input",
        )

    def test_four_reason_containers_reject_end_columns_past_byte_line(self):
        self.check_ranges(
            [self.position(1, 1, 1, 19), self.position(1, 1, 1, 999), self.position(1, 1, 2, 2)],
            pattern="source columns outside input",
        )

    def test_four_reason_containers_reject_reversed_ranges(self):
        self.check_ranges(
            [self.position(2, 1, 1, 1), self.position(1, 8, 1, 4)], pattern="source: range"
        )

    def test_four_reason_containers_accept_zero_width_eof_positions(self):
        for source, line, col in (
            (b"", 1, 1),
            (b"\n", 2, 1),
            (b"fn main() void {}", 1, 18),
            (b"fn main() void {}\n", 2, 1),
        ):
            with self.subTest(source=source):
                (self.checkout / "src/fort/main.ft").write_bytes(source)
                self.report = make_report(self.argv, self.checkout)
                self.check_ranges([self.position(line, col)])

    def test_four_reason_containers_accept_ordered_cross_line_ranges(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"fn main() void {}\n// next\n")
        self.report = make_report(self.argv, self.checkout)
        self.check_ranges(
            [self.position(1, 4, 2, 8), self.position(1, 18, 3, 1), self.position(3, 1)]
        )

    def test_four_reason_containers_use_lone_cr_and_tab_byte_columns(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"\r\tfn main() void {}\r")
        self.report = make_report(self.argv, self.checkout)
        self.check_ranges(
            [self.position(1, 1, 1, 3), self.position(1, 6, 1, 10), self.position(1, 21)]
        )
        self.check_ranges([self.position(2, 1)], pattern="source line outside input")
        self.check_ranges([self.position(1, 22)], pattern="source columns outside input")

    def test_four_reason_containers_use_multibyte_byte_columns(self):
        (self.checkout / "src/fort/main.ft").write_bytes(b"fn main() void {}\r\xc3\xa9")
        self.report = make_report(self.argv, self.checkout)
        self.check_ranges([self.position(1, 19, 1, 21), self.position(1, 20), self.position(1, 21)])
        self.check_ranges([self.position(1, 22)], pattern="source columns outside input")

    def test_four_reason_containers_use_referenced_file_bytes(self):
        (self.standard / "rt.ft").write_bytes(b"fn rt() void {}")
        self.report = make_report(self.argv, self.checkout)
        self.check_ranges([self.position(1, 16, file=1)])
        self.check_ranges([self.position(1, 17, file=1)], pattern="source columns outside input")
        self.check_ranges([self.position(2, 1, file=1)], pattern="source line outside input")

    def test_meter_reason_with_invalid_range_cannot_supply_exit_zero(self):
        report = make_report(self.argv, self.checkout, accepted=True)
        report["meters"] = [
            {
                "id": 0,
                "name": "services",
                "owner": None,
                "fir_size": 0,
                "counts": [],
                "first_refusal": incomplete(
                    "graph", "missing_correspondence", self.position(999, 1)
                ),
            }
        ]
        fit_local_ledgers(report)
        with self.assertRaisesRegex(audit.InvalidEvidence, "source line outside input"):
            self.validate(report, exit_status=0)
        report["meters"][0]["first_refusal"]["source"] = self.position(2, 1)
        self.assertEqual(self.validate(report, exit_status=0)[2], 2)


class StageTests(RepositoryFixture):
    def block(self, stage):
        body = self.report["bodies"][0]
        body[stage] = "failed"
        position = audit.STAGES.index(stage)
        for later in audit.STAGES[position + 1 :]:
            body[later] = "unexecuted"
        for row in body["analyses"]:
            row.update(correspondence="unexecuted", solver="unexecuted", proof="unexecuted")
        body.update(ownership="failed", first_incomplete=None)
        codes = {
            "checking": "source_error",
            "lowering": "unsupported_lowering",
            "verification": "verification_failure",
        }
        self.report["failure"] = incomplete(stage, codes[stage])
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        fit_local_ledgers(self.report)

    def test_failed_checking_keeps_unexecuted_rows(self):
        self.block("checking")
        self.report["bodies"][0]["key"] = None
        fit_local_ledgers(self.report)
        self.assertEqual(self.validate()[2], 2)
        self.reject(lambda value: value["bodies"][0].update(lowering="complete"))
        self.reject(lambda value: value["bodies"][0]["analyses"][0].update(proof="incomplete"))

    def test_unsupported_lowering_can_keep_valid_exit_one_evidence(self):
        self.block("lowering")
        self.assertEqual(self.validate()[2], 2)
        self.reject(lambda value: value["bodies"][0].update(verification="complete"))

        # A body that verification never reached has no local ledger.
        def unverified_ledgers(report):
            key = report["bodies"][0]["key"]
            for name in ("services", "local"):
                report["meters"].append(
                    body_ledger(len(report["meters"]), name, key, 0, report["limits"])
                )

        self.reject(unverified_ledgers, "exactly one local ledger")

        # A body that verification never reached has no raw ledger either.
        def unverified_raw(report):
            key = report["bodies"][0]["key"]
            for name in ("services", "raw"):
                report["meters"].append(
                    body_ledger(len(report["meters"]), name, key, 0, report["limits"])
                )

        self.reject(unverified_raw, "exactly one raw ledger")

        # A body that verification never reached has no stored ledger either.
        def unverified_stored(report):
            key = report["bodies"][0]["key"]
            for name in ("services", "stored_borrows"):
                report["meters"].append(
                    body_ledger(len(report["meters"]), name, key, 0, report["limits"])
                )

        self.reject(unverified_stored, "exactly one stored ledger")

    def test_verifier_failure_requires_exit_two_and_fails_audit(self):
        self.block("verification")
        self.reject(lambda value: value, "failure")
        self.report.update(exit_status=2, verdict="failed")
        with self.assertRaisesRegex(audit.InvalidEvidence, "compiler exit 2"):
            self.validate(exit_status=2)

    def test_solver_completion_is_separate_from_correspondence(self):
        self.report["analyses"][0].update(producer="integrated")
        self.report["bodies"][0]["analyses"][0].update(
            correspondence="incomplete", solver="complete"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.validate()
        self.reject(
            lambda value: value["bodies"][0]["analyses"][0].update(correspondence="unexecuted")
        )

    def test_independent_facts_can_prove_obligations_after_precision_loss(self):
        report = make_report(self.argv, self.checkout, accepted=True)
        report["bodies"][0]["analyses"][0]["correspondence"] = "incomplete"
        report["totals"] = audit.derived_totals(report["bodies"])
        self.validate(report, exit_status=0)

    def test_violations_require_validated_proof_status_and_exact_sums(self):
        self.report["analyses"][0].update(producer="integrated", status="complete")
        body = self.report["bodies"][0]
        body["analyses"][0].update(
            correspondence="complete", solver="complete", proof="violated", violations=1
        )
        body.update(ownership="violated", violations=1)
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.validate()
        self.assertIsNotNone(body["first_incomplete"])
        self.reject(lambda value: value["bodies"][0].update(ownership="incomplete"))
        self.reject(lambda value: value["bodies"][0].update(violations=0))
        self.reject(lambda value: value["bodies"][0]["analyses"][0].update(violations=0))
        self.reject(lambda value: value["bodies"][0]["analyses"][0].update(proof="incomplete"))

    def test_unexecuted_stage_supplies_no_complete_or_violated_proof(self):
        for proof in ("complete", "violated"):
            for field in ("correspondence", "solver"):
                with self.subTest(proof=proof, field=field):
                    report = make_report(self.argv, self.checkout, accepted=True)
                    body = report["bodies"][0]
                    row = body["analyses"][0]
                    row[field] = "unexecuted"
                    row["proof"] = proof
                    row["violations"] = 1 if proof == "violated" else 0
                    body.update(ownership=proof, violations=row["violations"])
                    report["totals"] = audit.derived_totals(report["bodies"])
                    report.update(
                        verdict="rejected" if proof == "violated" else "accepted",
                        exit_status=1 if proof == "violated" else 0,
                    )
                    with self.assertRaisesRegex(
                        audit.InvalidEvidence, "unexecuted stage supplies proof"
                    ):
                        self.validate(report, exit_status=report["exit_status"])

    def test_failed_solver_cannot_hide_failure(self):
        self.report["analyses"][0].update(producer="integrated")
        self.reject(
            lambda value: value["bodies"][0]["analyses"][0].update(solver="failed"),
            "failure hidden",
        )

    def test_rejected_exit_requires_rejection_evidence(self):
        report = make_report(self.argv, self.checkout, accepted=True)
        report.update(exit_status=1, verdict="rejected")
        with self.assertRaisesRegex(audit.InvalidEvidence, "no rejection evidence"):
            self.validate(report)

    def test_abnormal_exit_never_passes(self):
        for status in (-9, 3, 127):
            with self.subTest(status=status):
                with self.assertRaises(audit.InvalidEvidence):
                    self.validate(exit_status=status)


class BudgetTests(RepositoryFixture):
    def setUp(self):
        super().setUp()
        self.report = make_report(self.argv, self.checkout, accepted=True)

    def meter(self, category="W", stage="graph", owner=None, code=None, used=None):
        bound = LIMITS[category.lower()]
        refusal = incomplete(
            stage,
            code or ("report_limit" if category == "V" else "work_limit"),
            limit={"category": category, "used": bound if used is None else used, "bound": bound},
        )
        self.report["meters"] = [
            {
                "id": 0,
                "name": "services",
                "owner": copy.deepcopy(owner),
                "fir_size": 0,
                "counts": [
                    {
                        "category": category,
                        "scope": audit.CATEGORY_SCOPES[category],
                        "used": refusal["limit"]["used"],
                        "bound": bound,
                    }
                ],
                "first_refusal": refusal,
            }
        ]
        fit_local_ledgers(self.report)
        return refusal

    def work_refusal(self):
        body = self.report["bodies"][0]
        refusal = self.meter(owner=body["key"])
        body["analyses"][0].update(solver="incomplete", proof="incomplete")
        body.update(ownership="incomplete", first_incomplete=copy.deepcopy(refusal))
        self.report["analyses"][0]["status"] = "incomplete"
        self.report.update(
            first_incomplete=copy.deepcopy(refusal), exit_status=1, verdict="rejected"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])

    def test_recorded_work_refusal_rejects_acceptance_below_or_at_bound(self):
        for ledger in ("services", "graph_private"):
            for used, code in ((0, "work_limit"), (65536, "work_limit"), (0, "missing_budget")):
                with self.subTest(ledger=ledger, used=used, code=code):
                    self.meter(used=used, code=code)
                    self.report["meters"][0]["name"] = ledger
                    with self.assertRaisesRegex(audit.InvalidEvidence, "budget refusal: accepted"):
                        self.validate(exit_status=0)

    def test_work_reason_without_numeric_limit_still_rejects_acceptance(self):
        for location in ("report", "body", "meter"):
            with self.subTest(location=location):
                self.report = make_report(self.argv, self.checkout, accepted=True)
                refusal = incomplete("graph", "work_limit")
                if location == "report":
                    self.report["first_incomplete"] = refusal
                elif location == "body":
                    self.report["bodies"][0]["first_incomplete"] = refusal
                    self.report["first_incomplete"] = copy.deepcopy(refusal)
                else:
                    self.meter()
                    self.report["meters"][0]["first_refusal"] = refusal
                with self.assertRaisesRegex(audit.InvalidEvidence, "budget refusal: accepted"):
                    self.validate(exit_status=0)

    def test_full_meters_without_refusal_can_accept(self):
        self.report["meters"] = [
            {
                "id": 0,
                "name": "services",
                "owner": None,
                "fir_size": 0,
                "counts": [
                    {
                        "category": category,
                        "scope": audit.CATEGORY_SCOPES[category],
                        "used": LIMITS[category.lower()],
                        "bound": LIMITS[category.lower()],
                    }
                    for category in audit.CATEGORIES
                ],
                "first_refusal": None,
            }
        ]
        fit_local_ledgers(self.report)
        self.assertEqual(self.validate(exit_status=0)[2], 2)

    def test_precision_refusal_can_retain_independent_complete_proof(self):
        for category in "DRPGHTE":
            with self.subTest(category=category):
                self.meter(
                    category, owner=self.report["bodies"][0]["key"], code="missing_correspondence"
                )
                self.report["bodies"][0]["analyses"][0]["correspondence"] = "incomplete"
                self.report["totals"] = audit.derived_totals(self.report["bodies"])
                self.assertEqual(self.validate(exit_status=0)[2], 2)

    def test_owned_work_refusal_keeps_affected_output_and_prior_body_proof(self):
        self.work_refusal()
        owner = self.report["meters"][0]["owner"]
        self.report["meters"][0]["owner"] = {name: owner[name] for name in reversed(owner)}
        self.assertEqual(self.validate()[2], 2)
        self.assertEqual(self.report["bodies"][1]["ownership"], "complete")
        self.assertTrue(
            all(row["solver"] == "complete" for row in self.report["bodies"][1]["analyses"])
        )
        self.reject(lambda value: value["analyses"][0].update(status="complete"), "closure outcome")
        self.reject(
            lambda value: value["analyses"][0].update(status="unexecuted"), "closure outcome"
        )
        self.reject(lambda value: value["bodies"][0].update(first_incomplete=None), "body.*reason")
        self.reject(lambda value: value.update(first_incomplete=None), "report.*reason")

    def test_work_refusal_cannot_hide_complete_computation(self):
        self.work_refusal()
        self.report["bodies"][0]["analyses"][0]["solver"] = "complete"
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        with self.assertRaisesRegex(audit.InvalidEvidence, "complete affected computation"):
            self.validate()

    def test_work_refusal_cannot_use_independent_facts_for_complete_affected_proof(self):
        self.work_refusal()
        self.report["bodies"][0]["analyses"][0]["proof"] = "complete"
        self.report["bodies"][0]["ownership"] = "complete"
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        with self.assertRaisesRegex(audit.InvalidEvidence, "complete or absent affected proof"):
            self.validate()

    def test_pre_solver_work_refusal_retains_unexecuted_work(self):
        self.work_refusal()
        self.report["bodies"][0]["analyses"][0].update(
            correspondence="unexecuted", solver="unexecuted"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.assertEqual(self.validate()[2], 2)
        self.report = make_report(self.argv, self.checkout)
        refusal = self.meter(owner=self.report["bodies"][0]["key"])
        self.report["bodies"][0]["first_incomplete"] = copy.deepcopy(refusal)
        self.report["first_incomplete"] = copy.deepcopy(refusal)
        self.assertEqual(self.validate()[2], 2)

    def test_solver_can_finish_after_incomplete_work_correspondence(self):
        self.work_refusal()
        self.report["bodies"][0]["analyses"][0].update(
            correspondence="incomplete", solver="complete"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.assertEqual(self.validate()[2], 2)

    def test_closure_work_refusal_preserves_complete_body_proof(self):
        refusal = self.meter(owner=None)
        self.report["analyses"][0]["status"] = "incomplete"
        self.report.update(
            first_incomplete=copy.deepcopy(refusal), exit_status=1, verdict="rejected"
        )
        self.assertEqual(self.validate()[2], 2)
        self.assertTrue(all(body["ownership"] == "complete" for body in self.report["bodies"]))

    def test_work_refusal_retains_validated_violation_and_incompleteness(self):
        self.work_refusal()
        body = self.report["bodies"][0]
        body["analyses"][0].update(proof="violated", violations=1)
        body.update(ownership="violated", violations=1)
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        self.assertEqual(self.validate()[2], 2)
        self.assertEqual(self.report["totals"]["violations"], 1)
        self.assertIsNotNone(body["first_incomplete"])

    def test_internal_work_failure_retains_rows_but_fails_audit(self):
        self.work_refusal()
        body = self.report["bodies"][0]
        body["analyses"][0].update(solver="failed", proof="failed")
        body["ownership"] = "failed"
        self.report["analyses"][0]["status"] = "failed"
        self.report.update(
            failure=incomplete("graph", "analysis_failure"), exit_status=2, verdict="failed"
        )
        self.report["totals"] = audit.derived_totals(self.report["bodies"])
        with self.assertRaises(audit.FailedReportAttempt) as error:
            self.validate(exit_status=2)
        self.assertEqual(error.exception.bodies, 2)

    def test_later_report_refusal_preserves_complete_solver_and_proof(self):
        body = self.report["bodies"][0]
        refusal = self.meter("V", owner=body["key"])
        body["first_incomplete"] = copy.deepcopy(refusal)
        self.report.update(
            first_incomplete=copy.deepcopy(refusal), exit_status=1, verdict="rejected"
        )
        self.assertEqual(self.validate()[2], 2)
        self.assertEqual(body["ownership"], "complete")
        self.assertEqual(body["analyses"][0]["solver"], "complete")
        self.assertEqual(self.report["analyses"][0]["status"], "complete")
        self.reject(lambda value: value["bodies"][0].update(first_incomplete=None), "body reason")
        self.reject(lambda value: value.update(first_incomplete=None), "report.*reason")

    def test_report_refusal_cannot_accept_with_or_without_limit(self):
        for limit in (None, {"category": "V", "used": 512, "bound": 512}):
            with self.subTest(limit=limit):
                self.meter("V")
                self.report["meters"][0]["first_refusal"]["limit"] = limit
                with self.assertRaisesRegex(audit.InvalidEvidence, "budget refusal: accepted"):
                    self.validate(exit_status=0)

    def test_v_charge_work_code_uses_report_refusal_semantics(self):
        body = self.report["bodies"][0]
        refusal = self.meter("V", owner=body["key"], code="work_limit")
        body["first_incomplete"] = copy.deepcopy(refusal)
        self.report.update(
            first_incomplete=copy.deepcopy(refusal), exit_status=1, verdict="rejected"
        )
        self.assertEqual(self.validate()[2], 2)
        self.assertEqual(body["analyses"][0]["proof"], "complete")

    def test_report_storage_failure_requires_exit_two_and_tool_reason(self):
        refusal = self.meter("V", stage="tool")
        self.report.update(
            first_incomplete=copy.deepcopy(refusal), exit_status=1, verdict="rejected"
        )
        with self.assertRaisesRegex(audit.InvalidEvidence, "tool failure requires exit 2"):
            self.validate()
        self.report.update(exit_status=2, verdict="failed")
        with self.assertRaisesRegex(audit.InvalidEvidence, "tool failure requires exit 2"):
            self.validate(exit_status=2)
        self.report["failure"] = incomplete("tool", "tool_failure")
        with self.assertRaises(audit.FailedReportAttempt) as error:
            self.validate(exit_status=2)
        self.assertEqual(error.exception.bodies, 2)

    def test_enumeration_work_refusal_rejects_complete_enumeration(self):
        for container in ("report", "body", "meter", "failure"):
            with self.subTest(container=container):
                self.report = make_report(self.argv, self.checkout, accepted=True)
                self.report.update(
                    first_incomplete=incomplete("graph", "missing_renderer"),
                    exit_status=1,
                    verdict="rejected",
                )
                refusal = incomplete(
                    "enumeration", "work_limit", limit={"category": "W", "used": 0, "bound": 65536}
                )
                if container == "report":
                    self.report["first_incomplete"] = refusal
                elif container == "body":
                    self.report["bodies"][0]["first_incomplete"] = refusal
                elif container == "meter":
                    self.meter(stage="enumeration", used=0)
                else:
                    self.report.update(
                        failure=incomplete("enumeration", "tool_failure", limit=refusal["limit"]),
                        exit_status=2,
                        verdict="failed",
                    )
                with self.assertRaisesRegex(audit.InvalidEvidence, "complete enumeration"):
                    self.validate(exit_status=self.report["exit_status"])
                self.report["enumeration"]["complete"] = False
                with self.assertRaises(audit.FailedReportAttempt) as error:
                    self.validate(exit_status=self.report["exit_status"])
                self.assertEqual(error.exception.bodies, 2)
                self.assertEqual(len(error.exception.identities), 2)
                self.assertEqual(len(error.exception.inputs), 2)

    def test_graph_private_refusal_categories_match_its_w_ledger(self):
        self.report = make_report(self.argv, self.checkout)
        for category in "DRPGHTEV":
            for counts in (
                [],
                [{"category": "W", "scope": "computation", "used": 0, "bound": 65536}],
            ):
                with self.subTest(category=category, counts=counts):
                    self.meter(category, code="work_limit")
                    self.report["meters"][0].update(name="graph_private", counts=counts)
                    with self.assertRaisesRegex(
                        audit.InvalidEvidence, "unsupported refusal category"
                    ):
                        self.validate()
        self.meter("W", used=0)
        self.report["meters"][0]["name"] = "graph_private"
        self.assertEqual(self.validate()[2], 2)

    def test_graph_private_report_limit_requires_no_numeric_limit_to_reject(self):
        self.report = make_report(self.argv, self.checkout)
        for limit in (None, {"category": "W", "used": 0, "bound": 65536}):
            with self.subTest(limit=limit):
                self.meter(code="report_limit")
                self.report["meters"][0]["name"] = "graph_private"
                self.report["meters"][0]["first_refusal"]["limit"] = limit
                with self.assertRaisesRegex(
                    audit.InvalidEvidence, "refusal category|report limit category"
                ):
                    self.validate()
        self.report["meters"][0]["first_refusal"] = None
        self.assertEqual(self.validate()[2], 2)

    def test_recorded_precision_category_overrides_work_limit_code(self):
        for category in "DRPGHTE":
            with self.subTest(category=category):
                self.meter(category, code="work_limit", owner=self.report["bodies"][0]["key"])
                self.report["bodies"][0]["analyses"][0]["correspondence"] = "incomplete"
                self.report["totals"] = audit.derived_totals(self.report["bodies"])
                self.assertEqual(self.validate(exit_status=0)[2], 2)
        self.meter("W", code="work_limit")
        with self.assertRaisesRegex(audit.InvalidEvidence, "budget refusal: accepted"):
            self.validate(exit_status=0)

    def test_service_v_refusal_keeps_complete_proof_with_empty_counts(self):
        for limit in (None, {"category": "V", "used": 0, "bound": 512}):
            with self.subTest(limit=limit):
                refusal = self.meter("V", used=0)
                self.report["meters"][0]["counts"] = []
                refusal["limit"] = limit
                self.report.update(
                    first_incomplete=copy.deepcopy(refusal), exit_status=1, verdict="rejected"
                )
                self.assertEqual(self.validate()[2], 2)
                self.assertTrue(
                    all(body["ownership"] == "complete" for body in self.report["bodies"])
                )

    def test_report_limit_rejects_inconsistent_numeric_categories(self):
        for category in "DRPGHTEW":
            for container in ("report", "body", "meter"):
                with self.subTest(category=category, container=container):
                    self.report = make_report(self.argv, self.checkout, accepted=True)
                    self.report.update(
                        first_incomplete=incomplete("graph", "missing_renderer"),
                        exit_status=1,
                        verdict="rejected",
                    )
                    refusal = incomplete(
                        "graph",
                        "report_limit",
                        limit={"category": category, "used": 0, "bound": LIMITS[category.lower()]},
                    )
                    if container == "report":
                        self.report["first_incomplete"] = refusal
                    elif container == "body":
                        self.report["bodies"][0]["first_incomplete"] = refusal
                    else:
                        self.meter(category, code="report_limit", used=0)
                    with self.assertRaisesRegex(
                        audit.InvalidEvidence, "report limit category mismatch"
                    ):
                        self.validate()

    def test_failure_limit_refusal_cannot_claim_complete_closure(self):
        self.report.update(
            first_incomplete=incomplete("graph", "missing_correspondence"),
            failure=incomplete(
                "graph", "analysis_failure", limit={"category": "W", "used": 0, "bound": 65536}
            ),
            exit_status=2,
            verdict="failed",
        )
        with self.assertRaisesRegex(audit.InvalidEvidence, "complete closure outcome"):
            self.validate(exit_status=2)
        self.report["analyses"][0]["status"] = "failed"
        self.report["first_incomplete"] = None
        with self.assertRaisesRegex(audit.InvalidEvidence, "missing report reason"):
            self.validate(exit_status=2)

    def test_failure_limit_preserves_prior_proofs_and_failed_attempt_rows(self):
        for category, stage, code in (
            ("W", "graph", "analysis_failure"),
            ("V", "tool", "tool_failure"),
        ):
            with self.subTest(category=category):
                self.report = make_report(self.argv, self.checkout, accepted=True)
                self.report.update(
                    first_incomplete=incomplete(stage, "missing_renderer"),
                    failure=incomplete(
                        stage,
                        code,
                        limit={"category": category, "used": 0, "bound": LIMITS[category.lower()]},
                    ),
                    exit_status=2,
                    verdict="failed",
                )
                if category == "W":
                    self.report["analyses"][0]["status"] = "failed"
                with self.assertRaises(audit.FailedReportAttempt) as error:
                    self.validate(exit_status=2)
                self.assertEqual(error.exception.bodies, 2)
                self.assertTrue(
                    all(body["ownership"] == "complete" for body in self.report["bodies"])
                )

    def test_failure_without_limit_does_not_imply_budget_refusal(self):
        self.report.update(
            failure=incomplete("tool", "tool_failure"), exit_status=2, verdict="failed"
        )
        with self.assertRaises(audit.FailedReportAttempt) as error:
            self.validate(exit_status=2)
        self.assertEqual(error.exception.bodies, 2)
        self.assertIsNone(self.report["first_incomplete"])


class RunnerTests(RepositoryFixture):
    def test_enumeration_work_refusal_retains_four_failed_attempts(self):
        self.executable("enumeration_work")
        result, errors = self.run_fixture()
        self.assertFalse(result["complete"])
        self.assertEqual((len(result["attempts"]), len(errors)), (4, 4))
        self.assertEqual(
            result["totals"],
            {
                "root_attempts": 4,
                "unique_files": 5,
                "unique_source_bodies": 5,
                "context_bodies": 11,
            },
        )
        for attempt in result["attempts"]:
            self.assertEqual(attempt["exit_status"], 1)
            self.assertIsNotNone(attempt["report_sha256"])
            self.assertTrue(attempt["inputs"])
        self.assertEqual(len(list((self.base / "out").glob("report-*.json"))), 4)
        with self.assertRaisesRegex(audit.InvalidEvidence, "incomplete evidence"):
            self.validate_fixture()

    def test_auto_inventory_main_first_and_distinct_source_totals(self):
        self.executable()
        result, errors = self.run_fixture()
        self.assertEqual(errors, [])
        self.assertTrue(result["complete"])
        self.assertEqual(result["inventory"], sorted(result["inventory"]))
        self.assertEqual(result["attempts"][0]["root"], "src/fort/main.ft")
        self.assertEqual(len(result["attempts"]), 4)
        self.assertEqual(
            result["totals"],
            {
                "root_attempts": 4,
                "unique_files": 5,
                "unique_source_bodies": 5,
                "context_bodies": 11,
            },
        )
        self.assertEqual(self.validate_fixture(), result)
        self.assertEqual([row["exit_status"] for row in result["attempts"]], [1] * 4)
        self.assertEqual(len(list((self.base / "out").glob("inputs-*.json"))), 4)

    def test_new_tracked_root_enters_inventory_without_constant(self):
        self.write("src/fort/new.ft", "fn new_root() void {}\n")
        self.write("src/fort/not_source.txt", "not a source\n")
        self.commit()
        self.executable()
        result, errors = self.run_fixture()
        self.assertEqual(errors, [])
        self.assertEqual(result["totals"]["root_attempts"], 5)
        self.assertIn("src/fort/new.ft", result["inventory"])
        self.assertNotIn("src/fort/not_source.txt", result["inventory"])
        self.validate_fixture()

    def test_untracked_root_is_no_inventory_root(self):
        self.write("src/fort/untracked.ft", "fn untracked() void {}\n")
        self.assertNotIn("src/fort/untracked.ft", audit.inventory(self.checkout))
        self.executable()
        result, errors = self.run_fixture()
        self.assertEqual(errors, [])
        self.assertEqual(result["totals"]["root_attempts"], 4)

    def test_accepts_zero_only_with_valid_reports(self):
        self.executable("accepted")
        result, errors = self.run_fixture()
        self.assertEqual(errors, [])
        self.assertEqual([row["exit_status"] for row in result["attempts"]], [0] * 4)
        self.validate_fixture()

    def test_failed_root_does_not_remove_later_attempts(self):
        self.executable("invalid_first")
        result, errors = self.run_fixture()
        self.assertFalse(result["complete"])
        self.assertEqual(len(errors), 1)
        self.assertEqual(len(result["attempts"]), 4)
        self.assertEqual(result["attempts"][0]["exit_status"], 1)
        self.assertIsNotNone(result["attempts"][0]["report_sha256"])
        self.assertEqual(len(result["attempts"][0]["inputs"]), 3)
        self.assertTrue((self.base / "out/report-0000.json").is_file())
        with self.assertRaisesRegex(audit.InvalidEvidence, "incomplete evidence"):
            self.validate_fixture()

    def test_invalid_report_rejection_cases_have_counted_attempts(self):
        for mode in ("missing", "truncated", "duplicate", "incomplete", "exit2"):
            with self.subTest(mode=mode):
                self.executable(mode)
                result, errors = self.run_fixture(output=mode)
                self.assertFalse(result["complete"])
                self.assertEqual(len(errors), 4)
                self.assertEqual(len(result["attempts"]), 4)
                for attempt in result["attempts"]:
                    self.assertEqual(attempt["report_sha256"] is None, mode == "missing")
                    self.assertEqual(attempt["exit_status"], 2 if mode == "exit2" else 1)
                self.assertEqual(len((self.base / mode / "errors.txt").read_text().splitlines()), 4)

    def test_schema_valid_failed_reports_keep_known_body_counts(self):
        for mode in ("exit2", "incomplete"):
            with self.subTest(mode=mode):
                self.executable(mode)
                result, errors = self.run_fixture(output=mode)
                self.assertFalse(result["complete"])
                self.assertEqual(len(errors), 4)
                self.assertEqual(
                    result["totals"],
                    {
                        "root_attempts": 4,
                        "unique_files": 5,
                        "unique_source_bodies": 5,
                        "context_bodies": 11,
                    },
                )
                self.assertTrue(
                    all(attempt["report_sha256"] is not None for attempt in result["attempts"])
                )
                self.assertTrue(all(attempt["inputs"] for attempt in result["attempts"]))

    def test_fresh_directory_rejects_stale_destination(self):
        self.executable()
        self.run_fixture()
        before = (self.base / "out/audit.json").read_bytes()
        with self.assertRaisesRegex(audit.InvalidEvidence, "fresh directory"):
            self.run_fixture()
        self.assertEqual((self.base / "out/audit.json").read_bytes(), before)

    def test_runner_rejects_changed_input_and_binary_bytes(self):
        for mode in ("source_change", "input_added", "input_deleted", "binary_change"):
            with self.subTest(mode=mode):
                if not (self.standard / "rt.ft").exists():
                    (self.standard / "rt.ft").write_text("fn rt() void {}\n")
                self.git("checkout", "--", "src/fort")
                (self.standard / "new.ft").unlink(missing_ok=True)
                self.executable(mode)
                result, errors = self.run_fixture(output=mode)
                self.assertFalse(result["complete"])
                self.assertGreaterEqual(len(errors), 1)
                self.assertEqual(len(result["attempts"]), 4)
                self.assertIsNotNone(result["attempts"][0]["report_sha256"])
                self.assertIn("changed", " ".join(errors))

    def test_os_execution_failure_keeps_attempts(self):
        self.executable()
        original = audit.subprocess.run

        def fail_compiler(argv, **options):
            if argv[0] == str(self.compiler) and argv[1:] != ["--version"]:
                raise OSError("cannot execute")
            return original(argv, **options)

        with patch.object(audit.subprocess, "run", side_effect=fail_compiler):
            result, errors = self.run_fixture()
        self.assertEqual(len(errors), 4)
        self.assertFalse(result["complete"])
        self.assertEqual([row["exit_status"] for row in result["attempts"]], [2] * 4)
        self.assertTrue(all(row["report_sha256"] is None for row in result["attempts"]))

    def test_unreadable_reports_do_not_remove_later_attempts(self):
        self.executable()
        original = Path.read_bytes

        def read_bytes(path):
            if path.name.startswith("report-"):
                raise OSError("cannot read report bytes")
            return original(path)

        with patch.object(Path, "read_bytes", read_bytes):
            result, errors = self.run_fixture()
        self.assertFalse(result["complete"])
        self.assertEqual(len(errors), 4)
        self.assertEqual(len(result["attempts"]), 4)
        self.assertTrue(all(attempt["report_sha256"] is None for attempt in result["attempts"]))

    def test_exact_target_configuration_and_modes(self):
        self.executable()
        target = "arm64-apple-macosx11.0.0"
        result, errors = self.run_fixture(
            target=target, cfg=["feature=yes"], release=True, no_bounds=True
        )
        self.assertEqual(errors, [])
        self.assertEqual(result["target"], target)
        self.assertIn({"key": "target_arch", "value": "aarch64"}, result["configuration"])
        self.assertIn({"key": "feature", "value": "yes"}, result["configuration"])
        self.validate_fixture()
        for attempt in result["attempts"]:
            self.assertIn("--release", attempt["argv"])
            self.assertIn("--no-bounds-check", attempt["argv"])

    def test_provenance_missing_mismatch_dirty_and_digest_changes(self):
        self.executable()
        self.provenance.unlink()
        with self.assertRaises(audit.InvalidEvidence):
            self.run_fixture()
        self.refresh_provenance()
        value = json.loads(self.provenance.read_text())
        self.provenance.write_text(json.dumps(dict(value, revision="0" * 40)))
        with self.assertRaisesRegex(audit.InvalidEvidence, "revision mismatch"):
            self.run_fixture()
        self.provenance.write_text(json.dumps(dict(value, sha256="0" * 64)))
        with self.assertRaisesRegex(audit.InvalidEvidence, "digest mismatch"):
            self.run_fixture()
        self.refresh_provenance()
        self.write("src/fort/main.ft", "fn changed() void {}\n")
        with self.assertRaisesRegex(audit.InvalidEvidence, "tracked source changes"):
            self.run_fixture()

    def test_compiler_revision_can_differ_from_source_revision(self):
        other = self.clone("compiler-source")
        path = other / "src/fort/ownership_limits.ft"
        path.write_text(path.read_text().replace("65536", "65537"))
        subprocess.check_call(
            [
                "git",
                "-C",
                str(other),
                "commit",
                "-qam",
                "Change synthetic limits",
                "--author=Audit Test <audit@example.invalid>",
            ],
            stdout=subprocess.DEVNULL,
        )
        self.executable()
        self.provenance.write_text(
            json.dumps(
                {
                    "revision": audit.git(other, "rev-parse", "HEAD"),
                    "sha256": audit.digest(self.compiler.read_bytes()),
                }
            )
        )
        result, errors = self.run_fixture(compiler_checkout=other)
        self.assertFalse(result["complete"])
        self.assertNotEqual(result["source_revision"], result["compiler_revision"])
        self.assertEqual(len(errors), 4)
        self.assertTrue(all("production table" in error for error in errors))

    def test_snapshot_detects_imports_outside_report_closure(self):
        self.executable()
        original = audit.snapshot
        calls = 0

        def altered(roots):
            nonlocal calls
            calls += 1
            values = original(roots)
            if calls == 4:
                values[str(self.checkout / "src/fort/ownership_api.ft")] += b"changed"
            return values

        with patch.object(audit, "snapshot", side_effect=altered):
            result, errors = self.run_fixture()
        self.assertFalse(result["complete"])
        self.assertEqual(len(errors), 1)
        self.assertIn("changed", errors[0])

    def test_distinct_compiler_revision_with_matching_limits_is_valid(self):
        other = self.clone("other-compiler-source")
        subprocess.check_call(
            [
                "git",
                "-C",
                str(other),
                "commit",
                "--allow-empty",
                "-qm",
                "Create separate synthetic compiler revision",
            ],
            stdout=subprocess.DEVNULL,
        )
        self.executable()
        self.provenance.write_text(
            json.dumps(
                {
                    "revision": audit.git(other, "rev-parse", "HEAD"),
                    "sha256": audit.digest(self.compiler.read_bytes()),
                }
            )
        )
        result, errors = self.run_fixture(compiler_checkout=other)
        self.assertEqual(errors, [])
        self.assertNotEqual(result["source_revision"], result["compiler_revision"])
        self.assertEqual(self.validate_fixture(compiler_checkout=other), result)

    def test_matching_changed_compiler_limits_do_not_use_source_limits(self):
        other = self.clone("other-limits-source")
        path = other / "src/fort/ownership_limits.ft"
        path.write_text(path.read_text().replace("65536", "65537"))
        subprocess.check_call(
            ["git", "-C", str(other), "commit", "-qam", "Change synthetic production bound"],
            stdout=subprocess.DEVNULL,
        )
        self.executable(limits=dict(LIMITS, w=65537))
        self.provenance.write_text(
            json.dumps(
                {
                    "revision": audit.git(other, "rev-parse", "HEAD"),
                    "sha256": audit.digest(self.compiler.read_bytes()),
                }
            )
        )
        result, errors = self.run_fixture(compiler_checkout=other)
        self.assertEqual(errors, [])
        self.assertEqual(self.validate_fixture(compiler_checkout=other), result)

    def test_version_comparison_preserves_internal_text(self):
        self.executable(version="  fort synthetic 1\nbuild metadata  ")
        result, errors = self.run_fixture()
        self.assertEqual(errors, [])
        self.assertEqual(self.validate_fixture(), result)

    def test_report_compiler_version_must_match_binary_query(self):
        self.executable("version_mismatch")
        result, errors = self.run_fixture()
        self.assertFalse(result["complete"])
        self.assertEqual(len(errors), 4)
        self.assertTrue(all("version mismatch" in error for error in errors))

    def test_missing_main_root_fails_inventory(self):
        self.git("rm", "-q", "src/fort/main.ft")
        self.git("commit", "-qm", "Remove synthetic main root")
        with self.assertRaisesRegex(audit.InvalidEvidence, "missing tracked"):
            audit.inventory(self.checkout)

    def test_configuration_preserves_empty_and_equals_values(self):
        self.executable()
        result, errors = self.run_fixture(cfg=["feature=,value=a=b"])
        self.assertEqual(errors, [])
        self.assertIn({"key": "feature", "value": ""}, result["configuration"])
        self.assertIn({"key": "value", "value": "a=b"}, result["configuration"])
        self.validate_fixture()
        for cfg in (
            ["target_os=linux"],
            ["feature=one,feature=two"],
            ["bad"],
            ["=empty"],
            ["x=a,"],
        ):
            with self.subTest(cfg=cfg):
                with self.assertRaises(audit.InvalidEvidence):
                    audit.effective_configuration(TARGET, cfg)


class EnforcementTests(RepositoryFixture):
    def enforce(self, mode, scope="local"):
        self.executable(mode)
        # Each run writes fresh report paths, so a second run in one test takes its own output.
        output = f"{mode}-{scope}"
        result, errors = self.run_fixture(output=output)
        self.assertEqual(errors, [])
        self.assertTrue(result["complete"])
        return audit.enforce_audit(
            self.base / output / "audit.json",
            scope=scope,
            checkout=self.checkout,
            compiler=self.compiler,
            compiler_checkout=self.checkout,
            compiler_provenance=self.provenance,
        )

    def test_clean_scope_accepts_and_counts_declared_bodies(self):
        summary, problems = self.enforce("local_clean")
        self.assertEqual(problems, [])
        self.assertEqual(summary["scope"], "local")
        self.assertEqual(summary["problems"], 0)
        self.assertGreater(summary["declared_context_bodies"], 0)
        self.assertLess(summary["declared_context_bodies"], summary["context_bodies"])

    def test_violation_anywhere_is_rejected(self):
        summary, problems = self.enforce("local_violation")
        self.assertEqual(problems, ["src/fort/main.ft:1: main: 1 local violation(s)"])
        self.assertEqual(summary["problems"], 1)

    def test_incomplete_declared_obligation_is_rejected(self):
        summary, problems = self.enforce("local_incomplete")
        expected = "src/fort/main.ft:1: main: incomplete declared local obligation (incomplete)"
        self.assertEqual(problems, [expected])

    def test_one_body_in_overlapping_closures_is_one_problem(self):
        summary, problems = self.enforce("local_shared_violation")
        self.assertEqual(problems, ["src/fort/shared.ft:1: shared: 1 local violation(s)"])
        self.assertEqual(summary["problems"], 1)

    def test_stored_scope_rejects_violations_and_incomplete_declared_bodies(self):
        summary, problems = self.enforce("stored_clean", "stored_borrows")
        self.assertEqual(problems, [])
        self.assertEqual(summary["scope"], "stored_borrows")
        self.assertGreater(summary["declared_context_bodies"], 0)
        self.assertLess(summary["declared_context_bodies"], summary["context_bodies"])
        summary, problems = self.enforce("stored_violation", "stored_borrows")
        self.assertEqual(problems, ["src/fort/main.ft:1: main: 1 stored_borrows violation(s)"])
        summary, problems = self.enforce("stored_incomplete", "stored_borrows")
        expected = (
            "src/fort/main.ft:1: main: incomplete declared stored_borrows obligation (incomplete)"
        )
        self.assertEqual(problems, [expected])
        summary, problems = self.enforce("stored_shared_violation", "stored_borrows")
        self.assertEqual(problems, ["src/fort/shared.ft:1: shared: 1 stored_borrows violation(s)"])

    def test_raw_scope_rejects_violations_and_incomplete_declared_obligations(self):
        summary, problems = self.enforce("raw_clean", scope="raw")
        self.assertEqual((problems, summary["scope"]), ([], "raw"))
        self.assertGreater(summary["declared_context_bodies"], 0)
        self.assertLess(summary["declared_context_bodies"], summary["context_bodies"])
        summary, problems = self.enforce("raw_violation", scope="raw")
        self.assertEqual(problems, ["src/fort/main.ft:1: main: 1 raw violation(s)"])
        summary, problems = self.enforce("raw_incomplete", scope="raw")
        expected = "src/fort/main.ft:1: main: incomplete declared raw obligation (incomplete)"
        self.assertEqual(problems, [expected])
        summary, problems = self.enforce("raw_shared_violation", scope="raw")
        self.assertEqual(problems, ["src/fort/shared.ft:1: shared: 1 raw violation(s)"])
        self.assertEqual(summary["problems"], 1)

    def test_each_scope_reads_only_its_own_row(self):
        # A problem of one scope declares nothing in another scope.
        pairs = (
            ("local_violation", "raw"),
            ("raw_violation", "local"),
            ("local_violation", "stored_borrows"),
            ("stored_violation", "local"),
            ("stored_violation", "raw"),
            ("raw_violation", "stored_borrows"),
        )
        for mode, scope in pairs:
            with self.subTest(mode=mode, scope=scope):
                summary, problems = self.enforce(mode, scope=scope)
                self.assertEqual((problems, summary["declared_context_bodies"]), ([], 0))

    def test_unavailable_local_rows_declare_nothing(self):
        summary, problems = self.enforce("valid")
        self.assertEqual(problems, [])
        self.assertEqual(summary["declared_context_bodies"], 0)

    def test_enforcement_validates_the_evidence_first(self):
        self.executable("local_clean")
        self.run_fixture()
        report = self.base / "out/report-0000.json"
        report.write_text(report.read_text().replace('"complete": true', '"complete": false', 1))
        with self.assertRaises(audit.InvalidEvidence):
            audit.enforce_audit(
                self.base / "out/audit.json",
                scope="local",
                checkout=self.checkout,
                compiler=self.compiler,
                compiler_checkout=self.checkout,
                compiler_provenance=self.provenance,
            )
        with self.assertRaisesRegex(audit.InvalidEvidence, "unknown scope"):
            audit.enforce_audit(self.base / "out/audit.json", scope="calls_heap")

    def test_cli_returns_one_for_problems_and_zero_for_a_clean_scope(self):
        common = [
            "--checkout",
            str(self.checkout),
            "--compiler",
            str(self.compiler),
            "--compiler-provenance",
            str(self.provenance),
        ]
        cases = (
            ("local_clean", "local", 0),
            ("local_violation", "local", 1),
            ("stored_clean", "stored_borrows", 0),
            ("stored_violation", "stored_borrows", 1),
        )
        for mode, scope, status in cases:
            with self.subTest(mode=mode):
                self.executable(mode)
                self.run_fixture(output=mode)
                stdout = io.StringIO()
                with redirect_stdout(stdout), redirect_stderr(io.StringIO()) as errors:
                    result = audit.main(
                        [
                            "enforce",
                            "--scope",
                            scope,
                            *common,
                            str(self.base / mode / "audit.json"),
                        ]
                    )
                self.assertEqual(result, status)
                self.assertEqual(json.loads(stdout.getvalue())["problems"], status)
                self.assertEqual(len(errors.getvalue().splitlines()), status)
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            audit.main(["enforce", "--scope", "calls_heap", *common, str(self.base / "x.json")])


class AttestationTests(RepositoryFixture):
    def setUp(self):
        super().setUp()
        self.executable()
        self.result, errors = self.run_fixture()
        self.assertEqual(errors, [])
        self.audit_path = self.base / "out/audit.json"

    def reject_audit(self, mutate, pattern=None):
        value = copy.deepcopy(self.result)
        mutate(value)
        self.audit_path.write_text(json.dumps(value))
        with self.assertRaisesRegex(audit.InvalidEvidence, pattern or ".+"):
            self.validate_fixture()

    def test_missing_extra_and_invalid_top_level_members(self):
        for member in self.result:
            with self.subTest(member=member):
                self.reject_audit(lambda value: value.pop(member))
        self.reject_audit(lambda value: value.update(extra=1))
        self.reject_audit(lambda value: value.update(version=True))
        self.reject_audit(lambda value: value.update(version=2))
        self.reject_audit(lambda value: value.update(complete=False))

    def test_inventory_missing_duplicate_extra_and_wrong_order(self):
        self.reject_audit(lambda value: value["inventory"].pop())
        self.reject_audit(lambda value: value["inventory"].append(value["inventory"][0]))
        self.reject_audit(lambda value: value["inventory"].append("src/fort/other.ft"))
        self.reject_audit(lambda value: value["inventory"].reverse())

    def test_attempt_missing_duplicate_or_wrong_root_order(self):
        self.reject_audit(lambda value: value["attempts"].pop())
        self.reject_audit(lambda value: value["attempts"].append(value["attempts"][0]))
        self.reject_audit(
            lambda value: value["attempts"][1].update(root=value["attempts"][0]["root"])
        )
        self.reject_audit(lambda value: value["attempts"].reverse())

    def test_attestation_revisions_and_digest_must_match_provenance(self):
        for field in ("source_revision", "compiler_revision"):
            self.reject_audit(lambda value: value.update({field: "0" * 40}))
            self.reject_audit(lambda value: value.update({field: "short"}))
        for invalid in ("0" * 64, "A" * 64, True, None):
            self.reject_audit(lambda value: value.update(compiler_sha256=invalid))

    def test_retained_report_digest_rejects_changed_bytes(self):
        report_path = self.base / "out/report-0000.json"
        report_path.write_bytes(report_path.read_bytes() + b"\n")
        with self.assertRaisesRegex(audit.InvalidEvidence, "digest mismatch"):
            self.validate_fixture()

    def test_missing_retained_report_fails(self):
        (self.base / "out/report-0000.json").unlink()
        with self.assertRaises(audit.InvalidEvidence):
            self.validate_fixture()

    def test_retained_report_duplicate_members_fail(self):
        report_path = self.base / "out/report-0000.json"
        report_path.write_text('{"kind":1,"kind":2}')
        with self.assertRaisesRegex(audit.InvalidEvidence, "duplicate member"):
            self.validate_fixture()

    def test_attempt_argv_target_configuration_modes_and_paths(self):
        mutations = [
            lambda r: r["attempts"][0].update(cwd=str(self.base)),
            lambda r: r["attempts"][0].update(report_path=str(self.base / "stale.json")),
            lambda r: r["attempts"][0].update(argv=[]),
            lambda r: r["attempts"][0]["argv"].__setitem__(0, str(self.base / "other")),
            lambda r: r["attempts"][0]["argv"].__setitem__(1, "--tokens"),
            lambda r: r["attempts"][0]["argv"].__setitem__(4, str(self.base / "stale.json")),
            lambda r: r["attempts"][0]["argv"].__setitem__(6, "arm64-apple-macosx11.0.0"),
            lambda r: r["attempts"][0]["argv"].__setitem__(-1, "src/fort/shared.ft"),
            lambda r: r["attempts"][0]["argv"].insert(-1, "--ast"),
            lambda r: r["attempts"][0]["argv"].insert(-1, "--release"),
            lambda r: r["attempts"][0].update(exit_status=2),
            lambda r: r.update(configuration=[]),
            lambda r: r.update(target="wrong"),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.reject_audit(mutate)

    def test_attempt_inputs_and_all_totals_are_exact(self):
        self.reject_audit(lambda value: value["attempts"][0]["inputs"].pop())
        self.reject_audit(
            lambda value: value["attempts"][0]["inputs"].append(value["attempts"][0]["inputs"][0])
        )
        self.reject_audit(lambda value: value["attempts"][0]["inputs"][0].update(sha256="0" * 64))
        self.reject_audit(
            lambda value: value["attempts"][0]["inputs"][0].update(path="src/fort/shared.ft")
        )
        for field in self.result["totals"]:
            self.reject_audit(lambda value: value["totals"].update({field: 99}))
            self.reject_audit(lambda value: value["totals"].update({field: True}))
            self.reject_audit(lambda value: value["totals"].pop(field))

    def test_candidate_manifest_is_required_and_matches_current_bytes(self):
        path = self.base / "out/inputs-0000.json"
        values = json.loads(path.read_text())
        values.pop()
        path.write_text(json.dumps(values))
        with self.assertRaisesRegex(audit.InvalidEvidence, "pre-invocation manifest"):
            self.validate_fixture()
        path.unlink()
        with self.assertRaises(audit.InvalidEvidence):
            self.validate_fixture()

    def test_changed_standard_input_has_no_matching_attestation(self):
        path = self.standard / "rt.ft"
        path.write_text(path.read_text() + "// changed\n")
        with self.assertRaisesRegex(audit.InvalidEvidence, "bytes or closure mismatch"):
            self.validate_fixture()

    def test_new_source_revision_invalidates_old_attestation(self):
        self.write("src/fort/new.ft", "fn new_root() void {}\n")
        self.commit()
        with self.assertRaises(audit.InvalidEvidence):
            self.validate_fixture()

    def test_cli_run_and_validate_use_native_fixture(self):
        common = [
            "--checkout",
            str(self.checkout),
            "--compiler",
            str(self.compiler),
            "--compiler-provenance",
            str(self.provenance),
        ]
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            status = audit.main(
                [
                    "run",
                    *common,
                    "--output",
                    str(self.base / "cli"),
                    "--target",
                    TARGET,
                    "--std-dir",
                    str(self.standard),
                ]
            )
        self.assertEqual(status, 0)
        self.assertEqual(json.loads(stdout.getvalue())["root_attempts"], 4)
        with redirect_stdout(io.StringIO()):
            self.assertEqual(
                audit.main(["validate", *common, str(self.base / "cli/audit.json")]), 0
            )
        with redirect_stderr(io.StringIO()):
            self.assertEqual(audit.main(["validate", *common, str(self.base / "absent.json")]), 1)

    def test_cli_failure_returns_one_and_keeps_artifacts(self):
        self.executable("incomplete")
        args = [
            "run",
            "--checkout",
            str(self.checkout),
            "--compiler",
            str(self.compiler),
            "--compiler-provenance",
            str(self.provenance),
            "--output",
            str(self.base / "bad-cli"),
            "--target",
            TARGET,
            "--std-dir",
            str(self.standard),
        ]
        with redirect_stderr(io.StringIO()) as errors:
            self.assertEqual(audit.main(args), 1)
        self.assertEqual(len(errors.getvalue().splitlines()), 4)
        self.assertFalse(json.loads((self.base / "bad-cli/audit.json").read_text())["complete"])


if __name__ == "__main__":
    unittest.main()
