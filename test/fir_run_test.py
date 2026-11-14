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

One difference is no difference (spec/ownership-design.md 17, row 5; D10.9 rule 2). An access to
dead storage gives the value that the storage held last, or the program stops with a memory
fault. The interpreter keeps the bytes of dead storage and reports the access as the event `use
after release`. So the script accepts a checked build that stops with SIGSEGV or SIGBUS under 3
conditions. The directive of the program expects that signal (`//! signal:`). The interpreter
reports that event. The stdout and the stderr of the checked build are each a start of those of
the interpreter: a memory fault flushes no buffered output. The script does not hold the access
at which the fault comes: the lang test of the program pins the signal and the output before it.

One program differs and the difference is pinned (PINNED_DIFFERENCES): its two runs report one
defect at two statements. For such a program the script holds the stdout and the end of the
stderr of each run against the pinned texts, so a change of one run fails the script.

The interpreter always reports the ownership events of each run (`--fir-events`,
spec/ownership-design.md 3.4), and the comparison of stderr leaves the event lines out. With
`--events` the script prints the count of the programs that run with no event and each program
with its events. A correct program makes no event, so each program with events must stand in
EXPECTED_EVENTS with its defect and the exact lines of its events, and each program there must
make them when it runs.

The script prints each program that differs, the count of the programs that run, of those that
are outside and of those that differ, the count of the outside programs for each function, and
the time of the whole corpus. It exits 0 when no program differs, 1 when one does or when the
events of the programs do not match EXPECTED_EVENTS, and 2 for an environment failure.
"""

import argparse
import collections
import concurrent.futures
import os
import pathlib
import re
import shutil
import signal
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
# A line of `fort --fir-run --fir-events` for an ownership event of the run.
EVENT_RE = re.compile(rb"^fort: event [^\n]*\n?", re.MULTILINE)
# The start of the event line of an access to dead storage, and the two signals of a memory fault
# (D10.9).
DEAD_ACCESS = "fort: event use after release "
MEMORY_FAULTS = (-signal.SIGSEGV, -signal.SIGBUS)
# The names of those signals in a `//! signal:` directive.
FAULT_NAMES = ("SEGV", "BUS")
# The programs of the corpus that make an event: each with its ownership defect and the lines of
# its events, in the order of the run, as
# `"programs/<name>.ft": ("<defect>", ["leak in @.<function> at <line>:<col>"])`. A line stands
# without its start `fort: event `, and `@` stands for the module of the program, the name of its
# file without `.ft`. The programs that hold the release rules of D10.9 stand here: each one
# releases storage that is no live allocation of the runtime, or reaches dead storage, and that is
# its defect.
# The four programs of `run/declarations/002` to `005` stand here too, and they are one program
# at four levels. It reads a scalar local of the loop run before through a kept
# pointer, between the zero bytes of the declaration and the initializer (D10.1). No program
# reads those bytes with no kept pointer, because the checker asks for an initializer. The
# program takes the loop route. On the other route, a pointer to a local of a call that
# returned, the builds give different output until frame storage is in the compiler (D10.9), so
# the program does not take it. The access is a `use after scope` of D10.9, which the
# interpreter reports as `forged` until each entry of a block gives its local new storage
# (spec/ownership-design.md 3.4).
STALE_SCALAR = "a read of a scalar local of the loop run before through a kept pointer"
EVENT_TABLE = {
    "run/declarations/002_scalar_zero_bytes.ft": (
        STALE_SCALAR,
        [
            "forged in @.peek at 31:12 through frame @.integer _5",
        ],
    ),
    "run/declarations/003_scalar_zero_bytes_o0.ft": (
        STALE_SCALAR,
        [
            "forged in @.peek at 32:12 through frame @.integer _5",
        ],
    ),
    "run/declarations/004_scalar_zero_bytes_o2.ft": (
        STALE_SCALAR,
        [
            "forged in @.peek at 32:12 through frame @.integer _5",
        ],
    ),
    "run/declarations/005_scalar_zero_bytes_release.ft": (
        STALE_SCALAR,
        [
            "forged in @.peek at 32:12 through frame @.integer _5",
        ],
    ),
    "run/errors/040_invalid_release_inner_pointer.ft": (
        "a del of an address inside an allocation",
        [
            "invalid release in @.main at 27:5",
        ],
    ),
    "run/errors/041_invalid_release_inner_border.ft": (
        "a del of the address of byte 16 of an allocation of 64 bytes",
        [
            "invalid release in @.main at 25:5",
        ],
    ),
    "run/errors/042_invalid_release_second.ft": (
        "a second del of one allocation",
        [
            "duplicate owner in @.main at 23:35",
            "double release in @.main at 30:5 through owner @.main _2",
        ],
    ),
    "run/errors/043_invalid_release_stack_address.ft": (
        "a del of the address of a local",
        [
            "invalid release in @.main at 23:5",
        ],
    ),
    "run/errors/044_invalid_release_storage_of_c.ft": (
        "a del of an allocation of C",
        [
            "invalid release in @.main at 19:5 through owner @.main _1",
        ],
    ),
    "run/errors/045_invalid_release_large_inner.ft": (
        "a del of an address inside a large allocation",
        [
            "invalid release in @.main at 25:5",
        ],
    ),
    "run/errors/047_invalid_release_huge_inner.ft": (
        "a del of an address inside an allocation with a mapping of its own",
        [
            "invalid release in @.main at 26:5",
        ],
    ),
    "run/errors/048_invalid_release_storage_went_back.ft": (
        "a second del of an allocation whose storage went back to the system",
        [
            "duplicate owner in @.main at 38:35",
            "double release in @.main at 43:5 through owner @.main _3",
        ],
    ),
    "run/errors/049_invalid_release_chunk_went_back.ft": (
        "a second del of an allocation whose chunk went back to the system",
        [
            "duplicate owner in @.main at 28:35",
            "double release in @.main at 34:5 through owner @.main _3",
        ],
    ),
    "run/errors/050_invalid_release_span_inner.ft": (
        "a del of a span owner whose pointer is an address inside an allocation",
        [
            "invalid release in @.main at 38:5 through owner @.main _1",
        ],
    ),
    "run/errors/051_invalid_release_string_second.ft": (
        "a second del of the storage of one string",
        [
            "double release in @.main at 41:5 through owner @.main _1",
        ],
    ),
    "run/errors/052_invalid_release_after_the_last_byte.ft": (
        "a del of the address after the last byte of an allocation",
        [
            "invalid release in @.main at 35:5",
        ],
    ),
    "run/errors/053_invalid_release_global.ft": (
        "a del of the address of a global",
        [
            "invalid release in @.main at 24:5",
        ],
    ),
    "run/errors/054_invalid_release_string_literal.ft": (
        "a del of the bytes of a string literal",
        [
            "invalid release in @.main at 25:5",
        ],
    ),
    "run/pointers/015_dead_view_keeps_its_bytes.ft": (
        "reads through a view of a dead allocation",
        [
            "use after release in @.main at 38:29 through owner @.main _2",
            "use after release in @.main at 38:43 through owner @.main _2",
            "use after release in @.main at 54:29 through owner @.main _2",
            "use after release in @.main at 54:43 through owner @.main _2",
        ],
    ),
    "run/pointers/016_dead_owner_slot_through_a_call.ft": (
        "a callee stores an owner into a dead allocation",
        [
            "use after release in @.fill_ptr at 18:5 through owner @.main _2",
            "use after release in @.fill_ptr at 18:5",
            "use after release in @.fill_ptr at 19:13 through owner @.main _2",
            "leak at exit in @.main at 47:5",
        ],
    ),
    "run/pointers/017_dead_owner_slot_store.ft": (
        "a store of an owner into a dead allocation",
        [
            "use after release in @.main at 19:5 through owner @.main _2",
            "use after release in @.main at 19:5",
            "use after release in @.main at 20:13 through owner @.main _2",
            "leak at exit in @.main at 30:5",
        ],
    ),
    "run/pointers/018_dead_large_read_faults.ft": (
        "a read of a dead large allocation",
        [
            "use after release in @.main at 18:19 through owner @.main _1",
        ],
    ),
    "run/pointers/019_dead_large_write_faults.ft": (
        "a write into a dead large allocation",
        [
            "use after release in @.main at 16:19 through owner @.main _1",
        ],
    ),
    "run/pointers/020_dead_small_read_faults.ft": (
        "a read of a dead allocation whose storage went back to the system",
        [
            "use after release in @.main at 34:19",
        ],
    ),
    "run/pointers/022_dead_huge_read_faults.ft": (
        "a read of a dead allocation with a mapping of its own",
        [
            "use after release in @.main at 19:19 through owner @.main _1",
        ],
    ),
}


def event_lines(path, lines):
    """Return `lines` of EVENT_TABLE for the program `path` as the interpreter writes them."""
    module = pathlib.PurePath(path).stem
    return ["fort: event " + line.replace("@", module) for line in lines]


# EVENT_TABLE with each line as the interpreter writes it.
EXPECTED_EVENTS = {
    path: (defect, event_lines(path, lines)) for path, (defect, lines) in EVENT_TABLE.items()
}
# The programs whose two runs differ, each with the reason, then the stdout and the end of the
# stderr of the interpreter, then those of the checked build. The two runs end with the same
# status.
#
# 052: a pointer to the address after the last byte of an allocation, made an owner by C. The
# allocator puts no header and no gap between 2 small blocks, so that address is the start of the
# next block when the program took one. The checked build compares addresses only: the first del
# releases the next block, and the del of the owner of that block is the invalid release. The
# interpreter keeps the allocation of each pointer, so it reports the first del. Each run reports
# the defect as `invalid release` and gives no dead storage to another object. A module with no
# `import extern;` cannot write this program (spec/ownership-design.md 3.5).
PINNED_DIFFERENCES = {
    "run/errors/052_invalid_release_after_the_last_byte.ft": (
        "the address after the last byte of a block is the start of the next block",
        (b"before\n", b".ft:35:5: runtime error: invalid release\n"),
        (b"before\nafter the first del: 16\n", b".ft:37:5: runtime error: invalid release\n"),
    ),
}

# The longest run of a program under the interpreter in a debug build in the Linux VM was 70 s
# (run/stdlib/095_rt_round_trip_floats.ft), so a program gets more time than the corpus gives.
TIMEOUT = 300.0


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("filters", nargs="*", help="hold only the programs whose path holds one")
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard root of the built target")
    parser.add_argument("--cc", required=True, help="the clang of the target")
    parser.add_argument("--target", required=True, help="the target triple")
    parser.add_argument("--timeout", type=float, default=TIMEOUT)
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="programs in parallel")
    parser.add_argument("--events", action="store_true", help="hold the events of the runs")
    return parser.parse_args(argv)


def checked_flags(flags):
    """The flags of a program without the flags of a build mode."""
    return [flag for flag in flags if flag not in MODE_FLAGS]


def interpret(args, test, workdir):
    """Run `test` under `fort --fir-run`; return the process."""
    env = run_tests.child_env(workdir, args.target)
    argv = [args.fort, "--std-dir", args.std_dir, "--target", args.target, "--fir-run"]
    argv.append("--fir-events")
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


def stops_at_dead_storage(test, want, events, got_stdout, got_stderr, want_stderr):
    """Whether the memory fault of the checked build `want` of `test` is a result that D10.9
    rule 2 permits. The directive of `test` expects a memory fault, the interpreter marks an
    access to dead storage, and the output of the checked build is a start of the output of the
    interpreter. The function does not hold the access at which the fault comes."""
    if want.returncode not in MEMORY_FAULTS:
        return False
    if test.signal_name not in FAULT_NAMES:
        return False
    if not any(event.startswith(DEAD_ACCESS) for event in events):
        return False
    return got_stdout.startswith(want.stdout) and got_stderr.startswith(want_stderr)


def holds_pinned(pinned, got, got_stderr, want, want_stderr):
    """Whether the run `got` of the interpreter and the checked build `want` give the texts of
    `pinned`, a row of PINNED_DIFFERENCES, and end with the same status."""
    _, (got_out, got_end), (want_out, want_end) = pinned
    return (
        got.returncode == want.returncode
        and got.stdout == got_out
        and got_stderr.endswith(got_end)
        and want.stdout == want_out
        and want_stderr.endswith(want_end)
    )


def compare(args, test):
    """Return (verdict, detail, events) for `test`: `run`, `outside` with the function, or
    `differ`, and the event lines of the run, which the comparison of stderr leaves out."""
    test = run_tests.select_target_os(test, run_tests.target_os(args.target))
    workdir = tempfile.mkdtemp(prefix="fort-fir-run-")
    try:
        got = interpret(args, test, workdir)
        if got.timed_out:
            return "differ", "%s: the interpreter timed out" % test.path, []
        outside = OUTSIDE_RE.search(got.stderr)
        if outside:
            return "outside", outside.group(1).decode("utf-8", errors="replace"), []
        events = [m.decode("utf-8", errors="replace").strip() for m in EVENT_RE.findall(got.stderr)]
        problem, want = build_and_run(args, test, workdir)
        if problem:
            return "differ", "%s: %s" % (test.path, problem), events
        want_stderr = run_tests.drop_qemu_notice(want.stderr)
        got_stderr = EVENT_RE.sub(b"", run_tests.drop_qemu_notice(got.stderr))
        pinned = PINNED_DIFFERENCES.get(test.path)
        if pinned:
            if holds_pinned(pinned, got, got_stderr, want, want_stderr):
                return "run", "", events
            detail = "%s: the runs do not give the pinned texts: interpreter %s; checked build %s"
            return "differ", detail % (test.path, describe(got), describe(want)), events
        if stops_at_dead_storage(test, want, events, got.stdout, got_stderr, want_stderr):
            return "run", "", events
        if (got.returncode, got.stdout, got_stderr) != (want.returncode, want.stdout, want_stderr):
            return "differ", "%s: interpreter %s; checked build %s" % (
                test.path,
                describe(got),
                describe(want),
            ), events
        return "run", "", events
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
    with_events = []
    with concurrent.futures.ThreadPoolExecutor(max(1, args.j)) as pool:
        for test, (verdict, detail, events) in zip(
            selected, pool.map(lambda t: compare(args, t), selected)
        ):
            counts[verdict] += 1
            if verdict == "outside":
                outside[detail] += 1
            elif verdict == "differ":
                print("DIFF", detail)
            if events:
                with_events.append((test.path, events))
    print(
        "fir_run_test: %d programs: %d run under the interpreter, %d outside, %d differ"
        % (len(selected), counts["run"], counts["outside"], counts["differ"])
    )
    print(
        "fir_run_test: outside by function: "
        + ", ".join("%s %d" % (name, n) for name, n in sorted(outside.items()))
    )
    for path in sorted(set(PINNED_DIFFERENCES) & {t.path for t in selected}):
        print("PINNED %s: %s" % (path, PINNED_DIFFERENCES[path][0]))
    if args.events:
        print(
            "fir_run_test: %d programs run with no event, %d with events"
            % (counts["run"] + counts["differ"] - len(with_events), len(with_events))
        )
        for path, events in sorted(with_events):
            print("EVENTS %s: %s" % (path, "; ".join(events)))
        ran = {t.path for t in selected}
        got = dict(with_events)
        wrong = sorted(p for p in got if got[p] != EXPECTED_EVENTS.get(p, ("", []))[1])
        quiet = sorted(set(EXPECTED_EVENTS) & ran - set(got))
        for path in wrong:
            print("EVENTS UNEXPECTED %s: want %s" % (path, EXPECTED_EVENTS.get(path, ("", []))[1]))
        for path in quiet:
            print("EVENTS MISSING %s: %s" % (path, EXPECTED_EVENTS[path][0]))
        if wrong or quiet:
            print("fir_run_test: the events do not match EXPECTED_EVENTS")
            print("fir_run_test: %.1f s" % (time.monotonic() - start))
            return 1
    print("fir_run_test: %.1f s" % (time.monotonic() - start))
    return 1 if counts["differ"] else 0


if __name__ == "__main__":
    sys.exit(main())
