#!/usr/bin/env python3
"""Count and hash all fort inputs read by the darwin gate."""

import hashlib
import os
from pathlib import Path


def sources(root):
    for directory in ("src", "std", "test", "spec", "notes", "tools", "editors"):
        base = root / directory
        if base.is_dir():
            yield from base.rglob("*.ft")
    for directory in ("build/darwin",):
        base = root / directory
        if not base.is_dir():
            raise SystemExit("darwin input manifest: missing %s" % base)
        yield from base.rglob("*.ft")
    # An ignored fort file outside build can still become an input.
    for path in root.rglob("*.ft"):
        rel = path.relative_to(root)
        if rel.parts[0] in ("build", ".git", ".worktrees"):
            continue
        yield path


def manifest(root):
    rows = []
    for path in sorted(set(sources(root))):
        if not path.is_file() and not path.is_symlink():
            continue
        rel = path.relative_to(root).as_posix()
        target = os.readlink(path) if path.is_symlink() else "-"
        content = hashlib.sha256(path.read_bytes()).hexdigest()
        rows.append("%s\t%s\t%s" % (rel, target, content))
    digest = hashlib.sha256(("\n".join(rows) + "\n").encode()).hexdigest()
    return rows, digest


def main():
    root = Path(__file__).resolve().parent.parent
    rows, digest = manifest(root)
    print("darwin .ft inputs: %d paths; SHA256 %s" % (len(rows), digest))
    for row in rows:
        print(row)


if __name__ == "__main__":
    main()
