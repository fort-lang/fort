#!/usr/bin/env python3
"""Run compiler-source ownership audits and validate their version 1 evidence.

Synthetic reports test this tool. They establish no compiler-source ownership proof.
"""

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ANALYSES = ("graph", "liveness", "local", "stored_borrows", "raw", "calls_heap", "process_exit")
STAGES = ("checking", "lowering", "verification")
STAGE_STATUSES = ("complete", "failed", "unexecuted")
WORK_STATUSES = ("complete", "incomplete", "failed", "unexecuted")
PROOF_STATUSES = ("complete", "violated", "incomplete", "failed", "unexecuted")
CATEGORIES = tuple("DRPGHTEWV")
SCOPES = ("path", "state", "function", "call", "computation", "report")
CATEGORY_SCOPES = dict(
    zip(
        CATEGORIES,
        (
            "path",
            "state",
            "state",
            "state",
            "state",
            "call",
            "function",
            "computation",
            "report",
        ),
        strict=True,
    )
)
INCOMPLETE_CODES = (
    "missing_transfer",
    "missing_graph",
    "missing_ffi",
    "missing_globals",
    "missing_renderer",
    "missing_budget",
    "missing_events",
    "work_limit",
    "report_limit",
    "missing_capture",
    "missing_path",
    "missing_boundary",
    "missing_correspondence",
    "source_coverage",
    "unknown_effect",
    "invalid_input",
    "missing_producer",
)
FAILURE_CODES = (
    "source_error",
    "unsupported_lowering",
    "verification_failure",
    "analysis_failure",
    "tool_failure",
)
U64_MAX = (1 << 64) - 1
U32_MAX = (1 << 32) - 1
LIMIT_VERSION = 2
REPORT_VERSION = 7
# The producer analyses with a ledger for each verified body, and the classification ledgers
# of the two flow analyses among them.
BODY_LEDGERS = ("local", "stored_borrows", "raw", "process_exit")
CLASSIFICATION_LEDGERS = {
    "local_classification": "local",
    "stored_borrows_classification": "stored_borrows",
}
LEDGER_LABELS = {
    "local": "local",
    "stored_borrows": "stored",
    "raw": "raw",
    "process_exit": "process exit",
}
# The calls_heap analysis keeps one ledger for each component computation of each of its two
# solvers. Its owner keys the first member of the component.
COMPONENT_LEDGERS = ("calls_summary", "calls_heap")
METER_NAMES = (
    "graph_private",
    "services",
    "local",
    "local_classification",
    "stored_borrows",
    "stored_borrows_classification",
    "raw",
    "process_exit",
    "process_exit_fills",
    *COMPONENT_LEDGERS,
)
BOUNDARY_KINDS = ("executable", "library")
BOUNDARY_STATUSES = ("complete", "incomplete", "unexecuted")
LIMIT_MEMBERS = ("version", "d", "r", "p", "g", "h", "t", "e", "w", "w_scale", "v")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
REVISION = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})\Z")


class InvalidEvidence(ValueError):
    """The evidence does not meet the report contract."""


class FailedReportAttempt(InvalidEvidence):
    """A schema-valid report fails the audit policy but retains measured rows."""

    def __init__(self, message, inputs, identities, bodies):
        super().__init__(message)
        self.inputs = inputs
        self.identities = identities
        self.bodies = bodies


def require(condition, message):
    if not condition:
        raise InvalidEvidence(message)


def obj(value, members, where):
    require(type(value) is dict, f"{where}: expected object")
    require(set(value) == set(members), f"{where}: incorrect members")
    return value


def array(value, where):
    require(type(value) is list, f"{where}: expected array")
    return value


def string(value, where):
    require(type(value) is str, f"{where}: expected string")
    require("\0" not in value, f"{where}: NUL is invalid")
    try:
        value.encode("utf-8")
    except UnicodeError as error:
        raise InvalidEvidence(f"{where}: invalid Unicode") from error
    return value


def integer(value, where, maximum=U64_MAX, minimum=0):
    require(type(value) is int and minimum <= value <= maximum, f"{where}: invalid integer")
    return value


def boolean(value, where):
    require(type(value) is bool, f"{where}: expected Boolean")
    return value


def choice(value, choices, where):
    string(value, where)
    require(value in choices, f"{where}: invalid value {value!r}")
    return value


def digest(data):
    return hashlib.sha256(data).hexdigest()


def hash_string(value, where):
    string(value, where)
    require(SHA256.fullmatch(value), f"{where}: invalid SHA-256")
    return value


def revision(value, where):
    string(value, where)
    require(REVISION.fullmatch(value), f"{where}: expected full git object name")
    return value


def duplicate_members(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"JSON: duplicate member {key!r}")
        result[key] = value
    return result


def read_json(path):
    try:
        data = Path(path).read_bytes()
        value = json.loads(data, object_pairs_hook=duplicate_members)
    except (OSError, UnicodeError, ValueError) as error:
        raise InvalidEvidence(f"{path}: {error}") from error
    return value, data


def atomic_json(path, value):
    path = Path(path)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def git(checkout, *args):
    try:
        return subprocess.check_output(["git", "-C", str(checkout), *args]).decode().strip()
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        raise InvalidEvidence(f"git provenance: {error}") from error


def inventory(checkout):
    paths = git(checkout, "ls-files", "-z", "--", "src/fort").split("\0")
    result = sorted(path for path in paths if path.endswith(".ft"))
    require("src/fort/main.ft" in result, "inventory: missing tracked src/fort/main.ft")
    require(len(result) == len(set(result)), "inventory: duplicate roots")
    return result


def root_order(paths):
    return ["src/fort/main.ft"] + [path for path in paths if path != "src/fort/main.ft"]


def configuration(value):
    result = []
    for row in array(value, "configuration"):
        obj(row, ("key", "value"), "configuration row")
        result.append((string(row["key"], "configuration key"), string(row["value"], "cfg value")))
    require(result == sorted(result), "configuration: keys are not sorted")
    require(len(result) == len({key for key, _ in result}), "configuration: duplicate keys")
    return value


def effective_configuration(target, cfg):
    if target == "x86_64-linux-gnu":
        values = {"target_os": "linux", "target_arch": "x86_64", "target_abi": ""}
    else:
        require(re.fullmatch(r"arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+", target), "invalid target")
        values = {"target_os": "macos", "target_arch": "aarch64", "target_abi": ""}
    for item in cfg:
        for assignment in item.split(","):
            require("=" in assignment, "configuration: invalid assignment")
            key, value = assignment.split("=", 1)
            require(re.fullmatch(r"[a-zA-Z_][a-zA-Z_0-9]*", key), "configuration: invalid key")
            require(key not in values, "configuration: duplicate or reserved key")
            values[key] = value
    return [{"key": key, "value": value} for key, value in sorted(values.items())]


def resolve_path(path, cwd):
    string(path, "path")
    require(path != "", "path: empty")
    return (Path(cwd) / path).resolve()


