#!/usr/bin/env python3
"""Check the source boundary of the ownership witness module."""

import argparse
from pathlib import Path
import re


ALLOWED_SUMMARY_FIELDS = {"returns", "interface", "required_some", "post"}
CONDITION_KINDS = {"call_returns", "check_passes", "nonnull", "input_branch", "input_alias"}


def fail(message):
    raise SystemExit("ownership_witness_lint: " + message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    source = args.root / "src/fort/ownership_witness.ft"
    heap = args.root / "src/fort/ownership_heap.ft"
    report = args.root / "src/fort/ownership_report.ft"
    text = source.read_text()
    if "import ownership_facts;" not in text:
        fail("the checker does not import ownership_facts")
    fields = set(re.findall(r"(?:input|s->input)->summary->([a-z_]+)", text))
    extra = fields - ALLOWED_SUMMARY_FIELDS
    if extra:
        fail("the checker reads other summary fields: " + ", ".join(sorted(extra)))
    match = re.search(r"enum condition_kind\s*\{([^}]*)\}", text, re.DOTALL)
    if not match:
        fail("the condition kind table is absent")
    kinds = {
        line.strip().rstrip(",") for line in match.group(1).splitlines() if line.strip()
    }
    if kinds != CONDITION_KINDS:
        fail("the condition kind table differs")
    if "import ownership_witness;" in heap.read_text():
        fail("ownership_heap imports ownership_witness")
    if report.read_text().count('"this path needs "') != 1:
        fail("the report needs one condition sentence pattern")
    print(
        f"ownership_witness_lint: {len(fields)} summary fields, {len(kinds)} condition kinds"
    )


if __name__ == "__main__":
    main()
