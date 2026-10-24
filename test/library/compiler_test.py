#!/usr/bin/env python3
"""Build the compiler from libraries and hold it against its whole-program build (D9.11).

The script runs the three steps of a build tool, one invocation each (module-system.md 10.1):

1. `fort --lib std <std-dir>` writes `<work>/std.flib`.
2. `fort --lib fort <source-root>/fort --use std=...` writes `<work>/fort.flib`. The file
   `src/fort/a/b.ft` is the module `fort.a.b` of this library.
3. `fort --use std=... --use fort=... <source-root>/fort/main.ft` writes `<work>/fort`. The
   entry unit is the module `main` alone.

Each step names the cache directory `<work>/cache` and `--cache-stats`, and the script writes
the time of each step and its cache line. So a second run of the script with the same `<work>`
is a warm build, and the module tests of the library job read the artifacts of step 1 and
step 2. Then the script holds two facts:

- A program that a library build makes writes the output of its whole-program build (D9.11). The
  compiler of step 3 and `--fort` each write the IR module of the whole-program build of the
  compiler with `-S`, and the two modules are the same bytes.
- The compiler of step 3 builds a program and the program runs.

Exit 0 when each step works and the two modules are the same, 1 when they differ or a step
fails, or 2 for a usage error.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile
import time

PROGRAM = "fn main() i32 {\n    println(\"compiler from libraries\");\n    return 0;\n}\n"
EXPECTED = b"compiler from libraries\n"


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler that builds the libraries")
    parser.add_argument("--std-dir", required=True, help="the standard root of the built target")
    parser.add_argument("--source-root", required=True, help="the directory src, which holds fort/")
    parser.add_argument("--cc", required=True, help="the clang of the target")
    parser.add_argument("--work", required=True, help="the directory of the artifacts and cache")
    return parser.parse_args(argv)


def library_paths(work):
    """The artifacts, the compiler and the cache directory below `work`."""
    return {
        "std": work / "std.flib",
        "fort": work / "fort.flib",
        "compiler": work / "fort",
        "cache": work / "cache",
    }


def step_commands(fort, std_dir, source_root, cc, paths):
    """The three invocations of the library build, as (name, argv)."""
    cache = ["--cache-dir", str(paths["cache"]), "--cache-stats", "--cc", cc]
    use_std = ["--use", "std=%s" % paths["std"]]
    use_fort = ["--use", "fort=%s" % paths["fort"]]
    library = source_root / "fort"
    return [
        ("std.flib", [fort, "--lib", "std", str(std_dir), "-o", str(paths["std"])] + cache),
        ("fort.flib", [fort, "--lib", "fort", str(library)] + use_std
         + ["-o", str(paths["fort"])] + cache),
        ("fort", [fort] + use_std + use_fort + ["-o", str(paths["compiler"])] + cache
         + [str(library / "main.ft")]),
    ]


def ir_command(fort, std_dir, source_root, output):
    """The `-S` command of the whole-program build of the compiler."""
    return [fort, "-S", "--std-dir", str(std_dir), "-I", str(source_root), "-o", str(output),
            str(source_root / "fort" / "main.ft")]


def run_step(name, argv, out):
    """Run one step; write its time and its stdout. Returns True when it exits 0."""
    start = time.monotonic()
    proc = subprocess.run(argv, capture_output=True, check=False)
    seconds = time.monotonic() - start
    line = proc.stdout.decode(errors="replace").strip()
    out.write("%s: %.2f s, exit %d%s\n" % (name, seconds, proc.returncode,
                                            ", " + line if line else ""))
    if proc.returncode != 0:
        out.write("  command: %s\n" % " ".join(argv))
        out.write(proc.stderr.decode(errors="replace"))
    return proc.returncode == 0


def same_bytes(left, right):
    """True when the two files hold the same bytes."""
    return pathlib.Path(left).read_bytes() == pathlib.Path(right).read_bytes()


def main(argv=None, out=sys.stdout):
    args = parse_args(argv)
    work = pathlib.Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    paths = library_paths(work)
    source_root = pathlib.Path(args.source_root).resolve()
    std_dir = pathlib.Path(args.std_dir).resolve()
    for name, command in step_commands(args.fort, std_dir, source_root, args.cc, paths):
        if not run_step(name, command, out):
            return 1
    with tempfile.TemporaryDirectory(prefix="fort-compiler-") as tmp:
        tmp = pathlib.Path(tmp)
        whole = tmp / "whole.ll"
        built = tmp / "libraries.ll"
        if not run_step("-S by --fort", ir_command(args.fort, std_dir, source_root, whole), out):
            return 1
        compiler = str(paths["compiler"])
        if not run_step("-S by the library build",
                        ir_command(compiler, std_dir, source_root, built), out):
            return 1
        if not same_bytes(whole, built):
            out.write("the compiler from libraries writes another IR module than --fort\n")
            return 1
        source = tmp / "hello.ft"
        source.write_text(PROGRAM)
        program = tmp / "hello"
        build = [compiler, "--std-dir", str(std_dir), "--cc", args.cc, "-o", str(program),
                 str(source)]
        if not run_step("a program by the library build", build, out):
            return 1
        proc = subprocess.run([str(program)], capture_output=True, check=False)
        if proc.returncode != 0 or proc.stdout != EXPECTED:
            out.write("the program wrote %r and exited %d\n" % (proc.stdout, proc.returncode))
            return 1
    out.write("the compiler from libraries writes the IR module of --fort\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