def normalize(path, cwd, checkout, standard):
    resolved = resolve_path(path, cwd)
    # A standard root can reside inside the checkout. Its namespace takes precedence.
    for root, prefix in ((Path(standard).resolve(), "std/"), (Path(checkout).resolve(), "")):
        try:
            return prefix + resolved.relative_to(root).as_posix()
        except ValueError:
            pass
    return str(resolved)


def snapshot(roots):
    result = {}
    visited = set()

    def walk_error(error):
        raise error

    try:
        for root in roots:
            for directory, children, files in os.walk(root, followlinks=True, onerror=walk_error):
                resolved = Path(directory).resolve()
                if resolved in visited:
                    children.clear()
                    continue
                visited.add(resolved)
                for name in files:
                    if name.endswith(".ft"):
                        path = resolved / name
                        result[str(path.resolve())] = path.read_bytes()
    except OSError as error:
        raise InvalidEvidence(f"input snapshot: {error}") from error
    return result


def source_range(value, files, source_lines, body=False):
    members = ("file", "line", "col", "end_line", "end_col")
    if body:
        members += ("instance", "module", "name")
    obj(value, members, "source")
    file_id = integer(value["file"], "source file")
    require(file_id < len(files), "source: unknown file")
    for name in ("line", "col", "end_line", "end_col"):
        integer(value[name], f"source {name}", minimum=1)
    require((value["line"], value["col"]) <= (value["end_line"], value["end_col"]), "source: range")
    lines = source_lines[file_id]
    where = "body" if body else "reason"
    for prefix in ("", "end_"):
        line = value[f"{prefix}line"]
        col = value[f"{prefix}col"]
        require(line <= len(lines), f"{where}: source line outside input")
        require(col <= len(lines[line - 1]) + 1, f"{where}: source columns outside input")
    if body:
        require(integer(value["instance"], "source instance", U32_MAX) == 0, "source: v1 instance")
        string(value["name"], "source name")
        require(value["name"] != "", "source: empty name")
        require(value["module"] == files[file_id]["module"], "source: module differs from file")
    return value


def work_bound(table, fir_size):
    """Return the W bound of a computation with this FIR size, saturated at u64."""
    return min(U64_MAX, table["w"] + table["w_scale"] * fir_size)


def is_work_bound(table, bound):
    """Return whether some FIR size gives this W bound."""
    if bound == U64_MAX:
        return True
    return bound >= table["w"] and (bound - table["w"]) % table["w_scale"] == 0


def reason(value, files, source_lines, failure=False, limits=None, work=None):
    """Validate a reason. `work` is the W bound of the ledger that refused, if one is known."""
    if value is None:
        return
    obj(value, ("stage", "code", "source", "limit"), "reason")
    choice(
        value["stage"],
        ("enumeration", "checking", "lowering", "verification", "tool") + ANALYSES,
        "reason stage",
    )
    choice(value["code"], FAILURE_CODES if failure else INCOMPLETE_CODES, "reason code")
    if value["source"] is not None:
        source_range(value["source"], files, source_lines)
    if value["limit"] is not None:
        limit = obj(value["limit"], ("category", "used", "bound"), "reason limit")
        choice(limit["category"], CATEGORIES, "limit category")
        integer(limit["used"], "limit used")
        integer(limit["bound"], "limit bound")
        if limit["category"] == "W" and work is not None:
            require(limit["bound"] == work, "reason: W bound differs from its ledger")
        elif limit["category"] == "W" and limits is not None:
            require(is_work_bound(limits, limit["bound"]), "reason: W bound of no FIR size")
        elif limits is not None:
            require(
                limit["bound"] == limits[limit["category"].lower()],
                "reason: production bound mismatch",
            )
        require(limit["used"] <= limit["bound"], "reason: use exceeds bound")
        require(
            value["code"] != "report_limit" or limit["category"] == "V",
            "reason: report limit category mismatch",
        )


def validate_boundary(value, enumeration, bodies, analyses, files, source_lines, limits):
    """Validate the process-exit boundary of one closure (toolchain.md 1.1)."""
    boundary = obj(
        value, ("kind", "leaves", "correspondence", "proof", "first_incomplete"), "boundary"
    )
    choice(boundary["kind"], BOUNDARY_KINDS, "boundary kind")
    integer(boundary["leaves"], "boundary leaves")
    correspondence = choice(boundary["correspondence"], BOUNDARY_STATUSES, "boundary status")
    proof = choice(boundary["proof"], BOUNDARY_STATUSES, "boundary status")
    # The boundary reads every body, so only a failed enumeration or body leaves it unexecuted.
    # Such a report fails the audit through its other members.
    executed = enumeration["complete"] and all(
        body["verification"] == "complete" for body in bodies
    )
    require(
        (correspondence == "unexecuted") == (proof == "unexecuted"),
        "boundary: half executed",
    )
    require(correspondence != "unexecuted" or not executed, "boundary: unexecuted after success")
    require(proof != "complete" or correspondence == "complete", "boundary: proof without facts")
    reason(boundary["first_incomplete"], files, source_lines, limits=limits)
    if proof == "incomplete":
        require(boundary["first_incomplete"] is not None, "boundary: missing reason")
        require(
            boundary["first_incomplete"]["stage"] == "process_exit", "boundary: reason stage"
        )
    else:
        require(boundary["first_incomplete"] is None, "boundary: reason without failure")
    closure = analyses[ANALYSES.index("process_exit")]
    require(
        closure["status"] != "complete" or proof == "complete",
        "boundary: process_exit closure claims the boundary",
    )


def named_rows(value, members, where):
    rows = array(value, where)
    require(len(rows) == len(ANALYSES), f"{where}: missing analysis rows")
    for name, row in zip(ANALYSES, rows, strict=True):
        obj(row, ("name",) + tuple(members), where)
        require(row["name"] == name, f"{where}: incorrect analysis order")
    return rows


def source_identity(body, paths):
    src = body["source"]
    return (
        paths[src["file"]],
        src["line"],
        src["col"],
        src["end_line"],
        src["end_col"],
        src["name"],
        src["instance"],
    )


def counter(values, statuses):
    counts = Counter(values)
    return {status: counts[status] for status in statuses}


def checked_sum(values, where):
    return integer(sum(values), where)


def derived_totals(bodies):
    total = {
        "bodies": len(bodies),
        "violations": checked_sum((body["violations"] for body in bodies), "total violations"),
    }
    for stage in STAGES:
        total[stage] = counter((body[stage] for body in bodies), STAGE_STATUSES)
    total["ownership"] = counter((body["ownership"] for body in bodies), PROOF_STATUSES)
    total["analyses"] = []
    for index, name in enumerate(ANALYSES):
        rows = [body["analyses"][index] for body in bodies]
        row_total = {
            "name": name,
            "violations": checked_sum((row["violations"] for row in rows), "analysis violations"),
        }
        for field, statuses in (
            ("correspondence", WORK_STATUSES),
            ("solver", WORK_STATUSES),
            ("proof", PROOF_STATUSES),
        ):
            row_total[field] = counter((row[field] for row in rows), statuses)
        total["analyses"].append(row_total)
    return total


