#!/usr/bin/env python3
"""Hold the interface file of `std` against its sources (D9.11, module-system.md 10.3).

1. `fort --lib std <std> --emit-interface` writes the same bytes in two runs, and from a copy of
   the root at another path whose files were created in the other order.
2. A program that imports every section of the interface checks with no diagnostic, and the
   writer writes each section again as the same text: the interface file is a fixed point.
3. Each program of the language corpus checks to the same error lines (file, line, column and
   text) and the same status against the interface as against the sources. A note can point into
   the interface file, so notes are not compared.

The check against the interface runs test/library/checker.ft, the test-only path that reads an
interface file in place of `--use`.

Exit 0 when all three hold, 1 when one does not, or 2 for an environment failure.
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
# Cross-compiled programs run under qemu-user, which needs the target's libraries.
DEFAULT_QEMU_LD_PREFIX = "/usr/x86_64-linux-gnu"
TIMEOUT = 300

sys.path.insert(0, str(LANG))
import run_tests  # noqa: E402


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard library root of the target")
    parser.add_argument("--cc", required=True, help="the clang that links the checker")
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="checks run in parallel")
    return parser.parse_args(argv)


def run(argv, cwd=None):
    env = dict(os.environ)
    env.setdefault("QEMU_LD_PREFIX", DEFAULT_QEMU_LD_PREFIX)
    return subprocess.run(argv, cwd=cwd, env=env, capture_output=True, timeout=TIMEOUT)


def emit(fort, root):
    proc = run([fort, "--lib", "std", str(root), "--emit-interface"])
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        raise SystemExit("interface_test: --emit-interface exited %d" % proc.returncode)
    return proc.stdout


def copy_reversed(source, target):
    """Copy the tree `source` to `target`, each directory's files created in reverse order."""
    target.mkdir(parents=True)
    for entry in sorted(source.iterdir(), reverse=True):
        if entry.is_dir():
            copy_reversed(entry, target / entry.name)
        else:
            shutil.copyfile(entry, target / entry.name)


def check_determinism(args, work):
    first = emit(args.fort, args.std_dir)
    second = emit(args.fort, args.std_dir)
    copy = work / "elsewhere" / "deep" / "std"
    copy_reversed(pathlib.Path(args.std_dir), copy)
    third = emit(args.fort, copy)
    problems = []
    if first != second:
        problems.append("two runs wrote different interface files")
    if first != third:
        problems.append("a copy of the root at another path wrote another interface file")
    return first, problems


def build_checker(args, work):
    checker = work / "checker"
    proc = run([args.fort, "--std-dir", args.std_dir, "--cc", args.cc,
                "-I", str(SOURCE_ROOT / "src" / "fort"), "-o", str(checker),
                str(HERE / "checker.ft")])
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        raise SystemExit("interface_test: the checker did not build (exit %d)" % proc.returncode)
    return checker


def check_every_section(checker, interface, text, work):
    sections = [line.split(" ", 2)[2] for line in text.decode().splitlines()
                if line.startswith("//! module ")]
    entry = work / "every_section.ft"
    lines = ["import %s as m%d;" % (path, k) for k, path in enumerate(sections)]
    entry.write_text("\n".join(lines) + "\n")
    proc = run([str(checker), str(interface), "--check", str(entry)])
    if proc.returncode != 0 or proc.stderr:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        return len(sections), ["a program that imports every section did not check"]
    return len(sections), []


def compare(args, checker, interface, test):
    source = run([args.fort, "--check", "--std-dir", args.std_dir] + test.flags + [test.entry],
                 cwd=LANG)
    against = run([str(checker), str(interface), "--check"] + test.flags + [test.entry],
                  cwd=LANG)
    want = run_tests.parse_diagnostics(source.stderr, LANG)
    got = run_tests.parse_diagnostics(against.stderr, LANG)
    if source.returncode == against.returncode and want == got:
        return test, bool(want), ""
    return test, bool(want), "%s: exit %d and %d; source %s; interface %s" % (
        test.path, source.returncode, against.returncode, want[:2], got[:2])


def check_corpus(args, checker, interface):
    tests, problems = run_tests.discover(LANG)
    if problems:
        return 0, 0, ["the corpus has %d problems" % len(problems)]
    for test in tests:
        run_tests.load_test(LANG, test)
    with_errors = 0
    differences = []
    with concurrent.futures.ThreadPoolExecutor(max(1, args.j)) as pool:
        for test, erred, difference in pool.map(
                lambda t: compare(args, checker, interface, t), tests):
            with_errors += erred
            if difference:
                differences.append(difference)
    return len(tests), with_errors, differences


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    with tempfile.TemporaryDirectory(prefix="fort-interface-") as tmp:
        work = pathlib.Path(tmp)
        text, problems = check_determinism(args, work)
        interface = work / "std.flib" / "interface.ft"
        interface.parent.mkdir()
        interface.write_bytes(text)
        checker = build_checker(args, work)
        count, section_problems = check_every_section(checker, interface, text, work)
        problems.extend(section_problems)
        programs, with_errors, differences = check_corpus(args, checker, interface)
        problems.extend(differences)
    for problem in problems:
        print("FAIL", problem)
    print("interface_test: %d bytes, %d sections; %d programs, %d with errors, %d differ"
          % (len(text), count, programs, with_errors, len(differences)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
