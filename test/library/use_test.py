#!/usr/bin/env python3
"""Hold each program of the test/lang run corpus built against `std.flib` against its
whole-program build (D9.11, module-system.md 10.4, toolchain.md 2.1).

The script builds each program whose kind is `run` twice: once as one whole program with
`--std-dir`, and once with `--use std=<artifact>`. The artifact is the `fort --lib std` build of
the same standard root with the build mode, the bounds mode and the configuration of the
program, so one artifact serves each set of those options. A program with `//! link:` helpers is
compiled with `-c` and linked by hand: the helpers follow the object, and the `lib.a` of the
artifact follows the helpers.

1. The two builds give the same compile status.
2. The two programs give the same stdout, stderr and exit status. Each runs in a new directory
   of its own with the arguments and the stdin of its directives. The standard root of the
   artifact is the `--std-dir` of the whole program, so a path in a runtime error message is the
   same in both (D11.4).
3. With `--opt`, the IR module of the entry unit passes `opt -passes=verify`.
   A program in CONFLICTS declares a C symbol that a module of `std` which it does not import
   declares in another form. Its whole-program build must work, and its build against
   `std.flib` must fail with the conflict of D9.8, which D9.11 extends to every module of a used
   library.
4. The program of test/library/chain gives the output of its whole-program build when it is built
   against `std.flib` and two more libraries, `aaa` and `zzz`, where `zzz` uses `aaa`. The command
   line names them in byte order, and the link order names `zzz` first; GNU ld resolves the
   archives of a link in the order that the line names them.

Exit 0 when no program differs, 1 when one does, or 2 for an environment failure.
"""

import argparse
import concurrent.futures
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
# The programs whose build against std.flib conflicts with a module that they do not import,
# and the text of the error.
CONFLICTS = {
    "run/ffi/017_own_fstat.ft": "error: conflicting declarations of extern 'fstat'",
}

sys.path.insert(0, str(LANG))
import run_tests  # noqa: E402

# The options of a program that its used library must share (D9.11): the build mode, the bounds
# mode and the configuration map. `--cfg` takes the argument after it.
LIBRARY_FLAGS = ("--release", "--no-bounds-check")
CFG_FLAG = "--cfg"


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("filters", nargs="*", help="hold only the programs whose path holds one")
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard root of the built target")
    parser.add_argument("--cc", required=True, help="the clang of the target")
    parser.add_argument("--target", required=True, help="the target triple")
    parser.add_argument("--opt", default="", help="the LLVM verifier of the entry units")
    parser.add_argument("--timeout", type=float, default=run_tests.DEFAULT_TIMEOUT)
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="programs in parallel")
    return parser.parse_args(argv)


def library_options(flags):
    """The options of `flags` that the artifact must be built with, in their order."""
    options = []
    i = 0
    while i < len(flags):
        if flags[i] in LIBRARY_FLAGS:
            options.append(flags[i])
        elif flags[i] == CFG_FLAG and i + 1 < len(flags):
            options.extend(flags[i : i + 2])
            i += 1
        i += 1
    return tuple(options)


def build_artifacts(args, tests, work):
    """Build one `std.flib` for each set of library options; return the path of each."""
    artifacts = {}
    for test in tests:
        options = library_options(test.flags)
        if options in artifacts:
            continue
        out = work / ("lib%d" % len(artifacts)) / "std.flib"
        out.parent.mkdir()
        argv = [args.fort, "--cc", args.cc, "--lib", "std", args.std_dir] + list(options)
        proc = subprocess.run(argv + ["-o", str(out)], capture_output=True, timeout=600)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
            raise SystemExit(
                "use_test: fort --lib std %s exited %d" % (" ".join(options), proc.returncode)
            )
        artifacts[options] = out
    return artifacts


class Build:
    """The commands of one of the two builds of a program."""

    def __init__(self, args, head, archive):
        self.args = args
        self.head = head  # the compiler and the options that choose the standard modules
        self.archive = archive  # the lib.a that a link by hand adds, or None

    def compile(self, test, output, compile_only=False, emit_ir=False):
        argv = list(self.head) + test.flags
        if compile_only:
            argv.append("-c")
        if emit_ir:
            argv.append("-S")
        return argv + ["-o", output, test.entry]

    def link(self, test, prog, obj):
        argv = [self.args.cc, "--target=" + self.args.target, "-O1", "-o", prog, obj]
        argv.extend(str(LANG / link) for link in test.links)
        if self.archive is not None:
            argv.append(str(self.archive))
        return argv


def build_and_run(args, build, test, workdir):
    """Build the program with `build` and run it in a new directory under `workdir`.

    Return (what, outcome): `what` names the step that the outcome comes from, `compile` or
    `run`, and the outcome is the status, or the status, stdout and stderr of the run.
    """
    env = run_tests.child_env(workdir, args.target)
    prog = os.path.join(workdir, "prog")
    if test.links:
        obj = os.path.join(workdir, "prog.o")
        proc = run_tests.run_process(
            build.compile(test, obj, compile_only=True), LANG, env, args.timeout
        )
        if proc.returncode == 0:
            proc = run_tests.run_process(build.link(test, prog, obj), workdir, env, args.timeout)
    else:
        proc = run_tests.run_process(build.compile(test, prog), LANG, env, args.timeout)
    if proc.returncode != 0 or proc.timed_out:
        return "compile", (
            proc.returncode,
            proc.timed_out,
            proc.stderr.decode("utf-8", errors="replace"),
        )
    rundir = os.path.join(workdir, "run")
    os.mkdir(rundir)
    ran = run_tests.run_process([prog] + test.args, rundir, env, args.timeout, test.stdin)
    return "run", (
        ran.returncode,
        ran.timed_out,
        ran.stdout,
        run_tests.drop_qemu_notice(ran.stderr),
    )


