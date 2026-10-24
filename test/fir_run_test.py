#!/usr/bin/env python3
"""Hold the run corpus of test/lang under `fort --fir-run` against the native checked build of
each program (spec/ownership-design.md 3.5).

For each program of test/lang/run and test/lang/programs, the script runs `fort --fir-run` with
the flags of the program, its arguments after `--`, and its stdin. The flags lose `--release`
and `--no-bounds-check`, because the interpreter runs the FIR before the build-mode pass, as the
checked mode does. A run whose stderr holds `the run calls '<name>', which the interpreter does
not run` calls a function that the interpreter does not model: the program is `outside`, and it
is no case of the oracle. For each other program the script builds the program in the checked
mode with the same flags, and runs it with the same arguments and stdin. The two must give the
same stdout, the same stderr and the same status.

The script prints each program that differs, the count of the programs that run, of those that
are outside and of those that differ, the count of the outside programs for each function, and
the time of the whole corpus. It exits 0 when no program differs, 1 when one does, and 2 for an
environment failure.
"""

import argparse
import collections
import concurrent.futures
import os
import pathlib
import re
import shutil
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
LANG = HERE / "lang"
sys.path.insert(0, str(LANG))
import run_tests  # noqa: E402

# The flags of a build mode, which the checked build of the oracle drops.
MODE_FLAGS = ("--release", "--no-bounds-check")
# The line of `fort --fir-run` for a call that the interpreter does not model.
OUTSIDE_RE = re.compile(
    rb"fort: error: the run calls '([^']*)', which the interpreter does not run"
)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("filters", nargs="*", help="hold only the programs whose path holds one")
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard root of the built target")
    parser.add_argument("--cc", required=True, help="the clang of the target")
    parser.add_argument("--target", required=True, help="the target triple")
    parser.add_argument("--timeout", type=float, default=run_tests.DEFAULT_TIMEOUT)
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="programs in parallel")
    return parser.parse_args(argv)


def checked_flags(flags):
    """The flags of a program without the flags of a build mode."""
    return [flag for flag in flags if flag not in MODE_FLAGS]


def interpret(args, test, workdir):
    """Run `test` under `fort --fir-run`; return the process."""
    env = run_tests.child_env(workdir, args.target)
    argv = [args.fort, "--std-dir", args.std_dir, "--target", args.target, "--fir-run"]
    argv += checked_flags(test.flags) + [test.entry, "--"] + test.args
    return run_tests.run_process(argv, LANG, env, args.timeout, test.stdin)


def build_and_run(args, test, workdir):
    """Build `test` in the checked mode and run it; return (problem, process)."""
    env = run_tests.child_env(workdir, args.target)
    prog = os.path.join(workdir, "prog")
    head = [args.fort, "--cc", args.cc, "--std-dir", args.std_dir] + checked_flags(test.flags)
    if test.links:
        obj = os.path.join(workdir, "prog.o")
        proc = run_tests.run_process(head + ["-c", "-o", obj, test.entry], LANG, env, args.timeout)
        if proc.returncode == 0:
            link = [args.cc, "--target=" + args.target, "-O1", "-o", prog, obj]
            link += [str(LANG / helper) for helper in test.links]
            proc = run_tests.run_process(link, workdir, env, args.timeout)
    else:
        proc = run_tests.run_process(head + ["-o", prog, test.entry], LANG, env, args.timeout)
    if proc.returncode != 0 or proc.timed_out:
        return "the checked build failed: " + run_tests._first_line(proc.stderr), None
    rundir = os.path.join(workdir, "run")
    os.mkdir(rundir)
    ran = run_tests.run_process([prog] + test.args, rundir, env, args.timeout, test.stdin)
    return "", ran


def describe(proc):
    return "status %s, stdout %r, stderr %r" % (
        run_tests._describe_status(proc.returncode),
        proc.stdout[:200],
        proc.stderr[:200],
    )


def compare(args, test):
    """Return (verdict, detail) for `test`: `run`, `outside` with the function, or `differ`."""
    test = run_tests.select_target_os(test, run_tests.target_os(args.target))
    workdir = tempfile.mkdtemp(prefix="fort-fir-run-")
    try:
        got = interpret(args, test, workdir)
        if got.timed_out:
            return "differ", "%s: the interpreter timed out" % test.path
        outside = OUTSIDE_RE.search(got.stderr)
        if outside:
            return "outside", outside.group(1).decode("utf-8", errors="replace")
        problem, want = build_and_run(args, test, workdir)
        if problem:
            return "differ", "%s: %s" % (test.path, problem)
        want_stderr = run_tests.drop_qemu_notice(want.stderr)
        got_stderr = run_tests.drop_qemu_notice(got.stderr)
        if (got.returncode, got.stdout, got_stderr) != (want.returncode, want.stdout, want_stderr):
            return "differ", "%s: interpreter %s; checked build %s" % (
                test.path,
                describe(got),
                describe(want),
            )
        return "run", ""
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    args.fort = os.path.abspath(args.fort)
    args.std_dir = os.path.abspath(args.std_dir)
    start = time.monotonic()
    tests, problems = run_tests.discover(LANG)
    if problems:
        print("fir_run_test: the corpus has %d problems" % len(problems))
        return 2
    for test in tests:
        run_tests.load_test(LANG, test)
    selected = [t for t in run_tests.select(tests, args.filters) if t.kind == "run"]
    if not selected:
        print("fir_run_test: no run program matches %s" % " ".join(args.filters))
        return 2
    counts = collections.Counter()
    outside = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(max(1, args.j)) as pool:
        for verdict, detail in pool.map(lambda t: compare(args, t), selected):
            counts[verdict] += 1
            if verdict == "outside":
                outside[detail] += 1
            elif verdict == "differ":
                print("DIFF", detail)
    print(
        "fir_run_test: %d programs: %d run under the interpreter, %d outside, %d differ"
        % (len(selected), counts["run"], counts["outside"], counts["differ"])
    )
    print(
        "fir_run_test: outside by function: "
        + ", ".join("%s %d" % (name, n) for name, n in sorted(outside.items()))
    )
    print("fir_run_test: %.1f s" % (time.monotonic() - start))
    return 1 if counts["differ"] else 0


if __name__ == "__main__":
    sys.exit(main())