def validate_totals(actual, expected, where="totals"):
    if type(expected) is dict:
        obj(actual, expected, where)
        for name, value in expected.items():
            validate_totals(actual[name], value, f"{where}.{name}")
    elif type(expected) is list:
        array(actual, where)
        require(len(actual) == len(expected), f"{where}: incorrect rows")
        for index, value in enumerate(expected):
            validate_totals(actual[index], value, f"{where}[{index}]")
    else:
        if type(expected) is int:
            integer(actual, where)
        else:
            string(actual, where)
        require(actual == expected, f"{where}: inconsistent total")


def validate_body(body, files, analyses, limits, source_lines):
    obj(
        body,
        ("source", "key", *STAGES, "analyses", "ownership", "violations", "first_incomplete"),
        "body",
    )
    source_range(body["source"], files, source_lines, body=True)
    key = body["key"]
    if key is not None:
        obj(key, ("module", "declaration", "instance"), "function key")
        for value in key.values():
            integer(value, "function key", U32_MAX)
        require(key["instance"] == body["source"]["instance"], "key: instance mismatch")
    blocked = False
    failed = False
    for stage in STAGES:
        status = choice(body[stage], STAGE_STATUSES, stage)
        if blocked:
            require(status == "unexecuted", f"{stage}: ran after failed prerequisite")
        failed |= status == "failed"
        blocked |= status != "complete"
    if body["verification"] == "complete":
        require(key is not None, "verified body: missing canonical key")
    rows = named_rows(
        body["analyses"], ("correspondence", "solver", "proof", "violations"), "body analyses"
    )
    for closure, row in zip(analyses, rows, strict=True):
        for field in ("correspondence", "solver"):
            choice(row[field], WORK_STATUSES, field)
        choice(row["proof"], PROOF_STATUSES, "proof")
        integer(row["violations"], "analysis violations")
        if blocked:
            require(
                all(row[field] == "unexecuted" for field in ("correspondence", "solver", "proof")),
                "analysis: ran after failed prerequisite",
            )
        elif closure["producer"] == "unavailable":
            require(
                (row["correspondence"], row["solver"], row["proof"])
                == ("unexecuted", "unexecuted", "incomplete"),
                "unavailable producer: false proof",
            )
        require(
            not row["violations"] or row["proof"] in ("violated", "failed"),
            "analysis: violations contradict proof",
        )
        require(row["proof"] != "violated" or row["violations"] > 0, "analysis: missing violation")
        if row["proof"] in ("complete", "violated"):
            require(
                row["correspondence"] != "unexecuted" and row["solver"] != "unexecuted",
                "analysis: unexecuted stage supplies proof",
            )
        if row["solver"] == "complete":
            require(
                row["correspondence"] in ("complete", "incomplete"), "solver: no correspondence"
            )
        if "failed" in (row["correspondence"], row["solver"]):
            require(row["proof"] == "failed", "analysis: failure hidden by proof")
    violations = checked_sum((row["violations"] for row in rows), "body violations")
    require(integer(body["violations"], "body violations") == violations, "body: violation total")
    precedence = ("failed", "violated", "incomplete", "unexecuted", "complete")
    ownership = (
        "failed"
        if failed
        else next(status for status in precedence if any(row["proof"] == status for row in rows))
    )
    require(body["ownership"] == ownership, "body: incorrect ownership precedence")
    reason(body["first_incomplete"], files, source_lines, limits=limits)
    if any(row["proof"] == "incomplete" for row in rows):
        require(body["first_incomplete"] is not None, "body: missing incomplete reason")


def validate_budget_outcomes(report):
    key_names = ("module", "declaration", "instance")
    by_key = {
        tuple(body["key"][name] for name in key_names): body
        for body in report["bodies"]
        if body["key"] is not None
    }
    records = [(report["first_incomplete"], None), (report["failure"], None)]
    records.extend((body["first_incomplete"], body) for body in report["bodies"])
    records.extend(
        (
            meter["first_refusal"],
            by_key[tuple(meter["owner"][name] for name in key_names)]
            if meter["owner"] is not None
            else None,
        )
        for meter in report["meters"]
    )
    for refusal, body in records:
        if refusal is None:
            continue
        category = refusal["limit"]["category"] if refusal["limit"] is not None else None
        work = category == "W" or (refusal["code"] == "work_limit" and category is None)
        rendering = category == "V" or (refusal["code"] == "report_limit" and category is None)
        if not (work or rendering):
            continue
        require(report["exit_status"] != 0, "budget refusal: accepted verdict")
        require(report["first_incomplete"] is not None, "budget refusal: missing report reason")
        if body is not None:
            require(body["first_incomplete"] is not None, "budget refusal: missing body reason")
        if work and refusal["stage"] == "enumeration":
            require(not report["enumeration"]["complete"], "work refusal: complete enumeration")
        if work and refusal["stage"] in ANALYSES:
            index = ANALYSES.index(refusal["stage"])
            require(
                report["analyses"][index]["status"] in ("incomplete", "failed"),
                "work refusal: complete closure outcome",
            )
            if body is not None:
                row = body["analyses"][index]
                require(
                    (row["correspondence"], row["solver"]) != ("complete", "complete"),
                    "work refusal: complete affected computation",
                )
                require(
                    row["proof"] in ("incomplete", "violated", "failed"),
                    "work refusal: complete or absent affected proof",
                )
        if work and body is not None and refusal["stage"] in STAGES:
            require(body[refusal["stage"]] != "complete", "work refusal: complete affected stage")
        if refusal["stage"] == "tool":
            require(
                report["exit_status"] == 2
                and report["failure"] is not None
                and report["failure"]["code"] == "tool_failure",
                "budget refusal: tool failure requires exit 2",
            )


