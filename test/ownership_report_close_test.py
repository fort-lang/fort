#!/usr/bin/env python3
"""Make the close of the ownership report file fail, and check exit 2 and the prior report.

The probe is a seccomp user-notification filter on Linux. The compiler runs under a filter that
stops each close system call. This supervisor reads the descriptor of each stopped call through
/proc. It fails the close of the non-empty temporary report file with EIO and lets every other
close run. The kernel then returns that error to the compiler. The compiler has no failure hook,
and this test adds none: the failed close is the system call result that the compiler reads.

Linux is the named host of this probe. Darwin has no system call filter that a test can install,
so CMake registers the test on Linux only.
"""

import argparse
# socket.send_fds imports array. The child calls send_fds after the filter is on, and an import
# there can open and close a file, which would wait for this process. Import it here first.
import array  # noqa: F401
import ctypes
import errno
import json
import os
from pathlib import Path
import platform
import select
import socket
import struct
import subprocess
import sys
import tempfile
import unittest


OPTIONS = None
# The exit status of a run that skipped a case. CMakeLists.txt names it as SKIP_RETURN_CODE.
SKIP_STATUS = 77

PR_SET_NO_NEW_PRIVS = 38
SECCOMP_SET_MODE_FILTER = 1
SECCOMP_FILTER_FLAG_NEW_LISTENER = 1 << 3
SECCOMP_RET_USER_NOTIF = 0x7FC00000
SECCOMP_RET_ALLOW = 0x7FFF0000
SECCOMP_USER_NOTIF_FLAG_CONTINUE = 1
# _IOWR('!', 0, struct seccomp_notif) and _IOWR('!', 1, struct seccomp_notif_resp).
NOTIF_SIZE = 80
RESP_SIZE = 24
IOCTL_RECV = (3 << 30) | (NOTIF_SIZE << 16) | (ord("!") << 8) | 0
IOCTL_SEND = (3 << 30) | (RESP_SIZE << 16) | (ord("!") << 8) | 1
# The host machine: its audit architecture, the number of seccomp, and the number of close.
MACHINES = {
    "x86_64": (0xC000003E, 317, 3),
    "aarch64": (0xC00000B7, 277, 57),
}
# BPF opcodes: load a word at an absolute offset, jump when equal, return a constant.
BPF_LD_ABS = 0x20
BPF_JEQ = 0x15
BPF_RET = 0x06


class SockFilter(ctypes.Structure):
    _fields_ = [("code", ctypes.c_ushort), ("jt", ctypes.c_ubyte), ("jf", ctypes.c_ubyte),
                ("k", ctypes.c_uint)]


class SockFprog(ctypes.Structure):
    _fields_ = [("len", ctypes.c_ushort), ("filter", ctypes.POINTER(SockFilter))]


def close_filter():
    """The BPF program: stop each close of the host architecture; allow every other call."""
    arch, _, close = MACHINES[platform.machine()]
    program = (SockFilter * 6)(
        SockFilter(BPF_LD_ABS, 0, 0, 4),
        SockFilter(BPF_JEQ, 0, 3, arch),
        SockFilter(BPF_LD_ABS, 0, 0, 0),
        SockFilter(BPF_JEQ, 0, 1, close),
        SockFilter(BPF_RET, 0, 0, SECCOMP_RET_USER_NOTIF),
        SockFilter(BPF_RET, 0, 0, SECCOMP_RET_ALLOW),
    )
    return program


