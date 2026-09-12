#!/usr/bin/env python3
"""test/tty_test.py: the interactive half of D11.5, witnessed on a real pseudo terminal.

No language test can see this rule. `test/lang/run_tests.py` captures a program's stdout through
a pipe, so every language test takes the *non*-interactive path and would stay green with line
buffering deleted. So this harness compiles `test/tty/print_then_wait.ft` once and runs it twice,
with its output on a pseudo terminal and with its output on a pipe, and asks what has arrived
while the program is still blocked in a read of stdin and its exit flush has not happened yet:

  on a terminal  both of its lines, in the order it printed them (stdout is line-buffered),
  on a pipe      the stderr line alone, the stdout line waiting for the flush at exit.

It does that for two programs. Two runtimes are live while the compiler still lowers its builtins
to the C one. `print_then_wait.ft` writes through the print family, which reaches
runtime/fort_rt.c. `rt_print_then_wait.ft` writes through std.rt's own entry points. Both programs
must behave the same way, and no language test can see either one.

ctest runs it as the unit test `tty` (toolchain.md 5.3). Exit status: 0 when both runs behave,
1 when either does not, 2 when the environment cannot be measured at all.
"""

import argparse
import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
# One program for each runtime, named after the runtime whose buffers it writes through.
SOURCES = (
    ("C runtime", "print_then_wait"),
    ("fort runtime", "rt_print_then_wait"),
)

# Seconds to wait for output the program has already written; generous, because it is only
# reached when the output never comes, which is the failing path.
TIMEOUT = 20.0
# Seconds to wait before concluding that nothing more is coming. The program wrote its stdout
# line before the stderr line this harness has already read, so a line-buffered pipe would have
# delivered it long before this elapses.
QUIET = 1.0
# Cross-compiled programs run under qemu-user, which needs the target's libraries.
DEFAULT_QEMU_LD_PREFIX = "/usr/x86_64-linux-gnu"


def drain(fd, deadline, done=None):
    """Read from fd until done(text) holds, the deadline passes, or it closes."""
    text = ""
    while True:
        if done is not None and done(text):
            break
        left = deadline - time.monotonic()
        if left <= 0:
            break
        ready, _, _ = select.select([fd], [], [], left)
        if not ready:
            break
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            # A pty master reports EIO once the last slave is closed.
            break
        if not chunk:
            break
        text += chunk.decode("utf-8", "replace")
    return text


def normalize(text):
    """A terminal's line discipline turns each \\n into \\r\\n on the way out."""
    return text.replace("\r\n", "\n")


def release(master):
    """Send the byte the program is blocked on; it has gone if the write fails."""
    try:
        os.write(master, b"\n")
    except OSError:
        pass


def run_on_terminal(program, env):
    """Run the program with fds 0, 1 and 2 on a pty: (output while it blocks, output after)."""
    pid, master = pty.fork()
    if pid == 0:
        try:
            os.execve(program, [program], env)
        finally:
            os._exit(127)
    running = drain(master, time.monotonic() + TIMEOUT, lambda t: t.count("\n") >= 2)
    release(master)
    # The program blocks again after this line, so it is read from a live pty:
    # the master loses whatever is still buffered once the last slave closes.
    rest = drain(master, time.monotonic() + TIMEOUT, lambda t: "bye\n" in normalize(t))
    release(master)
    _, status = os.waitpid(pid, 0)
    os.close(master)
    return normalize(running), normalize(rest), os.waitstatus_to_exitcode(status)


def run_on_pipe(program, env):
    """Run the program with fds 0, 1 and 2 on pipes: (stderr, stdout while it blocks, stdout)."""
    with subprocess.Popen(
        [program],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    ) as proc:
        err = drain(proc.stderr.fileno(), time.monotonic() + TIMEOUT, lambda t: "\n" in t)
        running = drain(proc.stdout.fileno(), time.monotonic() + QUIET)
        out, more = proc.communicate(b"\n", timeout=TIMEOUT)
        return err + more.decode(), running, running + out.decode(), proc.returncode


def compile_program(args, work, stem):
    """Compile one fort program for the target, as the language harness does (toolchain.md 2)."""
    source = os.path.join(HERE, "tty", stem + ".ft")
    program = os.path.join(work, stem)
    argv = [args.fort, "--cc", args.cc, "--std-dir", args.std_dir, "-o", program, source]
    done = subprocess.run(argv, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        print("tty: compiling %s failed:\n%s%s" % (source, done.stdout, done.stderr), end="")
        return None
    return program


def check(failures, what, actual, expected):
    if actual != expected:
        failures.append("tty: %s: got %r, expected %r" % (what, actual, expected))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", default="build/debug/fort", help="the compiler to drive")
    parser.add_argument("--std-dir", default="build/debug/std", help="passed as --std-dir")
    parser.add_argument("--cc", default="clang", help="clang for --cc and the link")
    args = parser.parse_args()

    for tool in (args.fort, args.cc):
        if shutil.which(tool) is None:
            print("tty: %s not found (build the fort target first)" % tool, file=sys.stderr)
            return 2

    env = dict(os.environ)
    env.setdefault("QEMU_LD_PREFIX", DEFAULT_QEMU_LD_PREFIX)
    failures = []
    with tempfile.TemporaryDirectory() as work:
        for label, stem in SOURCES:
            program = compile_program(args, work, stem)
            if program is None:
                return 1
            running, rest, status = run_on_terminal(program, env)
            # On a terminal the runtime flushes each line as the program writes it. Both lines
            # are there while the program still runs, in the order it printed them (D11.5).
            check(failures, label + ", terminal, while running", running, "out\nerr\n")
            check(failures, label + ", terminal, after the read", "bye\n" in rest, True)
            check(failures, label + ", terminal, exit status", status, 0)

            err, running, out, status = run_on_pipe(program, env)
            # On a pipe the runtime still does not buffer stderr, and stdout still waits for a
            # flush. A redirected program gives the same bytes in the same few writes (D11.5).
            check(failures, label + ", pipe, stderr", err, "err\n")
            check(failures, label + ", pipe, stdout while running", running, "")
            check(failures, label + ", pipe, stdout after exit", out, "out\nbye\n")
            check(failures, label + ", pipe, exit status", status, 0)

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