def validate_report(
    report,
    *,
    argv,
    cwd,
    root,
    target,
    cfg,
    checkout,
    standard,
    exit_status,
    candidates,
    limits=None,
    compiler_version=None,
):
    """Validate one closure without combining its numeric keys with another closure."""
    obj(
        report,
        (
            "kind",
            "version",
            "complete",
            "compiler_version",
            "invocation",
            "files",
            "enumeration",
            "analyses",
            "limits",
            "meters",
            "bodies",
            "totals",
            "boundary",
            "first_incomplete",
            "failure",
            "verdict",
            "exit_status",
        ),
        "report",
    )
    require(report["kind"] == "fort-ownership-report", "report: incorrect kind")
    require(
        integer(report["version"], "report version") == REPORT_VERSION, "report: unknown version"
    )
    require(boolean(report["complete"], "report complete"), "report: truncated evidence")
    require(string(report["compiler_version"], "compiler version") != "", "compiler: empty version")
    if compiler_version is not None:
        require(report["compiler_version"] == compiler_version, "compiler: version mismatch")
    invocation = obj(
        report["invocation"],
        ("entry", "cwd", "argv", "target", "configuration", "mode"),
        "invocation",
    )
    require(array(invocation["argv"], "invocation argv") == argv[1:], "invocation: argv mismatch")
    require(resolve_path(invocation["cwd"], cwd) == Path(cwd).resolve(), "invocation: cwd mismatch")
    require(
        resolve_path(invocation["entry"], cwd) == (Path(checkout) / root).resolve(),
        "invocation: entry mismatch",
    )
    require(invocation["target"] == target, "invocation: target mismatch")
    require(configuration(invocation["configuration"]) == cfg, "invocation: configuration mismatch")
    mode = obj(invocation["mode"], ("check", "release", "no_bounds_check"), "mode")
    for name, flag in (
        ("check", "--check"),
        ("release", "--release"),
        ("no_bounds_check", "--no-bounds-check"),
    ):
        require(
            boolean(mode[name], f"mode {name}") == (flag in argv[1:]), "invocation: mode mismatch"
        )
    files = array(report["files"], "files")
    paths = []
    file_paths = []
    source_lines = []
    inputs = []
    for index, row in enumerate(files):
        obj(row, ("id", "path", "module"), "file")
        require(integer(row["id"], "file id") == index, "files: IDs have gaps")
        string(row["module"], "file module")
        absolute = str(resolve_path(row["path"], cwd))
        require(absolute in candidates, "file: absent from pre-invocation snapshot")
        path = normalize(row["path"], cwd, checkout, standard)
        require(path not in paths, "files: duplicate normalized identity")
        paths.append(path)
        file_paths.append(absolute)
        source_lines.append(candidates[absolute].split(b"\n"))
        inputs.append({"path": path, "sha256": digest(candidates[absolute])})
    require(root in paths, "files: missing root")
    enumeration = obj(
        report["enumeration"],
        ("complete", "selected_bodies", "inactive_declarations"),
        "enumeration",
    )
    boolean(enumeration["complete"], "enumeration complete")
    if enumeration["complete"]:
        runtime = str((Path(standard) / "rt.ft").resolve())
        runtime_identity = normalize(runtime, cwd, checkout, standard)
        require(
            (runtime, runtime_identity) in zip(file_paths, paths, strict=True),
            "enumeration: missing selected runtime",
        )
    integer(enumeration["selected_bodies"], "selected bodies")
    integer(enumeration["inactive_declarations"], "inactive declarations")
    analyses = named_rows(report["analyses"], ("producer", "status"), "closure analyses")
    for row in analyses:
        choice(row["producer"], ("integrated", "unavailable"), "producer")
        choice(row["status"], WORK_STATUSES, "closure status")
        require(
            row["producer"] != "unavailable" or row["status"] == "incomplete",
            "unavailable producer: false closure outcome",
        )
    table = obj(report["limits"], LIMIT_MEMBERS, "limits")
    require(
        integer(table["version"], "limits version") == LIMIT_VERSION, "limits: unsupported version"
    )
    for category in CATEGORIES:
        integer(table[category.lower()], "production bound", minimum=1)
    integer(table["w_scale"], "production W scale", minimum=1)
    if limits is not None:
        require(table == limits, "limits: differs from production table")
    bodies = array(report["bodies"], "bodies")
    identities = set()
    keys = set()
    module_names = {}
    declaration_keys = {}
    for body in bodies:
        validate_body(body, files, analyses, table, source_lines)
        identity = source_identity(body, paths)
        require(identity not in identities, "bodies: duplicate source identity")
        identities.add(identity)
        src = body["source"]
        lines = source_lines[src["file"]]
        require(src["line"] == src["end_line"], "body: name range spans lines")
        line = lines[src["line"] - 1]
        require(
            src["col"] < src["end_col"] <= len(line) + 1,
            "body: source columns outside input",
        )
        require(
            line[src["col"] - 1 : src["end_col"] - 1] == src["name"].encode("utf-8"),
            "body: source name range mismatch",
        )
        if body["key"] is not None:
            key = tuple(body["key"][name] for name in ("module", "declaration", "instance"))
            require(key not in keys, "bodies: duplicate canonical key")
            keys.add(key)
            require(
                key[0] not in module_names or module_names[key[0]] == src["module"],
                "key: inconsistent module identity",
            )
            module_names[key[0]] = src["module"]
            declaration_keys.setdefault(key[0], []).append(key[1])
    for values in declaration_keys.values():
        require(values == sorted(set(values)), "keys: declaration order")
    ordered_keys = [
        tuple(body["key"][name] for name in ("module", "declaration", "instance"))
        for body in bodies
        if body["key"] is not None
    ]
    require(ordered_keys == sorted(ordered_keys), "keys: body order")
    require(enumeration["selected_bodies"] == len(bodies), "enumeration: body count mismatch")
    graph_sizes = []
    fills_sizes = []
    body_sizes = 0
    service_sizes = {}
    # The producer analyses keep one ledger for each verified body. A flow analysis also keeps
    # a classification ledger of the same FIR size when the body needs one.
    ledger_sizes = {name: {} for name in BODY_LEDGERS}
    classification_sizes = {name: {} for name in CLASSIFICATION_LEDGERS.values()}
    component_sizes = {name: {} for name in COMPONENT_LEDGERS}
    for index, meter in enumerate(array(report["meters"], "meters")):
        obj(meter, ("id", "name", "owner", "fir_size", "counts", "first_refusal"), "meter")
        require(integer(meter["id"], "meter id") == index, "meters: IDs have gaps")
        meter_bounds = dict(table)
        meter_bounds["w"] = work_bound(table, integer(meter["fir_size"], "meter FIR size"))
        choice(
            meter["name"],
            METER_NAMES,
            "meter name",
        )
        if meter["name"] == "graph_private":
            graph_sizes.append(meter["fir_size"])
        elif meter["name"] == "process_exit_fills":
            require(meter["owner"] is None, "process exit fills ledger: owner")
            fills_sizes.append(meter["fir_size"])
        elif meter["name"] == "services":
            body_sizes += meter["fir_size"]
        owner_key = None
        if meter["owner"] is not None:
            owner = obj(meter["owner"], ("module", "declaration", "instance"), "meter owner")
            owner_key = tuple(
                integer(owner[name], "meter owner key", U32_MAX)
                for name in ("module", "declaration", "instance")
            )
            require(owner_key in keys, "meter: unknown owner")
        # A local, stored or raw ledger is the computation of one body, beside its service
        # ledger. A classification ledger is the second run of a flow, when it needed one.
        if meter["name"] in BODY_LEDGERS:
            label = LEDGER_LABELS[meter["name"]]
            sizes = ledger_sizes[meter["name"]]
            require(owner_key is not None, f"{label} ledger: missing owner")
            require(owner_key not in sizes, f"{label} ledger: duplicate owner")
            sizes[owner_key] = meter["fir_size"]
        elif meter["name"] in CLASSIFICATION_LEDGERS:
            flow = CLASSIFICATION_LEDGERS[meter["name"]]
            label = "classification" if flow == "local" else "stored classification"
            require(owner_key is not None, f"{label} ledger: missing owner")
            require(owner_key not in classification_sizes[flow], f"{label} ledger: duplicate owner")
            classification_sizes[flow][owner_key] = meter["fir_size"]
        elif meter["name"] in COMPONENT_LEDGERS:
            sizes = component_sizes[meter["name"]]
            require(owner_key is not None, "calls_heap ledger: missing owner")
            require(owner_key not in sizes, "calls_heap ledger: duplicate owner")
            sizes[owner_key] = meter["fir_size"]
        elif meter["name"] == "services" and owner_key is not None:
            service_sizes[owner_key] = meter["fir_size"]
        count_keys = set()
        for count in array(meter["counts"], "meter counts"):
            obj(count, ("category", "scope", "used", "bound"), "meter count")
            choice(count["category"], CATEGORIES, "meter category")
            choice(count["scope"], SCOPES, "meter scope")
            require(
                count["scope"] == CATEGORY_SCOPES[count["category"]],
                "meter: category scope mismatch",
            )
            require(
                meter["name"] != "graph_private" or count["category"] == "W",
                "graph private ledger: unsupported category",
            )
            require(
                meter["name"] != "process_exit_fills" or count["category"] == "W",
                "process exit fills ledger: unsupported category",
            )
            count_key = (count["category"], count["scope"])
            require(count_key not in count_keys, "meter: duplicate category and scope")
            count_keys.add(count_key)
            integer(count["used"], "meter used")
            require(
                integer(count["bound"], "meter bound") == meter_bounds[count["category"].lower()],
                "meter: bound mismatch",
            )
            require(count["used"] <= count["bound"], "meter: use exceeds bound")
        reason(
            meter["first_refusal"],
            files,
            source_lines,
            limits=meter_bounds,
            work=meter_bounds["w"],
        )
        refusal = meter["first_refusal"]
        if meter["name"] == "graph_private" and refusal is not None:
            require(
                refusal["code"] != "report_limit"
                and (refusal["limit"] is None or refusal["limit"]["category"] == "W"),
                "graph private ledger: unsupported refusal category",
            )
    for analysis in BODY_LEDGERS:
        label = LEDGER_LABELS[analysis]
        for owner_key, size in ledger_sizes[analysis].items():
            require(
                service_sizes.get(owner_key) == size,
                f"{label} ledger: FIR size differs from the service ledger of its body",
            )
        for owner_key, size in classification_sizes.get(analysis, {}).items():
            require(
                ledger_sizes[analysis].get(owner_key) == size,
                "classification ledger: FIR size differs from the local ledger of its body"
                if analysis == "local"
                else "stored classification ledger: FIR size differs from the stored ledger of"
                " its body",
            )
    # The graph build covers each verified body, and each verified body has one ledger.
    require(len(graph_sizes) <= 1, "meters: two graph ledgers")
    require(
        not graph_sizes or graph_sizes[0] == body_sizes,
        "meters: graph FIR size differs from the body ledgers",
    )
    # The call-graph step of the process-exit boundary covers each body with a process_exit
    # ledger.
    require(len(fills_sizes) <= 1, "meters: two process exit fills ledgers")
    require(
        not fills_sizes or fills_sizes[0] == sum(ledger_sizes["process_exit"].values()),
        "meters: process exit fills FIR size differs from the process_exit ledgers",
    )
    # The components of the graph partition its bodies. Each calls_heap solver solves each
    # component once, so the FIR sizes of its ledgers sum to the graph FIR size. A ledger is
    # the computation of the component of its owner, whose FIR size holds that of the owner.
    for name, sizes in component_sizes.items():
        require(
            not sizes or (graph_sizes and sum(sizes.values()) == graph_sizes[0]),
            "calls_heap ledger: FIR sizes differ from the graph ledger",
        )
        for owner_key, size in sizes.items():
            require(
                size >= service_sizes.get(owner_key, U64_MAX),
                "calls_heap ledger: FIR size below its owner",
            )
    require(
        bool(component_sizes["calls_summary"]) == bool(component_sizes["calls_heap"]),
        "calls_heap ledger: one solver without ledgers",
    )
    totals = derived_totals(bodies)
    validate_totals(report["totals"], totals)
    validate_boundary(report["boundary"], enumeration, bodies, analyses, files, source_lines, table)
    reason(report["first_incomplete"], files, source_lines, limits=table)
    reason(report["failure"], files, source_lines, failure=True, limits=table)
    # Version 3 integrates the local analysis, version 4 the raw analysis and version 5 the
    # stored-borrow analysis: each verified body has one ledger of each.
    verified = {
        tuple(body["key"][name] for name in ("module", "declaration", "instance"))
        for body in bodies
        if body["verification"] == "complete"
    }
    for analysis in BODY_LEDGERS:
        label = LEDGER_LABELS[analysis]
        require(
            analyses[ANALYSES.index(analysis)]["producer"] == "integrated",
            f"{label} analysis: producer is not integrated",
        )
        require(
            set(ledger_sizes[analysis]) == verified,
            f"{label} ledger: each verified body needs exactly one {label} ledger",
        )
    if any(row["status"] == "incomplete" for row in analyses) or any(
        body["first_incomplete"] is not None for body in bodies
    ):
        require(report["first_incomplete"] is not None, "report: missing first incomplete reason")
    require(
        integer(report["exit_status"], "report exit", maximum=2) == exit_status, "exit: mismatch"
    )
    require(
        report["verdict"] == {0: "accepted", 1: "rejected", 2: "failed"}.get(exit_status),
        "verdict: mismatch",
    )
    validate_budget_outcomes(report)
    accepted = (
        enumeration["complete"]
        and all(row["status"] == "complete" for row in analyses)
        and all(body["ownership"] == "complete" for body in bodies)
        and not totals["violations"]
        and (report["failure"] is None and report["first_incomplete"] is None)
    )
    require(exit_status != 0 or accepted, "accepted: incomplete ownership proof")
    require(exit_status != 1 or not accepted, "rejected: no rejection evidence")
    failed_stages = (
        any(body[stage] == "failed" for body in bodies for stage in STAGES)
        or any(row["status"] == "failed" for row in analyses)
        or any(
            "failed" in (row["correspondence"], row["solver"], row["proof"])
            for body in bodies
            for row in body["analyses"]
        )
    )
    require(
        not (failed_stages or exit_status == 2) or report["failure"] is not None,
        "report: missing failure reason",
    )
    if report["failure"] is not None:
        expected_exit = (
            1 if report["failure"]["code"] in ("source_error", "unsupported_lowering") else 2
        )
        require(exit_status == expected_exit, "failure: exit status mismatch")
    internal_body_failure = any(
        row["proof"] == "failed" or row["correspondence"] == "failed" or row["solver"] == "failed"
        for body in bodies
        for row in body["analyses"]
    )
    if (
        internal_body_failure
        or any(body["verification"] == "failed" for body in bodies)
        or any(row["status"] == "failed" for row in analyses)
    ):
        require(exit_status == 2, "internal failure: expected exit 2")
    if not enumeration["complete"]:
        raise FailedReportAttempt("enumeration: incomplete", inputs, identities, len(bodies))
    if exit_status not in (0, 1):
        raise FailedReportAttempt(
            "audit: compiler exit 2 or abnormal termination", inputs, identities, len(bodies)
        )
    return inputs, identities, len(bodies)