class Supervisor:
    """Runs one command under the close filter and answers each stopped close."""

    def __init__(self, temporary_prefix, inject):
        self.prefix = temporary_prefix
        self.inject = inject
        self.injected = []
        self.closes = 0

    def install(self):
        # The child runs this between fork and exec. The listener fd is close-on-exec, and the
        # child closes nothing after it: each later close would wait for the supervisor.
        # Before the filter is on, a failure sends its reason, and the child then stops.
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0:
            self.refuse("prctl(PR_SET_NO_NEW_PRIVS)", ctypes.get_errno())
        _, seccomp, _ = MACHINES[platform.machine()]
        program = self.program
        listener = libc.syscall(seccomp, SECCOMP_SET_MODE_FILTER,
                                SECCOMP_FILTER_FLAG_NEW_LISTENER,
                                ctypes.byref(SockFprog(len(program), program)))
        if listener < 0:
            self.refuse("seccomp(SECCOMP_FILTER_FLAG_NEW_LISTENER)", ctypes.get_errno())
        socket.send_fds(self.child_end, [b"L"], [listener])

    def refuse(self, call, number):
        self.child_end.sendall(("E%s failed: %s" % (call, os.strerror(number))).encode())
        raise OSError(number, call)

    def run(self, argv, cwd):
        self.program = close_filter()
        parent_end, self.child_end = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        # Files, not pipes, take the output: a full pipe would stop the compiler while this
        # process waits for its next close.
        with parent_end, self.child_end, tempfile.TemporaryFile() as out, \
                tempfile.TemporaryFile() as err:
            try:
                process = subprocess.Popen(argv, cwd=cwd, stdout=out, stderr=err,
                                           close_fds=False, preexec_fn=self.install)
            except subprocess.SubprocessError:
                # The child has ended. With no copy of its end left open, recv returns at once,
                # with its reason or with nothing.
                self.child_end.close()
                reason = parent_end.recv(256).decode(errors="replace")
                if reason.startswith("E"):
                    raise unittest.SkipTest("this kernel gives no seccomp user notification: "
                                            + reason[1:]) from None
                raise
            # The child sent the listener before exec, and exec closed its copy. This process
            # closes its own copy, so a child that sent nothing gives end of file, not a wait.
            self.child_end.close()
            _, fds, _, _ = socket.recv_fds(parent_end, 1, 1)
            if not fds:
                raise OSError("the child sent no seccomp listener")
            listener = fds[0]
            try:
                self.serve(listener, process)
            finally:
                os.close(listener)
            process.wait(timeout=120)
            out.seek(0)
            err.seek(0)
            return subprocess.CompletedProcess(argv, process.returncode, out.read(), err.read())

    def serve(self, listener, process):
        libc = ctypes.CDLL(None, use_errno=True)
        poller = select.poll()
        poller.register(listener, select.POLLIN)
        while True:
            events = poller.poll(100)
            if not events:
                if process.poll() is not None:
                    return
                continue
            if any(mask & (select.POLLHUP | select.POLLERR) for _, mask in events) and \
                    not any(mask & select.POLLIN for _, mask in events):
                return
            notification = ctypes.create_string_buffer(NOTIF_SIZE)
            if libc.ioctl(listener, IOCTL_RECV, notification) != 0:
                if ctypes.get_errno() in (errno.ENOENT, errno.EINTR):
                    continue
                raise OSError(ctypes.get_errno(), "SECCOMP_IOCTL_NOTIF_RECV")
            ident, pid, _ = struct.unpack_from("=QII", notification.raw, 0)
            descriptor = struct.unpack_from("=Q", notification.raw, 32)[0] & 0xFFFFFFFF
            self.closes += 1
            error = 0
            flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE
            if self.inject and self.is_report(pid, descriptor):
                error = -errno.EIO
                flags = 0
            response = struct.pack("=QqiI", ident, 0, error, flags)
            if libc.ioctl(listener, IOCTL_SEND, ctypes.create_string_buffer(response, RESP_SIZE)) \
                    != 0 and ctypes.get_errno() != errno.ENOENT:
                raise OSError(ctypes.get_errno(), "SECCOMP_IOCTL_NOTIF_SEND")

    def is_report(self, pid, descriptor):
        link = "/proc/%d/fd/%d" % (pid, descriptor)
        try:
            target = os.readlink(link)
            size = os.stat(link).st_size
        except OSError:
            return False
        # The publication temporary holds the document. An empty one is another probe.
        if target.startswith(self.prefix) and size > 0:
            self.injected.append(target)
            return True
        return False


@unittest.skipUnless(sys.platform.startswith("linux"), "the close probe needs Linux seccomp")
class ReportCloseTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="fort-report-close-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.entry = self.root / "main.ft"
        self.entry.write_text("fn leak() void {\n    i32 mut* own p = new(i32);\n}\n"
                              "fn main() i32 { return 0; }\n")
        # The report directory differs from the output directory, so the absent-name probe of
        # the report path creates no temporary file there.
        reports = self.root / "reports"
        reports.mkdir()
        self.report = reports / "report.json"
        self.output = self.root / "output.ll"

    def invoke(self, check, inject):
        argv = [OPTIONS.fort, "--std-dir", OPTIONS.std_dir, "--ownership-check",
                "--ownership-report", str(self.report)]
        argv += ["--check", "--json"] if check else ["-S", "-o", str(self.output)]
        argv.append(str(self.entry))
        supervisor = Supervisor(str(self.report) + ".tmp.", inject)
        result = supervisor.run(argv, self.root)
        self.assertGreater(supervisor.closes, 0)
        self.assertFalse(list(self.root.rglob("*.tmp.*")))
        return result, supervisor

    def test_a_failed_report_close_exits_two_and_keeps_the_prior_report(self):
        for check in (True, False):
            with self.subTest(check=check):
                self.report.write_bytes(b"prior report\n")
                self.output.write_bytes(b"prior output\n")
                result, supervisor = self.invoke(check, inject=True)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(b"fort: error: cannot write ownership report", result.stderr)
                self.assertEqual(result.stdout, b"")
                self.assertEqual(len(supervisor.injected), 1, supervisor.injected)
                self.assertTrue(supervisor.injected[0].startswith(str(self.report) + ".tmp."))
                self.assertEqual(self.report.read_bytes(), b"prior report\n")
                self.assertEqual(self.output.read_bytes(), b"prior output\n")

    def test_the_supervisor_alone_changes_no_outcome(self):
        # The control: the same filter answers each close with its own result.
        for check in (True, False):
            with self.subTest(check=check):
                self.report.write_bytes(b"prior report\n")
                result, supervisor = self.invoke(check, inject=False)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(b"lost ownership of 'p'", result.stdout + result.stderr)
                self.assertEqual(supervisor.injected, [])
                document = json.loads(self.report.read_text())
                self.assertEqual(document["kind"], "fort-ownership-report")
                self.assertEqual(document["exit_status"], 1)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True)
    parser.add_argument("--std-dir", required=True)
    OPTIONS, remaining = parser.parse_known_args()
    OPTIONS.fort = str(Path(OPTIONS.fort).resolve())
    OPTIONS.std_dir = str(Path(OPTIONS.std_dir).resolve())
    program = unittest.main(argv=[sys.argv[0], *remaining], exit=False)
    result = program.result
    if result.testsRun == 0:
        sys.exit(1)
    # A skipped probe proves nothing, so ctest reports it as "Not Run" (SKIP_RETURN_CODE).
    if result.wasSuccessful() and result.skipped:
        sys.exit(SKIP_STATUS)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
