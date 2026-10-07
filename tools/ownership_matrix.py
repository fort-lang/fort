#!/usr/bin/env python3
"""Run the ownership qualification matrix and compare its host reports.

The manifest `test/ownership/qualification/manifest.json` lists the cases. Each case runs with
`--ownership-check` in each of its modes and in the four combinations of `--release` and
`--no-bounds-check` (D19.8). One run gives one host report. `compare` holds the reports of two
hosts against each other.

Subcommands:
  validate  check the manifest, its files, its counts and the approved hashes; no compiler
  run       run the matrix on this host and write a host report
  compare   compare two host reports case by case

The matrix is not registered as a verdict test. Its unit test checks the tool with a fake
compiler and checks the manifest without a compiler.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile

KIND = "fort-ownership-qualification"
REPORT_KIND = "fort-ownership-matrix"
VERSION = 1
CHECKOUT = Path(__file__).resolve().parents[1]
MANIFEST = Path("test/ownership/qualification/manifest.json")
# The four option combinations of D19.8, in a fixed order.
OPTIONS = (
    ("checked", ()),
    ("release", ("--release",)),
    ("no_bounds", ("--no-bounds-check",)),
    ("release_no_bounds", ("--release", "--no-bounds-check")),
)
MODES = ("check", "build")
GROUPS = ("original", "counterpart", "zero_element", "linear")
EXPECTED = ("accept", "reject")
# A verdict class of one execution. `source_error` is a source failure that the report records.
VERDICTS = ("accepted", "violated", "incomplete", "source_error", "failed")
CASE_MEMBERS = {"id", "group", "path", "modes", "pairs"}
# The patterns of test/lang/run_tests.py (ANNOTATION_RE, REPORT_RE, RENDERED_PREFIX). The unit
# test holds them equal, so the matrix reads a fixture as the language harness reads it.
ANNOTATION = re.compile(r"^(.*?\S)\s*//! error:(.*)$")
HEADER = re.compile(r"^(.+):(\d+):(\d+): (error|note): (.*)$")
RENDERED_PREFIX = " "
TIMEOUT = 300


class ManifestError(ValueError):
    """The manifest or one of its files does not meet the manifest contract."""


def require(condition, message):
    if not condition:
        raise ManifestError(message)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)


def approved_fixtures(root, manifest):
    """Map each approved file name to its approved manifest row."""
    rows = {}
    groups = set()
    positions = 0
    for relative in manifest["approved_manifests"]:
        approved = read_json(root / relative)
        for row in approved["fixtures"]:
            require(row["file"] not in rows, "approved file %s appears twice" % row["file"])
            rows[row["file"]] = dict(row, directory=str(Path(relative).parent))
            groups.add(row["group"])
            positions += len(row["expected_positions"])
    return rows, len(groups), positions


def annotations(path):
    """Return the expected (line, text) errors of a fixture in the directive format."""
    expected = []
    for number, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        if line.startswith("//!") or "//!" not in line:
            continue
        found = ANNOTATION.match(line)
        if found:
            expected.append((number, found.group(2).strip()))
    return expected


def directive_verdict(path):
    first = Path(path).read_text(encoding="utf-8").split("\n", 1)[0].strip()
    require(
        first in ("//! run", "//! fail"), "%s: first line is not '//! run' or '//! fail'" % path
    )
    return "accept" if first == "//! run" else "reject"


def expectation(root, case, approved):
    """Return the expected verdict and the expected error lines and texts of one case."""
    path = root / case["path"]
    if case["group"] == "original":
        row = approved.get(path.name)
        require(row is not None, "%s: no approved manifest row" % case["path"])
        require(
            sha256(path) == row["source_sha256"],
            "%s: source differs from its approved hash" % case["path"],
        )
        errors = [
            (p["line"], p["message"]) for p in row["expected_positions"] if p["kind"] == "error"
        ]
        notes = [
            (p["line"], p["message"]) for p in row["expected_positions"] if p["kind"] == "note"
        ]
        return {
            "verdict": row["expected_verdict"],
            "errors": errors,
            "notes": notes,
            "columns": [
                (p["line"], p["column"]) for p in row["expected_positions"] if p["kind"] == "error"
            ],
        }
    verdict = directive_verdict(path)
    errors = annotations(path)
    require(
        (verdict == "reject") == bool(errors),
        "%s: a reject case needs an error annotation and an accept case has none" % case["path"],
    )
    return {"verdict": verdict, "errors": errors, "notes": [], "columns": []}


def validate(root, manifest):
    """Check the manifest and return its measured counts. Raise ManifestError on a problem."""
    require(manifest.get("kind") == KIND, "kind is not %s" % KIND)
    require(manifest.get("version") == VERSION, "version is not %d" % VERSION)
    require(manifest.get("registered") is False, "the qualification matrix is not registered yet")
    approved, group_count, positions = approved_fixtures(root, manifest)
    ids = set()
    paths = set()
    counts = {group: 0 for group in GROUPS}
    for case in manifest["cases"]:
        require(
            set(case) <= CASE_MEMBERS and {"id", "group", "path", "modes"} <= set(case),
            "case %r has unknown or missing members" % case.get("id"),
        )
        require(re.fullmatch(r"[a-z0-9_]+", case["id"]) is not None, "bad case id %r" % case["id"])
        require(case["id"] not in ids, "case id %s appears twice" % case["id"])
        require(case["path"] not in paths, "case path %s appears twice" % case["path"])
        require(case["group"] in GROUPS, "case %s has an unknown group" % case["id"])
        require(
            case["modes"]
            and all(m in MODES for m in case["modes"])
            and len(set(case["modes"])) == len(case["modes"]),
            "case %s has bad modes" % case["id"],
        )
        require(
            (root / case["path"]).is_file(), "case %s: %s is missing" % (case["id"], case["path"])
        )
        ids.add(case["id"])
        paths.add(case["path"])
        counts[case["group"]] += 1
        expectation(root, case, approved)
    by_id = {case["id"]: case for case in manifest["cases"]}
    for case in manifest["cases"]:
        if "pairs" in case:
            require(
                case["group"] == "counterpart", "case %s: only a counterpart pairs" % case["id"]
            )
            partner = by_id.get(case["pairs"])
            require(
                partner is not None and partner["group"] == "original",
                "case %s pairs with no original" % case["id"],
            )
    originals = {Path(c["path"]).name for c in manifest["cases"] if c["group"] == "original"}
    require(originals == set(approved), "the original cases differ from the approved files")
    counts["approved_groups"] = group_count
    counts["approved_positions"] = positions
    counts["paired"] = sum(1 for case in manifest["cases"] if "pairs" in case)
    counts["executions"] = sum(len(case["modes"]) for case in manifest["cases"]) * len(OPTIONS)
    require(
        manifest["counts"] == counts,
        "counts %s differ from the measured %s"
        % (json.dumps(manifest["counts"], sort_keys=True), json.dumps(counts, sort_keys=True)),
    )
    return counts


def under_std(path, std_dir):
    """Return a path below the standard root as <std>/<path below it>, or None."""
    prefix = str(std_dir).rstrip("/") + "/" if std_dir else None
    if prefix and path.startswith(prefix):
        return "<std>/" + path[len(prefix) :]
    return None


def parse_diagnostics(stderr, std_dir):
    """Return the header lines of stderr as dictionaries, with the standard root as <std>."""
    found = []
    for line in stderr.splitlines():
        match = None if line.startswith(RENDERED_PREFIX) else HEADER.match(line)
        if not match:
            continue
        path = under_std(match.group(1), std_dir) or match.group(1)
        found.append(
            {
                "path": path,
                "line": int(match.group(2)),
                "col": int(match.group(3)),
                "kind": match.group(4),
                "message": match.group(5),
            }
        )
    return found


def classify(status, report):
    """Return the verdict class of one execution from its exit status and its report.

    A selected compile writes its report for a source failure too (spec/toolchain.md 1.1), so a
    missing report is a failure of the compiler, whatever its exit status.
    """
    if status not in (0, 1) or report is None:
        return "failed"
    failure = report.get("failure")
    if report.get("verdict") == "failed" or report.get("exit_status") != status:
        return "failed"
    if status == 0:
        return "accepted" if report.get("verdict") == "accepted" else "failed"
    if failure is not None and failure.get("code") in ("source_error", "unsupported_lowering"):
        return "source_error"
    if report.get("totals", {}).get("violations", 0) > 0:
        return "violated"
    return "incomplete" if report.get("first_incomplete") is not None else "failed"


def invoke(fort, std_dir, argv, cwd, timeout=TIMEOUT):
    """Run the compiler and return (status, stdout, stderr). A timeout gives status None."""
    try:
        result = subprocess.run(
            [str(fort), "--std-dir", str(std_dir)] + argv,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return None, b"", b"timeout"
    return result.returncode, result.stdout, result.stderr


def mode_flags(mode, scratch, name):
    if mode == "check":
        return ["--check"]
    return ["-S", "-o", str(Path(scratch) / name)]


def target(case, case_path, mode, scratch):
    """Return the working directory, the extra flags and the entry file of one execution.

    An approved original has no main. Its build mode compiles a generated entry that imports it
    through -I, so the original keeps its approved bytes and positions.
    """
    if case["group"] != "original" or mode != "build":
        return case_path.parent, [], case_path.name
    entry = Path(scratch) / "entry"
    entry.mkdir(exist_ok=True)
    (entry / "main.ft").write_text(
        "import %s;\n\nfn main() i32 {\n    return 0;\n}\n" % case_path.stem, encoding="utf-8"
    )
    return entry, ["-I", str(case_path.parent)], "main.ft"


def execute(fort, std_dir, place, mode, option, scratch):
    """Run one case in one mode and one option combination. Return its result row."""
    cwd, extra, entry = place
    name, flags = option
    report_path = Path(scratch) / ("report-%s-%s.json" % (mode, name))
    argv = (
        ["--ownership-check", "--ownership-report", str(report_path)]
        + list(flags)
        + extra
        + mode_flags(mode, scratch, "selected-%s.ll" % name)
        + [entry]
    )
    status, _, stderr = invoke(fort, std_dir, argv, cwd)
    stderr = stderr.decode("utf-8", "replace")
    try:
        report = read_json(report_path) if report_path.exists() else None
    except (OSError, ValueError):
        report = None
    diagnostics = parse_diagnostics(stderr, std_dir)
    verdict = classify(status, report)
    row = {
        "mode": mode,
        "option": name,
        "exit": status,
        "verdict": verdict,
        "violations": (report or {}).get("totals", {}).get("violations"),
        "first_incomplete": located_reason(report, std_dir),
        "diagnostics": diagnostics,
        "ir_equal": None,
    }
    if mode == "build" and verdict == "accepted":
        plain = Path(scratch) / ("plain-%s.ll" % name)
        status, _, _ = invoke(
            fort, std_dir, list(flags) + extra + ["-S", "-o", str(plain), entry], cwd
        )
        selected = Path(scratch) / ("selected-%s.ll" % name)
        row["ir_equal"] = (
            status == 0
            and plain.exists()
            and selected.exists()
            and plain.read_bytes() == selected.read_bytes()
        )
    return row


def own_diagnostics(row, case_name):
    return [d for d in row["diagnostics"] if Path(d["path"]).name == case_name]


def located_reason(report, std_dir):
    """Return the first incomplete reason of a report with its source file named by path.

    A file number depends on the load order of one invocation, so the reason names the file
    through the report's files table: a standard file as <std>/<path below the root>, and every
    other file by its name, which the case file and a generated entry keep on each host.
    """
    found = (report or {}).get("first_incomplete")
    if found is None:
        return None
    found = dict(found)
    source = found.get("source")
    if isinstance(source, dict):
        files = {row.get("id"): row.get("path") for row in report.get("files", [])}
        path = files.get(source.get("file"))
        source = dict(source)
        source["file"] = None if path is None else under_std(path, std_dir) or Path(path).name
        found["source"] = source
    return found


def reason(row):
    """The first incomplete reason: stage, code, exact limit and source file, line and column."""
    found = row["first_incomplete"]
    if found is None:
        return None
    source = found.get("source") or {}
    return (
        found.get("stage"),
        found.get("code"),
        json.dumps(found.get("limit"), sort_keys=True),
        source.get("file"),
        source.get("line"),
        source.get("col"),
    )


def signature(row):
    """The part of a result that every option combination must reproduce (D19.8)."""
    diagnostics = tuple(
        (d["path"], d["line"], d["col"], d["kind"], d["message"]) for d in row["diagnostics"]
    )
    return (row["verdict"], row["violations"], json.dumps(row["first_incomplete"]), diagnostics)


def mode_signature(row, case_name):
    """The part of a result that check mode and build mode must share (D20.1, D19.8).

    A path of the case file keeps its name only: build mode can reach the file through -I.
    """
    diagnostics = tuple(
        (
            case_name if Path(d["path"]).name == case_name else d["path"],
            d["line"],
            d["col"],
            d["kind"],
            d["message"],
        )
        for d in row["diagnostics"]
    )
    return (row["verdict"], row["violations"], reason(row), diagnostics)


def present(expected, found):
    """Each expected (line, text) has a diagnostic on its line; return that and the wording."""
    on_lines = all(any(d["line"] == line for d in found) for line, _ in expected)
    wording = all(
        any(text in d["message"] for d in found if d["line"] == line) for line, text in expected
    )
    return on_lines, wording


def judge(expected, row, case_name, strict=False):
    """Return 'match' or 'mismatch', the line and note agreement, the wording and the columns.

    A reject case matches only a complete proof with validated violations on exactly the
    expected lines. Incomplete proof is an error diagnostic of its own (spec/toolchain.md 1 and
    4.2), and an approved position holds no such error, so a violation beside incomplete proof
    does not match. A strict judgement also needs the expected notes and the expected wording.
    """
    own = own_diagnostics(row, case_name)
    errors = [d for d in own if d["kind"] == "error"]
    notes = [d for d in own if d["kind"] == "note"]
    lines = {d["line"] for d in errors} == {line for line, _ in expected["errors"]}
    _, error_wording = present(expected["errors"], errors)
    notes_present, note_wording = present(expected["notes"], notes)
    wording = error_wording and note_wording
    columns = all(
        any(d["line"] == line and d["col"] == col for d in errors)
        for line, col in expected["columns"]
    )
    if expected["verdict"] == "accept":
        outcome = row["verdict"] == "accepted"
    else:
        outcome = row["verdict"] == "violated" and row["first_incomplete"] is None and lines
        if strict:
            outcome = outcome and notes_present and wording
    return {
        "outcome": "match" if outcome else "mismatch",
        "lines": lines,
        "notes": notes_present,
        "wording": wording,
        "columns": columns,
    }


def run_case(fort, std_dir, root, case, approved, strict=False):
    expected = expectation(root, case, approved)
    case_path = (root / case["path"]).resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix="fort-matrix-") as scratch:
        for mode in case["modes"]:
            place = target(case, case_path, mode, scratch)
            for option in OPTIONS:
                row = execute(fort, std_dir, place, mode, option, scratch)
                row.update(judge(expected, row, case_path.name, strict))
                results.append(row)
    consistent = True
    for mode in case["modes"]:
        rows = [r for r in results if r["mode"] == mode]
        consistent &= len({signature(r) for r in rows}) == 1
    rows = {(r["mode"], r["option"]): r for r in results}
    modes_agree = all(
        len({mode_signature(rows[(mode, name)], case_path.name) for mode in case["modes"]}) == 1
        for name, _ in OPTIONS
    )
    return {
        "id": case["id"],
        "group": case["group"],
        "path": case["path"],
        "expected": expected["verdict"],
        "consistent": consistent,
        "modes_agree": modes_agree,
        "outcome": "match" if all(r["outcome"] == "match" for r in results) else "mismatch",
        "results": results,
    }


def summarize(cases):
    totals = {
        "cases": len(cases),
        "executions": sum(len(c["results"]) for c in cases),
        "match": 0,
        "mismatch": 0,
        "inconsistent": 0,
        "modes_disagree": 0,
        "violated_on_expected_lines": 0,
        "with_incomplete": 0,
        "ir_compared": 0,
        "ir_different": 0,
        "verdicts": {verdict: 0 for verdict in VERDICTS},
        "groups": {group: 0 for group in GROUPS},
    }
    for case in cases:
        totals[case["outcome"]] += 1
        totals["groups"][case["group"]] += 1
        totals["inconsistent"] += not case["consistent"]
        totals["modes_disagree"] += not case["modes_agree"]
        for row in case["results"]:
            totals["verdicts"][row["verdict"]] += 1
            totals["with_incomplete"] += row["first_incomplete"] is not None
            totals["violated_on_expected_lines"] += (
                case["expected"] == "reject" and row["verdict"] == "violated" and row["lines"]
            )
            if row["ir_equal"] is not None:
                totals["ir_compared"] += 1
                totals["ir_different"] += not row["ir_equal"]
    return totals


def compiler_version(fort):
    try:
        return subprocess.check_output([str(fort), "--version"], text=True, timeout=60).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def source_revision(root):
    """Return the git revision of the checkout and whether a tracked file differs from it.

    Known limit: this names the checkout that holds the manifest and the fixtures. It does not
    name the revision that built the compiler binary; the caller must build both from one tree.
    """
    try:
        revision = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            timeout=60,
            check=True,
        ).stdout.strip()
        status = subprocess.run(
            ["git", "-C", str(root), "status", "--porcelain", "--untracked-files=no"],
            capture_output=True,
            text=True,
            timeout=60,
            check=True,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return None, None
    return revision, status != ""


def run_matrix(fort, std_dir, root, manifest_path, selected=(), strict=False):
    manifest = read_json(manifest_path)
    validate(root, manifest)
    approved, _, _ = approved_fixtures(root, manifest)
    cases = [
        c
        for c in manifest["cases"]
        if not selected or any(text in c["id"] or text in c["path"] for text in selected)
    ]
    if not cases:
        raise ManifestError("the filters select no case")
    rows = [run_case(fort, std_dir, root, case, approved, strict) for case in cases]
    revision, modified = source_revision(root)
    return {
        "kind": REPORT_KIND,
        "version": VERSION,
        "host": platform.system(),
        "machine": platform.machine(),
        "compiler_version": compiler_version(fort),
        "manifest_sha256": sha256(manifest_path),
        "source_revision": revision,
        "source_modified": modified,
        "require_expected": strict,
        "complete": not selected,
        "options": [name for name, _ in OPTIONS],
        "cases": rows,
        "totals": summarize(rows),
    }


def compare(first, second):
    """Return the differences between two host reports of one manifest."""
    problems = []
    for report in (first, second):
        if report.get("kind") != REPORT_KIND or report.get("version") != VERSION:
            problems.append("a report is not a %s version %d document" % (REPORT_KIND, VERSION))
    if problems:
        return problems
    if first["manifest_sha256"] != second["manifest_sha256"]:
        problems.append("the reports use different manifests")
    if first["host"] == second["host"]:
        problems.append("both reports come from host %s" % first["host"])
    revisions = (first.get("source_revision"), second.get("source_revision"))
    if None in revisions or revisions[0] != revisions[1]:
        problems.append("the reports do not name one source revision")
    for report in (first, second):
        if report.get("source_modified") is not False:
            problems.append("the %s report ran a modified or unknown tree" % report["host"])
        if report.get("complete") is not True:
            problems.append("the %s report ran a filtered manifest" % report["host"])
    left = {case["id"]: case for case in first["cases"]}
    right = {case["id"]: case for case in second["cases"]}
    for missing in sorted(set(left) ^ set(right)):
        problems.append("%s: only one report has this case" % missing)
    for case_id in sorted(set(left) & set(right)):
        name = Path(left[case_id]["path"]).name
        a_rows = {(r["mode"], r["option"]): r for r in left[case_id]["results"]}
        b_rows = {(r["mode"], r["option"]): r for r in right[case_id]["results"]}
        if set(a_rows) != set(b_rows):
            problems.append("%s: the reports ran different executions" % case_id)
            continue
        for key in sorted(a_rows):
            a, b = a_rows[key], b_rows[key]
            own_a = [
                (d["line"], d["col"], d["kind"], d["message"]) for d in own_diagnostics(a, name)
            ]
            own_b = [
                (d["line"], d["col"], d["kind"], d["message"]) for d in own_diagnostics(b, name)
            ]
            facts_a = (a["verdict"], a["violations"], reason(a), a["ir_equal"], own_a)
            facts_b = (b["verdict"], b["violations"], reason(b), b["ir_equal"], own_b)
            if facts_a != facts_b:
                problems.append(
                    "%s %s %s: %s on %s, %s on %s"
                    % (
                        case_id,
                        key[0],
                        key[1],
                        a["verdict"],
                        first["host"],
                        b["verdict"],
                        second["host"],
                    )
                )
    return problems


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=1, sort_keys=True) + "\n", encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("validate")
    check.add_argument("--root", type=Path, default=CHECKOUT)
    check.add_argument("--manifest", type=Path)
    run = sub.add_parser("run")
    run.add_argument("--root", type=Path, default=CHECKOUT)
    run.add_argument("--manifest", type=Path)
    run.add_argument("--fort", type=Path, required=True)
    run.add_argument("--std-dir", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--require-expected", action="store_true")
    run.add_argument("filters", nargs="*")
    both = sub.add_parser("compare")
    both.add_argument("first", type=Path)
    both.add_argument("second", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "compare":
            problems = compare(read_json(args.first), read_json(args.second))
            for problem in problems:
                print("compare: %s" % problem)
            print("compare: %d differences" % len(problems))
            return 1 if problems else 0
        root = args.root.resolve()
        manifest_path = args.manifest or root / MANIFEST
        if args.command == "validate":
            counts = validate(root, read_json(manifest_path))
            print("validate: %s" % json.dumps(counts, sort_keys=True))
            return 0
        report = run_matrix(
            args.fort.resolve(),
            args.std_dir.resolve(),
            root,
            manifest_path,
            tuple(args.filters),
            args.require_expected,
        )
    except (ManifestError, OSError, ValueError, KeyError, TypeError) as error:
        print("ownership_matrix: %s" % error, file=sys.stderr)
        return 2
    write_json(args.output, report)
    totals = report["totals"]
    print(
        "run: %s %d cases, %d executions, %d match, %d mismatch, %d inconsistent, "
        "%d modes disagree, %d failed, %d IR compared, %d IR different"
        % (
            report["host"],
            totals["cases"],
            totals["executions"],
            totals["match"],
            totals["mismatch"],
            totals["inconsistent"],
            totals["modes_disagree"],
            totals["verdicts"]["failed"],
            totals["ir_compared"],
            totals["ir_different"],
        )
    )
    print("run: verdicts %s" % json.dumps(totals["verdicts"], sort_keys=True))
    print(
        "run: %d executions violate on the expected lines, %d have incomplete proof"
        % (totals["violated_on_expected_lines"], totals["with_incomplete"])
    )
    # An exit 2, a crash, a timeout or a missing report fails the run in every option.
    failed = totals["inconsistent"] or totals["ir_different"] or totals["verdicts"]["failed"]
    if args.require_expected:
        failed = failed or totals["mismatch"] or totals["modes_disagree"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
