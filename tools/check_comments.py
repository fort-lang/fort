#!/usr/bin/env python3
"""Reject block comments in the project's C sources.

fort and project C use `//` comments. This keeps the bootstrap transliterable into fort.
The tool scans C files under bootstrap/ and test/lang/ffi/. It tracks strings, characters,
escapes, and `//` comments. It reports each real `/*` opener as `<file>:<line>: block comment`.
It exits 1 for a problem and 0 otherwise.
"""

import argparse
import sys
from pathlib import Path

SOURCE_GLOBS = (
    "bootstrap/src/*.c",
    "bootstrap/src/*.h",
    "bootstrap/test/*.c",
    "bootstrap/test/common/*.h",
    "test/lang/ffi/*.c",
)


def collect(root, globs):
    """Return the sorted list of files under root matching any of the globs."""
    files = set()
    for pattern in globs:
        files.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(files)


def scan_text(text):
    """Return the problems in text as (line, message) pairs, in source order.

    The scan uses a small state machine. Outside a literal and a comment, `/*`
    starts a block comment. Inside a string or character literal, a backslash
    escapes the next character, and `/*` has no effect. Inside a `//` comment,
    only the newline matters. The scan also reports a literal interrupted by a
    newline or the text end. Otherwise, the remaining scan would be invalid.
    """
    problems = []
    line = 1
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch == "/" and text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if ch == "/" and text.startswith("/*", i):
            problems.append((line, "block comment"))
            i += 2
            continue
        if ch in ('"', "'"):
            i, line, problem = scan_literal(text, i, line)
            if problem is not None:
                problems.append(problem)
            continue
        i += 1
    return problems


def scan_literal(text, start, line):
    """Scan the literal opening at start; return (index after it, line, problem).

    The index is past the closing quote. It can also identify the newline or
    text end that interrupted the literal. In that case, problem gives the
    cause.
    """
    quote = text[start]
    kind = "string" if quote == '"' else "character"
    i = start + 1
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "\\":
            if i + 1 < n and text[i + 1] == "\n":
                line += 1
            i += 2
            continue
        if ch == quote:
            return i + 1, line, None
        if ch == "\n":
            return i, line, (line, f"unterminated {kind} literal")
        i += 1
    return n, line, (line, f"unterminated {kind} literal")


def check_file(path, display):
    """Print the problems of one file; return how many there were."""
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = scan_text(text)
    for line, message in problems:
        print(f"{display}:{line}: {message}")
    return len(problems)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", maxsplit=1)[0])
    parser.add_argument(
        "files", nargs="*", type=Path, help="the files to scan (default: the C sources)"
    )
    parser.add_argument(
        "--root",
        type=Path,
        default=Path.cwd(),
        help="repository root (default: the current directory)",
    )
    args = parser.parse_args(argv)

    files = args.files if args.files else collect(args.root, SOURCE_GLOBS)
    found = 0
    for path in files:
        display = path
        if not args.files:
            display = path.relative_to(args.root)
        found += check_file(path, display)
    if found > 0:
        print(f"{found} block comment problem(s); use // comments (D2.2)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
