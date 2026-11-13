#!/usr/bin/env python3
"""Write the expected stdout of the run tests of the typed family.

Use `typed_pins.py --fort FORT [--std-dir DIR] [--cc CC] [--check] [-j N] [test ...]`. A test is
a file name of `test/fort/owner/typed/` without `.ft`. With no test the tool takes PINNED: the 8
parts and `cells_test`, whose lines a change of the prover moves.

For each test the tool compiles the file as test/lang/run_tests.py does, from `test/fort` with
the `//! flags:` of the file, and runs the program in an empty directory. Then it replaces the
`//|` lines after `//! stdout:` with the stdout of the run. It writes no file when a compile or
a run does not exit 0: a part with a failure of no class exits 1, and that is no line to pin.
`--check` writes no file and exits 1 when a file would change.
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys
import tempfile
from pathlib import Path

CORPUS = "test/fort"
FAMILY = "owner/typed"
PINNED = ["part_%d_test" % k for k in range(1, 9)] + ["cells_test"]
STDOUT = "//! stdout:"
FLAGS = "//! flags:"
TIMEOUT = 1800


def pinned(text, stdout):
    """Return `text` with the `//|` lines after `//! stdout:` replaced by the lines of `stdout`."""
    lines = text.split("\n")
    if STDOUT not in lines:
        raise ValueError("no line '%s'" % STDOUT)
    first = lines.index(STDOUT) + 1
    last = first
    while last < len(lines) and lines[last].startswith("//|"):
        last += 1
    new = ["//| " + line if line else "//|" for line in stdout.splitlines()]
    return "\n".join(lines[:first] + new + lines[last:])


def measure(args, path):
    """Compile and run the test `path`; return its stdout, or None after a status other than 0."""
    corpus = args.root / CORPUS
    flags = [line[len(FLAGS):].split() for line in path.read_text().split("\n")
             if line.startswith(FLAGS)]
    with tempfile.TemporaryDirectory(prefix="fort-pins-") as work:
        env = dict(os.environ, TMPDIR=work)
        prog = os.path.join(work, "prog")
        argv = [args.fort, "--std-dir", args.std_dir]
        argv += ["--cc", args.cc] if args.cc else []
        argv += sum(flags, []) + ["-o", prog, str(path.relative_to(corpus))]
        for step, cwd in ((argv, corpus), ([prog], work)):
            proc = subprocess.run(step, cwd=cwd, env=env, capture_output=True, timeout=TIMEOUT)
            if proc.returncode != 0:
                tail = proc.stderr.decode(errors="replace").strip().split("\n")[-1]
                print("%s: '%s' exited %d: %s" % (path.name, step[0], proc.returncode, tail))
                return None
        return proc.stdout.decode()


def pin(args, name):
    """Pin one test; return whether its file holds the stdout of its run after the call."""
    path = args.root / CORPUS / FAMILY / (name + ".ft")
    stdout = measure(args, path)
    if stdout is None:
        return False
    old = path.read_text()
    new = pinned(old, stdout)
    if new != old and not args.check:
        path.write_text(new)
    print("%s: %s" % (path.name, "same" if new == old else "differs" if args.check else "written"))
    return new == old or not args.check


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", help="the standard library (default: std beside the compiler)")
    parser.add_argument("--cc", help="the --cc of the compiler (default: the one of the compiler)")
    parser.add_argument("--check", action="store_true", help="write nothing; exit 1 on a change")
    parser.add_argument("-j", type=int, default=4, help="tests run in parallel")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help=argparse.SUPPRESS)
    parser.add_argument("tests", nargs="*", default=PINNED)
    args = parser.parse_args(argv)
    args.root = args.root.resolve()
    args.fort = os.path.abspath(args.fort)
    args.std_dir = os.path.abspath(args.std_dir or os.path.join(os.path.dirname(args.fort), "std"))
    with concurrent.futures.ThreadPoolExecutor(args.j) as pool:
        done = list(pool.map(lambda name: pin(args, name), args.tests))
    return 0 if all(done) else 1


if __name__ == "__main__":
    sys.exit(main())