def production_limits(checkout):
    path = Path(checkout) / "src/fort/ownership_limits.ft"
    api = Path(checkout) / "src/fort/ownership_api.ft"
    try:
        limit_bytes = path.read_bytes()
        api_bytes = api.read_bytes()
        for source, data in ((path, limit_bytes), (api, api_bytes)):
            committed = subprocess.check_output(
                [
                    "git",
                    "-C",
                    str(checkout),
                    "show",
                    f"HEAD:{source.relative_to(checkout).as_posix()}",
                ]
            )
            require(data == committed, "production limits: source differs from git bytes")
        text = limit_bytes.decode()
        version_text = api_bytes.decode()
    except (OSError, subprocess.CalledProcessError, UnicodeError) as error:
        raise InvalidEvidence(f"production limits: {error}") from error
    values = {"version": LIMIT_VERSION}
    require(
        re.findall(r"^u32 LIMIT_VERSION = ([0-9]+);$", version_text, re.M) == [str(LIMIT_VERSION)],
        "production limits: unsupported version",
    )
    for member, name in [(c.lower(), c) for c in CATEGORIES] + [("w_scale", "W_SCALE")]:
        matches = re.findall(rf"^u64 PRODUCTION_{name} = ([0-9]+);$", text, re.M)
        require(len(matches) == 1, "production limits: missing or duplicate declaration")
        values[member] = integer(int(matches[0]), "production bound", minimum=1)
    return {member: values[member] for member in LIMIT_MEMBERS}


