#!/usr/bin/env python3
"""tools/ownership_ir_compare.py: compare the IR of selected and unselected ownership builds.

usage: ownership_ir_compare.py --fort PATH --std-dir PATH [--mode NAME]... [--flag=ARG]...
                               <entry.ft>...

The ownership proof adds no runtime ownership check and no range check, and it changes no
representation (D17.14, D17.17, D19.8). So a program that the selected proof accepts has the IR
of the unselected build. For each entry and each mode, the tool runs two builds that differ only
in --ownership-check:

    fort --std-dir D [--ownership-check] <mode> <flags> -S -o <file> <entry>

The modes are `default` (no option), `release` (--release), `nobounds` (--no-bounds-check)
and `release-nobounds` (both). Without --mode the tool runs all four. Each --flag adds one
argument to both builds, after the mode. A --flag that selects the analysis or names its report
(--ownership-check, --ownership-report) is a usage error: it would make both builds selected,
and the tool would compare a selected build with itself.

The tool writes one line for each case, then a count:

    equal <entry> <mode>      the two IR files are equal byte for byte
    differ <entry> <mode>     the files differ; a unified diff follows
    rejected <entry> <mode>   the selected build exited 1 and wrote no IR; its stderr follows

A rejected case is no equality, because a selected build writes IR only when the proof accepts
the program (toolchain.md 1). Exit status: 0 when each case is equal, 1 when a case differs or
is rejected, 2 for a usage error, a failed unselected build, or a compiler status other than 0
or 1.
"""
import argparse
import difflib
import subprocess
import sys
import tempfile
from pathlib import Path

MODES = {"default": (), "release": ("--release",), "nobounds": ("--no-bounds-check",),
         "release-nobounds": ("--release", "--no-bounds-check")}
MODE_ORDER = ("default", "release", "nobounds", "release-nobounds")
# The options that select the analysis or name its report. Only the tool adds them.
SELECTION = ("--ownership-check", "--ownership-report")
# The diff lines that one differing case prints at most.
DIFF_LINES = 40


class ToolError(Exception):
    """A case that measures nothing: the inputs or the compiler failed."""


def build(options, entry, mode, selected, output):
    """Runs one -S build. Returns its status and stderr. A status above 1 is a tool error."""
    argv = [options.fort, "--std-dir", options.std_dir]
    if selected:
        argv.append("--ownership-check")
    argv.extend(MODES[mode])
    argv.extend(options.flag)
    argv.extend(("-S", "-o", str(output), str(entry)))
    try:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=options.timeout,
                                check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ToolError("%s %s: cannot run the compiler: %s" % (entry, mode, error)) from error
    which = "selected" if selected else "unselected"
    if result.returncode not in (0, 1) or (not selected and result.returncode != 0):
        raise ToolError("%s %s: the %s build exited %d\n%s"
                        % (entry, mode, which, result.returncode, result.stderr))
    if result.returncode == 0 and not output.is_file():
        raise ToolError("%s %s: the %s build exited 0 and wrote no IR" % (entry, mode, which))
    return result.returncode, result.stderr


def compare(options, entry, mode, directory):
    """Returns the verdict and the detail lines of one case."""
    unselected = directory / "unselected.ll"
    selected = directory / "selected.ll"
    build(options, entry, mode, False, unselected)
    status, stderr = build(options, entry, mode, True, selected)
    if status != 0:
        return "rejected", ["the selected build exited 1 and wrote no IR"], stderr.splitlines()
    before = unselected.read_bytes()
    after = selected.read_bytes()
    if before == after:
        return "equal", [], []
    diff = list(difflib.unified_diff(
        before.decode(errors="replace").splitlines(), after.decode(errors="replace").splitlines(),
        "unselected", "selected", lineterm=""))
    return "differ", [], diff[:DIFF_LINES]


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard root of both builds")
    parser.add_argument("--mode", action="append", choices=sorted(MODES),
                        help="a mode to compare; repeatable; default all four")
    parser.add_argument("--flag", action="append", default=[],
                        help="one more compiler argument for both builds; repeatable")
    parser.add_argument("--timeout", type=float, default=600, help="seconds for each build")
    parser.add_argument("entries", nargs="*", type=Path)
    options = parser.parse_args(argv)
    if not options.entries:
        parser.error("name at least one entry")
    for flag in options.flag:
        if flag in SELECTION or flag.startswith("--ownership-report="):
            parser.error("--flag=%s: the tool adds --ownership-check to one build only" % flag)
    modes = options.mode or list(MODE_ORDER)
    counts = {"equal": 0, "differ": 0, "rejected": 0}
    try:
        for entry in options.entries:
            for mode in modes:
                with tempfile.TemporaryDirectory(prefix="fort-ir-compare-") as directory:
                    verdict, detail, lines = compare(options, entry, mode, Path(directory))
                counts[verdict] += 1
                head = "%s %s %s" % (verdict, entry, mode)
                print(head + (": " + "; ".join(detail) if detail else ""))
                for line in lines:
                    print("    " + line)
    except ToolError as error:
        print("ownership_ir_compare: %s" % error, file=sys.stderr)
        return 2
    total = sum(counts.values())
    print("ownership_ir_compare: %d cases: %d equal, %d differ, %d rejected"
          % (total, counts["equal"], counts["differ"], counts["rejected"]))
    return 0 if counts["equal"] == total else 1


if __name__ == "__main__":
    sys.exit(main())