def verify(args, build, test, workdir):
    """The problem of the IR module of the entry unit, or the empty string."""
    env = run_tests.child_env(workdir, args.target)
    module = os.path.join(workdir, "unit.ll")
    proc = run_tests.run_process(build.compile(test, module, emit_ir=True), LANG, env, args.timeout)
    if proc.returncode != 0:
        return "-S exited %d" % proc.returncode
    proc = run_tests.run_process(
        [args.opt, "-passes=verify", "-disable-output", module], workdir, env, args.timeout
    )
    if proc.returncode != 0:
        return "the IR verifier rejected the entry unit: " + run_tests._first_line(proc.stderr)
    return ""


def describe(outcome):
    what, result = outcome
    if what == "compile":
        status, timed_out, stderr = result
        return "compile status %d%s: %s" % (
            status,
            " (timed out)" if timed_out else "",
            stderr[:200],
        )
    status, timed_out, stdout, stderr = result
    return "status %d%s, stdout %r, stderr %r" % (
        status,
        " (timed out)" if timed_out else "",
        stdout[:200],
        stderr[:200],
    )


def make_dirs(workdir):
    """The directory of each of the two builds of one program."""
    for name in ("whole", "library"):
        os.mkdir(os.path.join(workdir, name))


def compare(args, artifacts, test):
    """Hold the library build of `test` against its whole-program build: a problem or ''."""
    test = run_tests.select_target_os(test, run_tests.target_os(args.target))
    artifact = artifacts[library_options(test.flags)]
    whole = Build(args, [args.fort, "--cc", args.cc, "--std-dir", args.std_dir], None)
    library = Build(
        args, [args.fort, "--cc", args.cc, "--use", "std=%s" % artifact], artifact / "lib.a"
    )
    workdir = tempfile.mkdtemp(prefix="fort-use-")
    try:
        make_dirs(workdir)
        want = build_and_run(args, whole, test, os.path.join(workdir, "whole"))
        got = build_and_run(args, library, test, os.path.join(workdir, "library"))
        if want[0] != "run":
            return "%s: the whole-program build failed: %s" % (test.path, describe(want))
        if test.path in CONFLICTS:
            if got[0] == "compile" and got[1][0] == 1 and CONFLICTS[test.path] in got[1][2]:
                return ""
            return "%s: against std.flib %s; want the conflict %r" % (
                test.path,
                describe(got),
                CONFLICTS[test.path],
            )
        if got != want:
            return "%s: against std.flib %s; whole program %s" % (
                test.path,
                describe(got),
                describe(want),
            )
        if want[1][1]:
            return "%s: the program timed out" % test.path
        if args.opt:
            problem = verify(args, library, test, os.path.join(workdir, "library"))
            if problem:
                return "%s: %s" % (test.path, problem)
        return ""
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def check_chain(args, artifact, work):
    """Build the libraries of test/library/chain against `artifact` and the program against them;
    return the problems."""

    def must(argv, what):
        proc = subprocess.run([str(a) for a in argv], capture_output=True, timeout=600)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
            raise SystemExit("use_test: %s exited %d" % (what, proc.returncode))
        return proc

    base = [args.fort, "--cc", args.cc]
    std = ["--use", "std=%s" % artifact]
    aaa, zzz = work / "aaa.flib", work / "zzz.flib"
    must(base + ["--lib", "aaa", CHAIN / "aaa", "-o", aaa] + std, "fort --lib aaa")
    must(
        base + ["--lib", "zzz", CHAIN / "zzz", "-o", zzz] + std + ["--use", "aaa=%s" % aaa],
        "fort --lib zzz",
    )
    used, whole = work / "chain_use", work / "chain_whole"
    must(
        base
        + std
        + ["--use", "aaa=%s" % aaa, "--use", "zzz=%s" % zzz, "-o", used, CHAIN / "main.ft"],
        "the build of the chain with --use",
    )
    must(
        base + ["--std-dir", args.std_dir, "-I", CHAIN, "-o", whole, CHAIN / "main.ft"],
        "the whole-program build of the chain",
    )
    got = must([used], "the chain against its libraries")
    want = must([whole], "the chain as a whole program")
    if (got.stdout, got.stderr) != (want.stdout, want.stderr):
        return ["chain: against its libraries %r; whole program %r" % (got.stdout, want.stdout)]
    if not want.stdout:
        return ["chain: the program writes nothing"]
    return []


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    args.fort = os.path.abspath(args.fort)
    args.std_dir = os.path.abspath(args.std_dir)
    tests, problems = run_tests.discover(LANG)
    if problems:
        raise SystemExit("use_test: the corpus has %d problems" % len(problems))
    for test in tests:
        run_tests.load_test(LANG, test)
    selected = [t for t in run_tests.select(tests, args.filters) if t.kind == "run"]
    if not selected:
        raise SystemExit("use_test: no run program matches %s" % " ".join(args.filters))
    with tempfile.TemporaryDirectory(prefix="fort-use-libs-") as tmp:
        artifacts = build_artifacts(args, selected, pathlib.Path(tmp))
        differences = []
        with concurrent.futures.ThreadPoolExecutor(max(1, args.j)) as pool:
            for problem in pool.map(lambda t: compare(args, artifacts, t), selected):
                if problem:
                    differences.append(problem)
        if not args.filters:
            differences.extend(check_chain(args, artifacts[()], pathlib.Path(tmp)))
    for problem in differences:
        print("FAIL", problem)
    chain = "" if args.filters else " and 1 program against 2 more libraries"
    print(
        "use_test: %d programs against %d builds of std.flib%s; %d differ"
        % (len(selected), len(artifacts), chain, len(differences))
    )
    return 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main())