def provenance(checkout, compiler, compiler_checkout, provenance_path):
    source_revision = revision(git(checkout, "rev-parse", "HEAD"), "source revision")
    compiler_revision = revision(git(compiler_checkout, "rev-parse", "HEAD"), "compiler revision")
    value, _ = read_json(provenance_path)
    obj(value, ("revision", "sha256"), "compiler provenance")
    require(
        revision(value["revision"], "build revision") == compiler_revision,
        "compiler provenance: revision mismatch",
    )
    try:
        binary = Path(compiler).read_bytes()
    except OSError as error:
        raise InvalidEvidence(f"compiler bytes: {error}") from error
    binary_hash = digest(binary)
    require(
        hash_string(value["sha256"], "build digest") == binary_hash,
        "compiler provenance: binary digest mismatch",
    )
    # Tracked changes have no matching revision. Untracked standard roots remain valid inputs.
    for root in {str(Path(checkout).resolve()), str(Path(compiler_checkout).resolve())}:
        require(
            git(root, "status", "--porcelain", "--untracked-files=no") == "",
            "provenance: tracked source changes",
        )
    return source_revision, compiler_revision, binary_hash, binary


def compiler_version(compiler):
    try:
        result = subprocess.run(
            [str(compiler), "--version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        require(result.returncode == 0, "compiler: --version failed")
        version = result.stdout.decode("utf-8").strip()
        require(version != "", "compiler: invalid --version output")
        return version
    except (OSError, UnicodeError) as error:
        raise InvalidEvidence(f"compiler version: {error}") from error


def command(compiler, root, report_path, target, standard, search_roots, cfg, release, no_bounds):
    argv = [
        str(compiler),
        "--check",
        "--ownership-check",
        "--ownership-report",
        str(report_path),
        "--target",
        target,
        "--std-dir",
        str(standard),
    ]
    for search in search_roots:
        argv.extend(("-I", str(search)))
    for assignment in cfg:
        argv.extend(("--cfg", assignment))
    if release:
        argv.append("--release")
    if no_bounds:
        argv.append("--no-bounds-check")
    argv.append(root)
    return argv


def audit_totals(attempts, body_identities, context_bodies):
    files = {row["path"] for attempt in attempts for row in attempt["inputs"]}
    return {
        "root_attempts": len(attempts),
        "unique_files": len(files),
        "unique_source_bodies": len(body_identities),
        "context_bodies": context_bodies,
    }


def run_audit(
    *,
    checkout,
    compiler,
    compiler_checkout,
    compiler_provenance,
    output,
    target,
    standard,
    search_roots=(),
    cfg=(),
    release=False,
    no_bounds=False,
):
    """Keep all attempted roots. Mark the attestation incomplete when any evidence fails."""
    checkout = Path(checkout).resolve()
    compiler = Path(compiler).resolve()
    compiler_checkout = Path(compiler_checkout).resolve()
    standard = Path(standard).resolve()
    search_roots = [Path(root).resolve() for root in search_roots]
    paths = inventory(checkout)
    source_rev, compiler_rev, binary_hash, binary = provenance(
        checkout, compiler, compiler_checkout, compiler_provenance
    )
    version = compiler_version(compiler)
    require(compiler.read_bytes() == binary, "compiler: changed during version query")
    effective = effective_configuration(target, cfg)
    limits = production_limits(compiler_checkout)
    input_roots = [checkout / Path(root).parent for root in paths] + search_roots + [standard]
    audit_inputs = snapshot(input_roots)
    output = Path(output).resolve()
    require(not output.exists(), "output: requires a fresh directory")
    output.mkdir(parents=True)
    attestation = {
        "kind": "fort-ownership-audit",
        "version": 1,
        "complete": False,
        "source_revision": source_rev,
        "compiler_revision": compiler_rev,
        "compiler_sha256": binary_hash,
        "target": target,
        "configuration": effective,
        "inventory": paths,
        "attempts": [],
        "totals": {},
    }
    errors = []
    identities = set()
    context_bodies = 0
    for index, root in enumerate(root_order(paths)):
        report_path = output / f"report-{index:04d}.json"
        argv = command(
            compiler, root, report_path, target, standard, search_roots, cfg, release, no_bounds
        )
        attempt = {
            "root": root,
            "cwd": str(checkout),
            "argv": argv,
            "exit_status": 2,
            "report_path": str(report_path),
            "report_sha256": None,
            "inputs": [],
        }
        attestation["attempts"].append(attempt)
        candidates = {}
        try:
            require(not os.path.lexists(report_path), "report: stale path before invocation")
            require(compiler.read_bytes() == binary, "compiler: changed before invocation")
            roots = [checkout / Path(root).parent, *search_roots, standard]
            require(snapshot(input_roots) == audit_inputs, "inputs: changed since audit snapshot")
            candidates = snapshot(roots)
            atomic_json(
                output / f"inputs-{index:04d}.json",
                [
                    {"path": path, "sha256": digest(data)}
                    for path, data in sorted(candidates.items())
                ],
            )
            result = subprocess.run(
                argv, cwd=checkout, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
            )
            attempt["exit_status"] = result.returncode
            (output / f"stdout-{index:04d}.txt").write_bytes(result.stdout)
            (output / f"stderr-{index:04d}.txt").write_bytes(result.stderr)
            require(not report_path.is_symlink(), "report: symbolic link destination")
            report, report_bytes = read_json(report_path)
            attempt["report_sha256"] = digest(report_bytes)
            # Keep closure inputs even if another report field fails validation.
            if type(report) is dict and type(report.get("files")) is list:
                for row in report["files"]:
                    if type(row) is dict and type(row.get("path")) is str:
                        absolute = str(resolve_path(row["path"], checkout))
                        if absolute in candidates:
                            attempt["inputs"].append(
                                {
                                    "path": normalize(absolute, checkout, checkout, standard),
                                    "sha256": digest(candidates[absolute]),
                                }
                            )
            require(compiler.read_bytes() == binary, "compiler: changed during invocation")
            require(
                snapshot(roots) == candidates and snapshot(input_roots) == audit_inputs,
                "inputs: changed during invocation",
            )
            inputs, body_ids, count = validate_report(
                report,
                argv=argv,
                cwd=checkout,
                root=root,
                target=target,
                cfg=effective,
                checkout=checkout,
                standard=standard,
                exit_status=result.returncode,
                candidates=candidates,
                limits=limits,
                compiler_version=version,
            )
            attempt["inputs"] = inputs
            identities.update(body_ids)
            context_bodies = checked_sum((context_bodies, count), "context bodies")
        except FailedReportAttempt as error:
            attempt["inputs"] = error.inputs
            identities.update(error.identities)
            context_bodies = checked_sum((context_bodies, error.bodies), "context bodies")
            errors.append(f"{root}: {error}")
        except (InvalidEvidence, OSError) as error:
            errors.append(f"{root}: {error}")
            try:
                if report_path.is_file():
                    attempt["report_sha256"] = digest(report_path.read_bytes())
            except OSError:
                pass
        finally:
            attestation["totals"] = audit_totals(
                attestation["attempts"], identities, context_bodies
            )
            atomic_json(output / "audit.json", attestation)
    try:
        after = provenance(checkout, compiler, compiler_checkout, compiler_provenance)
        require(
            after[:3] == (source_rev, compiler_rev, binary_hash), "provenance: changed during audit"
        )
        require(inventory(checkout) == paths, "inventory: changed during audit")
        require(snapshot(input_roots) == audit_inputs, "inputs: changed during audit")
    except (InvalidEvidence, OSError) as error:
        errors.append(str(error))
    attestation["complete"] = not errors
    atomic_json(output / "audit.json", attestation)
    (output / "errors.txt").write_text("".join(f"{error}\n" for error in errors), encoding="utf-8")
    return attestation, errors


def parse_attempt_argv(argv, checkout, root, report_path):
    array(argv, "attempt argv")
    for arg in argv:
        string(arg, "argument")
    require(len(argv) >= 10, "attempt: truncated argv")
    require(
        argv[1:4] == ["--check", "--ownership-check", "--ownership-report"], "attempt: selection"
    )
    require(argv[4] == report_path, "attempt: report argument mismatch")
    require(argv[-1] == root, "attempt: root argument mismatch")
    require(argv[5] == "--target" and argv[7] == "--std-dir", "attempt: missing target or standard")
    target, standard = argv[6], resolve_path(argv[8], checkout)
    searches, cfg = [], []
    release, no_bounds = False, False
    index = 9
    while index < len(argv) - 1:
        flag = argv[index]
        if flag in ("-I", "--cfg"):
            require(index + 1 < len(argv) - 1, "attempt: missing option argument")
            if flag == "-I":
                searches.append(resolve_path(argv[index + 1], checkout))
            else:
                cfg.append(argv[index + 1])
            index += 2
        elif flag in ("--release", "--no-bounds-check"):
            require(
                not (release if flag == "--release" else no_bounds), "attempt: duplicate mode flag"
            )
            if flag == "--release":
                release = True
            else:
                no_bounds = True
            index += 1
        else:
            raise InvalidEvidence(f"attempt: unexpected option {flag!r}")
    require(
        argv
        == command(argv[0], root, report_path, target, standard, searches, cfg, release, no_bounds),
        "attempt: noncanonical argv",
    )
    return target, standard, searches, effective_configuration(target, cfg)


def validate_audit(path, *, checkout, compiler, compiler_checkout, compiler_provenance):
    """Validate retained reports, input bytes, inventory, and compiler build provenance."""
    checkout = Path(checkout).resolve()
    compiler = Path(compiler).resolve()
    audit, _ = read_json(path)
    obj(
        audit,
        (
            "kind",
            "version",
            "complete",
            "source_revision",
            "compiler_revision",
            "compiler_sha256",
            "target",
            "configuration",
            "inventory",
            "attempts",
            "totals",
        ),
        "audit",
    )
    require(audit["kind"] == "fort-ownership-audit", "audit: incorrect kind")
    require(integer(audit["version"], "audit version") == 1, "audit: unknown version")
    require(boolean(audit["complete"], "audit complete"), "audit: incomplete evidence")
    source_rev, compiler_rev, binary_hash, _ = provenance(
        checkout, compiler, compiler_checkout, compiler_provenance
    )
    require(
        revision(audit["source_revision"], "source revision") == source_rev,
        "source: revision mismatch",
    )
    require(
        revision(audit["compiler_revision"], "compiler revision") == compiler_rev,
        "compiler: revision mismatch",
    )
    require(
        hash_string(audit["compiler_sha256"], "compiler digest") == binary_hash,
        "compiler: digest mismatch",
    )
    version = compiler_version(compiler)
    paths = inventory(checkout)
    require(array(audit["inventory"], "inventory") == paths, "audit: incomplete inventory")
    cfg = configuration(audit["configuration"])
    string(audit["target"], "target")
    attempts = array(audit["attempts"], "attempts")
    require(len(attempts) == len(paths), "audit: missing root attempts")
    identities = set()
    context_bodies = 0
    seen_context = None
    limits = production_limits(compiler_checkout)
    for index, (root, attempt) in enumerate(zip(root_order(paths), attempts, strict=True)):
        obj(
            attempt,
            ("root", "cwd", "argv", "exit_status", "report_path", "report_sha256", "inputs"),
            "attempt",
        )
        require(attempt["root"] == root, "attempt: missing, duplicate, or unordered root")
        require(resolve_path(attempt["cwd"], checkout) == checkout, "attempt: cwd mismatch")
        expected_path = str(Path(path).resolve().parent / f"report-{index:04d}.json")
        require(attempt["report_path"] == expected_path, "attempt: stale or unexpected report path")
        target, standard, searches, actual_cfg = parse_attempt_argv(
            attempt["argv"], checkout, root, expected_path
        )
        require(
            resolve_path(attempt["argv"][0], checkout) == compiler,
            "attempt: compiler path mismatch",
        )
        require(
            target == audit["target"] and actual_cfg == cfg,
            "attempt: target or configuration mismatch",
        )
        context = (
            standard,
            tuple(searches),
            "--release" in attempt["argv"],
            "--no-bounds-check" in attempt["argv"],
        )
        if seen_context is None:
            seen_context = context
        else:
            require(context == seen_context, "attempt: inconsistent context")
        integer(attempt["exit_status"], "attempt exit", maximum=2)
        require(not Path(expected_path).is_symlink(), "report: symbolic link destination")
        report, data = read_json(expected_path)
        require(
            hash_string(attempt["report_sha256"], "report digest") == digest(data),
            "report: digest mismatch",
        )
        candidates = snapshot([checkout / Path(root).parent, *searches, standard])
        inputs, body_ids, count = validate_report(
            report,
            argv=attempt["argv"],
            cwd=checkout,
            root=root,
            target=target,
            cfg=cfg,
            checkout=checkout,
            standard=standard,
            exit_status=attempt["exit_status"],
            candidates=candidates,
            limits=limits,
            compiler_version=version,
        )
        for row in array(attempt["inputs"], "inputs"):
            obj(row, ("path", "sha256"), "input")
            string(row["path"], "input path")
            hash_string(row["sha256"], "input digest")
        require(attempt["inputs"] == inputs, "inputs: bytes or closure mismatch")
        before, _ = read_json(Path(path).resolve().parent / f"inputs-{index:04d}.json")
        require(
            before
            == [
                {"path": key, "sha256": digest(value)} for key, value in sorted(candidates.items())
            ],
            "inputs: pre-invocation manifest mismatch",
        )
        identities.update(body_ids)
        context_bodies = checked_sum((context_bodies, count), "context bodies")
    validate_totals(
        audit["totals"], audit_totals(attempts, identities, context_bodies), "audit totals"
    )
    after = provenance(checkout, compiler, compiler_checkout, compiler_provenance)
    require(
        after[:3] == (source_rev, compiler_rev, binary_hash),
        "provenance: changed during validation",
    )
    return audit


# The scopes that a CI gate can enforce. Each names its analysis row.
ENFORCED_SCOPES = ("local", "stored_borrows", "raw", "calls_heap", "process_exit")


def scope_problems(report, scope, checkout):
    """Return the declared bodies of one report and the problems of its enforced scope.

    A body declares its obligations when the scope's correspondence is complete. A declared
    body needs complete proof. A violation in any body is a problem. The process-exit scope
    also declares the boundary when its correspondence is complete, and then needs its proof.
    """
    index = ANALYSES.index(scope)
    files = report["files"]
    declared = 0
    problems = []
    for body in report["bodies"]:
        row = body["analyses"][index]
        source = body["source"]
        path = Path(files[source["file"]]["path"])
        try:
            shown = path.resolve().relative_to(Path(checkout).resolve())
        except ValueError:
            shown = path
        where = f"{shown}:{source['line']}: {source['name']}"
        if row["violations"]:
            problems.append(f"{where}: {row['violations']} {scope} violation(s)")
        if row["correspondence"] == "complete":
            declared += 1
            if row["proof"] != "complete" and not row["violations"]:
                problems.append(f"{where}: incomplete declared {scope} obligation ({row['proof']})")
    boundary = report["boundary"]
    if scope == "process_exit" and boundary["correspondence"] == "complete":
        if boundary["proof"] != "complete":
            problems.append(f"{report['invocation']['entry']}: incomplete declared boundary")
    return declared, problems


def enforce_audit(path, *, scope, **common):
    """Validate the evidence, then reject violations and incomplete declared obligations.

    Results outside the declared scope stay informational. Acceptance here changes no
    compiler verdict and proves no complete ownership.
    """
    require(scope in ENFORCED_SCOPES, "enforce: unknown scope")
    validated = validate_audit(path, **common)
    bodies = 0
    declared = 0
    problems = []
    for attempt in validated["attempts"]:
        report, _ = read_json(attempt["report_path"])
        found, listed = scope_problems(report, scope, common["checkout"])
        bodies = checked_sum((bodies, len(report["bodies"])), "context bodies")
        declared = checked_sum((declared, found), "declared bodies")
        for problem in listed:
            if problem not in problems:
                problems.append(problem)
    summary = {
        "scope": scope,
        "context_bodies": bodies,
        "declared_context_bodies": declared,
        "problems": len(problems),
    }
    return summary, problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="action", required=True)
    for name in ("run", "validate", "enforce"):
        sub = subparsers.add_parser(name)
        sub.add_argument("--checkout", type=Path, required=True)
        sub.add_argument("--compiler", type=Path, required=True)
        sub.add_argument("--compiler-checkout", type=Path)
        sub.add_argument("--compiler-provenance", type=Path, required=True)
        if name == "enforce":
            sub.add_argument("--scope", choices=ENFORCED_SCOPES, required=True)
        if name in ("validate", "enforce"):
            sub.add_argument("attestation", type=Path)
        else:
            sub.add_argument("--output", type=Path, required=True)
            sub.add_argument("--target", required=True)
            sub.add_argument("--std-dir", type=Path, required=True)
            sub.add_argument("--search-root", type=Path, action="append")
            sub.add_argument("--cfg", action="append", default=[])
            sub.add_argument("--release", action="store_true")
            sub.add_argument("--no-bounds-check", action="store_true")
    options = parser.parse_args(argv)
    common = {
        "checkout": options.checkout,
        "compiler": options.compiler,
        "compiler_checkout": options.compiler_checkout or options.checkout,
        "compiler_provenance": options.compiler_provenance,
    }
    try:
        if options.action == "enforce":
            summary, problems = enforce_audit(options.attestation, scope=options.scope, **common)
            for problem in problems:
                print(problem, file=sys.stderr)
            print(json.dumps(summary, sort_keys=True))
            return 1 if problems else 0
        if options.action == "validate":
            audit = validate_audit(options.attestation, **common)
        else:
            audit, errors = run_audit(
                **common,
                output=options.output,
                target=options.target,
                standard=options.std_dir,
                search_roots=options.search_root
                if options.search_root is not None
                else [options.checkout / "src"],
                cfg=options.cfg,
                release=options.release,
                no_bounds=options.no_bounds_check,
            )
            if errors:
                for error in errors:
                    print(error, file=sys.stderr)
                return 1
        print(json.dumps(audit["totals"], sort_keys=True))
        return 0
    except (InvalidEvidence, OSError) as error:
        print(f"ownership audit: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
