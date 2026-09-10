#!/usr/bin/env python3
"""Count test lines per compiler line (notes/toolchain.md 7.6, D14.6).

Compiler lines are src/bootstrap/*.c and *.h plus src/fort/*.ft; test lines
are test/*.c, test/lang/**/*.ft and test/lang/ffi/*.c (test/*.h, the
framework, counts on neither side). The runtime and the standard library
count on neither side. The target ratio is 3:1; --min fails the run when the
ratio is below the given value.
"""

import argparse
import sys
from pathlib import Path

COMPILER_GLOBS = ("src/bootstrap/*.c", "src/bootstrap/*.h", "src/fort/*.ft")
TEST_GLOBS = ("test/*.c", "test/*.h", "test/lang/**/*.ft", "test/lang/ffi/*.c")
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


def ratio_of(test_lines, compiler_lines):
    """The ratio, or None when there is no compiler source to measure against."""
    if compiler_lines == 0:
        return None
    return test_lines / compiler_lines


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
        "--verbose", "-v", action="store_true", help="list every counted file"
    )
    args = parser.parse_args(argv)

    compiler_files = collect(args.root, COMPILER_GLOBS)
    test_files = collect(args.root, TEST_GLOBS)
    compiler_lines = total_lines(compiler_files)
    test_lines = total_lines(test_files)
    ratio = ratio_of(test_lines, compiler_lines)

    if args.verbose:
        for label, files in (("compiler", compiler_files), ("test", test_files)):
            for p in files:
                print("%-8s %6d %s" % (label, count_lines(p), p.relative_to(args.root)))
    print("compiler: %d lines in %d files" % (compiler_lines, len(compiler_files)))
    print("tests:    %d lines in %d files" % (test_lines, len(test_files)))
    print("ratio:    %s (target %.1f:1)" % (format_ratio(ratio), TARGET_RATIO))

    if args.min is not None and ratio is not None and ratio < args.min:
        sys.stdout.flush()
        print("lines.py: ratio %s is below the minimum %.2f" % (format_ratio(ratio), args.min),
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
