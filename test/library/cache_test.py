#!/usr/bin/env python3
"""Hold the cache of the library builds with the real tools (D9.11, toolchain.md 2.1 and 2.3).

The script copies test/library/chain to a temporary directory, builds `std`, `aaa` and `zzz`
with `--lib` and the program with `--use`, each with `--cache-stats` and one cache directory,
and reads the line of each build.

1. Cold, fill and warm: the program built as one whole program (cold), against libraries that
   the cache does not hold (fill), and again (warm) writes the same stdout and stderr and exits
   with the same status. The fill misses each of the four units and the warm build hits each of
   them. The warm artifacts hold the bytes of the fill artifacts, `lib.a` included, because a hit
   writes the files of the entry.
2. A program with a compile error gives the same diagnostics in two builds, writes no line and
   stores no entry.
3. An edit of a body of `aaa` rebuilds 1 library and 0 dependents: `aaa` misses, and `std`,
   `zzz` and the entry unit hit. The program then gives the output of the edited whole program.
4. An edit of a declaration of `aaa` rebuilds each dependent: `aaa`, `zzz` and the entry unit
   miss, and `std` hits.
5. An entry whose `lib.a` or `object.o` changed is a miss with the cause `corrupt`, and the
   program that the build links gives the output of the whole program.
6. An object build stores the object that the compile line wrote, whatever its output: after
   `-c -o /dev/null`, a link build hits and gives the program of the whole build, and
   `-c -o /dev/stdout` into a pipe exits 0 and writes the bytes of the object, after a miss and
   after a hit.

Exit 0 when each holds, 1 when one does not, or 2 for an environment failure.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parent.parent
LANG = SOURCE_ROOT / "test" / "lang"
CHAIN = HERE / "chain"

sys.path.insert(0, str(LANG))
import run_tests  # noqa: E402

TIMEOUT = 600

# The body of `bump` in aaa/base.ft and its edit, and a declaration that the edit of a signature
# adds to the module.
BODY = "    counter += 1;\n"
EDITED_BODY = "    counter += 2;\n"
NEW_DECLARATION = "\nfn extra() i32 {\n    return 1;\n}\n"

# A program whose import names no declaration of zzz.top.
BAD_PROGRAM = "import zzz.top;\n\nfn main() i32 {\n    return top.missing();\n}\n"


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard root of the built target")
    parser.add_argument("--cc", required=True, help="the clang of the target")
    parser.add_argument("--target", required=True, help="the target triple")
    return parser.parse_args(argv)


class Rig:
    """The tools, the copy of the chain and the cache of one run."""

    def __init__(self, args, work):
        self.args = args
        self.work = work
        self.chain = work / "chain"
        shutil.copytree(CHAIN, self.chain)
        self.cache = work / "cache"
        self.env = run_tests.child_env(str(work), args.target)
        self.problems = []

    def fort(self, argv):
        """Run the compiler with --cc and --cache-dir; return the process."""
        full = [self.args.fort, "--cc", self.args.cc] + [str(a) for a in argv]
        return subprocess.run(full, capture_output=True, timeout=TIMEOUT, env=self.env)

    def must(self, argv, what):
        proc = self.fort(argv)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
            raise SystemExit("cache_test: %s exited %d" % (what, proc.returncode))
        return proc

    def stats(self, proc):
        """The words of the one --cache-stats line of `proc`: outcome, unit, name, key, cause."""
        lines = proc.stdout.decode().splitlines()
        if len(lines) != 1 or not lines[0].startswith("cache "):
            self.problems.append("a build wrote %r to stdout" % proc.stdout)
            return ("", "", "", "", "")
        words = lines[0].split(" ")[1:]
        return tuple(words + [""] * (5 - len(words)))

    def library(self, name, root, uses):
        argv = ["--cache-stats", "--cache-dir", self.cache, "--lib", name, root, "-o",
                self.work / (name + ".flib")]
        for used in uses:
            argv += ["--use", "%s=%s" % (used, self.work / (used + ".flib"))]
        return self.stats(self.must(argv, "fort --lib " + name))

    def program(self, entry, out):
        argv = ["--cache-stats", "--cache-dir", self.cache, "-o", out]
        for used in ("std", "aaa", "zzz"):
            argv += ["--use", "%s=%s" % (used, self.work / (used + ".flib"))]
        return self.fort(argv + [entry])

    def build_all(self):
        """Build the four units in dependency order; return the outcome of each."""
        outcomes = {}
        outcomes["std"] = self.library("std", self.args.std_dir, [])
        outcomes["aaa"] = self.library("aaa", self.chain / "aaa", ["std"])
        outcomes["zzz"] = self.library("zzz", self.chain / "zzz", ["std", "aaa"])
        proc = self.program(self.chain / "main.ft", self.work / "app")
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
            raise SystemExit("cache_test: the build of the program exited %d" % proc.returncode)
        outcomes["main"] = self.stats(proc)
        return outcomes

    def expect(self, what, outcomes, want):
        """Hold the outcome of each unit, `hit` or the cause of a miss, against `want`."""
        for unit, outcome in sorted(want.items()):
            got = outcomes[unit]
            word = "hit" if got[0] == "hit" else got[4]
            if word != outcome:
                self.problems.append("%s: %s gave %s, want %s" % (what, unit, " ".join(got),
                                                                  outcome))

    def run_program(self, binary):
        proc = subprocess.run([str(binary)], capture_output=True, timeout=TIMEOUT, env=self.env,
                              cwd=str(self.work))
        return proc.returncode, proc.stdout, proc.stderr

    def whole(self, out):
        """The cold build: the program of the copy as one whole program."""
        argv = [self.args.fort, "--cc", self.args.cc, "--std-dir", self.args.std_dir, "-I",
                self.chain, "-o", out, self.chain / "main.ft"]
        proc = subprocess.run([str(a) for a in argv], capture_output=True, timeout=TIMEOUT,
                              env=self.env)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
            raise SystemExit("cache_test: the whole-program build exited %d" % proc.returncode)
        return self.run_program(out)

    def same_program(self, what):
        """The linked program against the whole program of the copy as it stands."""
        want = self.whole(self.work / "whole")
        got = self.run_program(self.work / "app")
        if got != want:
            self.problems.append("%s: the program gives %r, the whole program %r"
                                 % (what, got, want))
        if not want[1]:
            self.problems.append("%s: the whole program writes nothing" % what)
        return got


def artifact_bytes(work, names):
    return {(name, f): (work / (name + ".flib") / f).read_bytes()
            for name in names for f in ("interface.ft", "lib.a", "manifest")}


def entry_count(cache):
    layout = cache / "1"
    return len(list(layout.iterdir())) if layout.is_dir() else 0


def check_cold_fill_warm(rig):
    cold = rig.whole(rig.work / "cold")
    fill = rig.build_all()
    rig.expect("fill", fill, {"std": "absent", "aaa": "absent", "zzz": "absent",
                              "main": "absent"})
    filled = rig.run_program(rig.work / "app")
    fill_bytes = artifact_bytes(rig.work, ("std", "aaa", "zzz"))
    warm = rig.build_all()
    rig.expect("warm", warm, {"std": "hit", "aaa": "hit", "zzz": "hit", "main": "hit"})
    warmed = rig.run_program(rig.work / "app")
    if not (cold == filled == warmed):
        rig.problems.append("cold %r, fill %r and warm %r differ" % (cold, filled, warmed))
    if artifact_bytes(rig.work, ("std", "aaa", "zzz")) != fill_bytes:
        rig.problems.append("the warm artifacts differ from the fill artifacts")
    for unit in fill:
        if fill[unit][3] != warm[unit][3]:
            rig.problems.append("%s: the fill key and the warm key differ" % unit)
    return entry_count(rig.cache)


def check_diagnostics(rig):
    bad = rig.work / "bad.ft"
    bad.write_text(BAD_PROGRAM)
    before = entry_count(rig.cache)
    first = rig.program(bad, rig.work / "bad")
    second = rig.program(bad, rig.work / "bad")
    if first.returncode != 1 or not first.stderr:
        rig.problems.append("the bad program exited %d with %r" % (first.returncode,
                                                                   first.stderr))
    if (first.returncode, first.stdout, first.stderr) != (second.returncode, second.stdout,
                                                          second.stderr):
        rig.problems.append("two builds of the bad program differ: %r and %r"
                            % (first.stderr, second.stderr))
    if first.stdout or entry_count(rig.cache) != before:
        rig.problems.append("a compile error wrote a line or stored an entry")


def edit(path, old, new):
    text = path.read_text()
    if old not in text:
        raise SystemExit("cache_test: %s holds no %r" % (path, old))
    path.write_text(text.replace(old, new, 1))


def check_body_edit(rig):
    base = rig.chain / "aaa" / "base.ft"
    interface = (rig.work / "aaa.flib" / "interface.ft").read_bytes()
    edit(base, BODY, EDITED_BODY)
    outcomes = rig.build_all()
    rig.expect("an edit of a body", outcomes, {"std": "hit", "aaa": "absent", "zzz": "hit",
                                               "main": "hit"})
    if (rig.work / "aaa.flib" / "interface.ft").read_bytes() != interface:
        rig.problems.append("an edit of a body changed the interface of aaa")
    before = rig.run_program(rig.work / "cold")
    after = rig.same_program("an edit of a body")
    if after == before:
        rig.problems.append("the edit of a body did not reach the program")


def check_signature_edit(rig):
    base = rig.chain / "aaa" / "base.ft"
    base.write_text(base.read_text() + NEW_DECLARATION)
    outcomes = rig.build_all()
    rig.expect("an edit of a declaration", outcomes,
               {"std": "hit", "aaa": "absent", "zzz": "absent", "main": "absent"})
    rig.same_program("an edit of a declaration")


def check_corrupt(rig):
    outcomes = rig.build_all()
    rig.expect("a build that changed nothing", outcomes,
               {"std": "hit", "aaa": "hit", "zzz": "hit", "main": "hit"})
    archive = rig.cache / "1" / outcomes["zzz"][3] / "lib.a"
    archive.write_bytes(archive.read_bytes() + b"\n")
    obj = rig.cache / "1" / outcomes["main"][3] / "object.o"
    obj.write_bytes(b"not an object\n")
    outcomes = rig.build_all()
    rig.expect("a corrupt entry", outcomes,
               {"std": "hit", "aaa": "hit", "zzz": "corrupt", "main": "corrupt"})
    rig.same_program("a corrupt entry")


def check_object_outputs(rig):
    cache = rig.work / "object-cache"
    uses = []
    for used in ("std", "aaa", "zzz"):
        uses += ["--use", "%s=%s" % (used, rig.work / (used + ".flib"))]
    entry = rig.chain / "main.ft"
    cold = rig.work / "cold.o"
    rig.must(["--cache-dir", rig.work / "cold-cache", "-c", "-o", cold] + uses + [entry],
             "the cold object")
    rig.must(["--cache-dir", cache, "-c", "-o", "/dev/null"] + uses + [entry],
             "the object to /dev/null")
    proc = rig.must(["--cache-stats", "--cache-dir", cache, "-o", rig.work / "app"] + uses
                    + [entry], "the link after the object to /dev/null")
    if rig.stats(proc)[0] != "hit":
        rig.problems.append("the link after -c -o /dev/null gave %r" % proc.stdout)
    rig.same_program("the link after -c -o /dev/null")
    for what in ("a hit", "a miss"):
        if what == "a miss":
            shutil.rmtree(cache)
        proc = rig.must(["--cache-dir", cache, "-c", "-o", "/dev/stdout"] + uses + [entry],
                        "the object to /dev/stdout after " + what)
        if proc.stdout != cold.read_bytes():
            rig.problems.append("-c -o /dev/stdout after %s wrote %d bytes, the object %d"
                                % (what, len(proc.stdout), len(cold.read_bytes())))


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    args.fort = os.path.abspath(args.fort)
    args.std_dir = os.path.abspath(args.std_dir)
    with tempfile.TemporaryDirectory(prefix="fort-cache-") as tmp:
        rig = Rig(args, pathlib.Path(tmp))
        entries = check_cold_fill_warm(rig)
        check_diagnostics(rig)
        check_body_edit(rig)
        check_signature_edit(rig)
        check_corrupt(rig)
        total = entry_count(rig.cache)
        check_object_outputs(rig)
    for problem in rig.problems:
        print("FAIL", problem)
    print("cache_test: %d entries after the fill, %d at the end; %d problems"
          % (entries, total, len(rig.problems)))
    return 1 if rig.problems else 0


if __name__ == "__main__":
    sys.exit(main())
