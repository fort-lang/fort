#!/usr/bin/env python3
"""Count test lines per source line (spec/toolchain.md 7.6, D14.6).

Source lines are src/bootstrap/*.c and *.h, src/fort/*.ft and std/*.ft; test
lines are test/*.c, test/*.h, test/**/*.ft and test/lang/ffi/*.c. The standard
library is on the source side because it is hand-written code the project
ships and must test, and crediting it to the tests would raise the ratio for
writing library code (D14.6, amended by T-076). The runtime counts with it:
the runtime is std.rt, an ordinary module of std/*.ft, since T-091 deleted
runtime/fort_rt.c and the glob that named it (D13.1, D14.6 as T-088 amends
it). The editor extension is still on neither side. The target ratio is 3:1;
--min fails the run when the ratio is below the given value.

--since REF measures a branch instead of the repository: it counts the lines
a diff against REF adds and removes on each side, so a ticket answers for the
code it introduces rather than hiding behind the corpus already there. A
branch that adds no source line has no ratio and passes.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

SOURCE_GLOBS = (
    "src/bootstrap/*.c",
    "src/bootstrap/*.h",
    "src/fort/*.ft",
    "std/*.ft",
)
TEST_GLOBS = ("test/*.c", "test/*.h", "test/**/*.ft", "test/lang/ffi/*.c")
TARGET_RATIO = 3.0


def count_lines(path):
    """Return the number of lines in path, as wc -l counts them."""
    with open(path, "rb") as f:
        return sum(1 for _ in f)


def collect(root, globs):
    """Return the sorted list of files under root matching any of the globs."""
    files = set()
    for pattern in globs:
        files.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(files)


def total_lines(files):
    return sum(count_lines(p) for p in files)


def glob_to_regex(pattern):
    """Compile one of the globs above into a regex over a slash-separated path.

    pathlib.Path.full_match would do this, but it arrived in Python 3.13 and
    the guest runs 3.12. `**/` matches any number of directories, `*` and `?`
    match within one name.
    """
    out = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            out.append("(?:[^/]+/)*")
            i += 3
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("".join(out) + r"\Z")


def path_matches(path, patterns):
    """Whether the path matches any of the patterns."""
    return any(p.match(path) for p in patterns)


def diff_lines(root, ref, globs):
    """Return (added, removed) line counts of a diff against ref for the globs.

    git numstat reports added and removed per file; a file whose path matches
    none of the globs is skipped, so the two sides use the same rules as a
    whole-repository count.
    """
    out = subprocess.run(
        ["git", "diff", "--numstat", "--no-renames", ref + "...HEAD"],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    patterns = [glob_to_regex(g) for g in globs]
    added = removed = 0
    for line in out.splitlines():
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] == "-":
            continue
        if not path_matches(fields[2], patterns):
            continue
        added += int(fields[0])
        removed += int(fields[1])
    return added, removed


def ratio_of(test_lines, source_lines):
    """The ratio, or None when there is no source to measure against."""
    if source_lines == 0:
        return None
    return test_lines / source_lines


def format_ratio(ratio):
    return "n/a" if ratio is None else "%.2f" % ratio


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="repository root (default: the parent of tools/)",
    )
    parser.add_argument(
        "--min",
        type=float,
        default=None,
        metavar="RATIO",
        help="exit with status 1 when the ratio is below RATIO",
    )
    parser.add_argument(
        "--since",
        metavar="REF",
        default=None,
        help="measure the diff against REF instead of the whole repository",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true", help="list every counted file"
    )
    args = parser.parse_args(argv)

    if args.since is not None:
        return main_since(args)

    source_files = collect(args.root, SOURCE_GLOBS)
    test_files = collect(args.root, TEST_GLOBS)
    source_lines = total_lines(source_files)
    test_lines = total_lines(test_files)
    ratio = ratio_of(test_lines, source_lines)

    if args.verbose:
        for label, files in (("source", source_files), ("test", test_files)):
            for p in files:
                print("%-8s %6d %s" % (label, count_lines(p), p.relative_to(args.root)))
    print("source:   %d lines in %d files" % (source_lines, len(source_files)))
    print("tests:    %d lines in %d files" % (test_lines, len(test_files)))
    print("ratio:    %s (target %.1f:1)" % (format_ratio(ratio), TARGET_RATIO))

    if args.min is not None and ratio is not None and ratio < args.min:
        sys.stdout.flush()
        print("lines.py: ratio %s is below the minimum %.2f" % (format_ratio(ratio), args.min),
              file=sys.stderr)
        return 1
    return 0


def main_since(args):
    """Measure the branch's own diff against args.since."""
    source_added, source_removed = diff_lines(args.root, args.since, SOURCE_GLOBS)
    test_added, test_removed = diff_lines(args.root, args.since, TEST_GLOBS)
    source_net = source_added - source_removed
    test_net = test_added - test_removed
    ratio = ratio_of(test_net, source_net) if source_net > 0 else None

    print("source:   %+d lines (%d added, %d removed)" % (source_net, source_added,
                                                          source_removed))
    print("tests:    %+d lines (%d added, %d removed)" % (test_net, test_added, test_removed))
    print("ratio:    %s (target %.1f:1, against %s)" % (format_ratio(ratio), TARGET_RATIO,
                                                        args.since))
    if ratio is None:
        print("lines.py: no source lines added, so there is no ratio to meet")
        return 0
    if args.min is not None and ratio < args.min:
        sys.stdout.flush()
        print("lines.py: ratio %s is below the minimum %.2f" % (format_ratio(ratio), args.min),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
