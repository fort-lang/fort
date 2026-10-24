#!/usr/bin/env python3
"""Hold the artifact of `fort --lib std <std>` against a reading of D9.11 that shares no code
with the compiler (toolchain.md 2.1 and 2.2, module-system.md 10.4).

1. Two builds write `std.flib` into two directories, and each file has the same bytes:
   `interface.ft`, `lib.a`, `manifest` and the empty `summaries/`. Each build has a cache
   directory of its own, so both compile the library (toolchain.md 2.3).
2. The manifest is the text that this script writes from the table of toolchain.md 2.2, and
   Python's hashlib gives each hash again: the compiler executable, the sources of the root, the
   key, the interface file and the archive.
3. The IR module of the library passes `opt -passes=verify`, defines no C `main` and numbers its
   private data from 0. The IR module of the other target, from a standard root that this script
   assembles for it, passes the verifier and compiles to an object.
4. Each program of test/library/programs gives the same stdout, stderr and exit status when it
   is built with `--use std=<artifact>` as when it is built as one whole program. The build with
   `--use` links the object of the entry unit with the `lib.a` of the artifact. Each `declare`
   line of the IR module of the entry unit carries the types of the definition in the library
   module.

The archiver of macOS writes the time stamp of each member unless ZERO_AR_DATE is set. The
compiler runs the archiver with ZERO_AR_DATE=1 (toolchain.md 2.1, step 8), so this script removes
the variable from the environment of its builds: on macOS the two archives of check 1 are equal
only when the compiler sets it. D9.11 asks for byte-deterministic IR text and `interface.ft` only.
Each build names a cache directory in the temporary directory of the script, so no build writes
a cache entry beside the standard root or the programs.

Exit 0 when all four hold, 1 when one does not, or 2 for an environment failure.
"""

import argparse
import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SOURCE_ROOT = HERE.parent.parent
PROGRAMS = HERE / "programs"
# Cross-compiled programs run under qemu-user, which needs the target's libraries.
DEFAULT_QEMU_LD_PREFIX = "/usr/x86_64-linux-gnu"
TIMEOUT = 300
TARGETS = {
    "x86_64-linux-gnu": ("linux", "x86_64", "x86_64-unknown-linux-gnu"),
    "arm64-apple-macosx11.0.0": ("darwin", "aarch64", "arm64-apple-macosx11.0.0"),
}
# The arguments that each program of PROGRAMS is run with.
PROGRAM_ARGS = ["one", "two words"]
SEGMENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--std-dir", required=True, help="the standard root of the built target")
    parser.add_argument("--cc", required=True, help="the clang of the target")
    parser.add_argument("--target", required=True, choices=sorted(TARGETS))
    parser.add_argument("--opt", required=True, help="the LLVM verifier")
    parser.add_argument("--ar", default="ar", help="the archiver of the artifact")
    return parser.parse_args(argv)


def environment():
    env = dict(os.environ)
    env.setdefault("QEMU_LD_PREFIX", DEFAULT_QEMU_LD_PREFIX)
    env.pop("ZERO_AR_DATE", None)
    return env


def run(argv, cwd=None):
    return subprocess.run([str(a) for a in argv], cwd=cwd, env=environment(),
                          capture_output=True, timeout=TIMEOUT)


def must(argv, what, cwd=None):
    proc = run(argv, cwd=cwd)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.decode("utf-8", errors="replace"))
        raise SystemExit("artifact_test: %s exited %d" % (what, proc.returncode))
    return proc


# ---- an independent reading of the manifest -----------------------------------------------

def value(text):
    """A manifest value: 0x21 to 0x7E stay, except `\\`; every other byte is `\\xHH`."""
    out = []
    for byte in text.encode() if isinstance(text, str) else text:
        if 0x21 <= byte <= 0x7E and byte != 0x5C:
            out.append(chr(byte))
        else:
            out.append("\\x%02X" % byte)
    return "".join(out)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def modules_of(root):
    """The modules of the library `std` at `root`: (module path, opened path), in byte order."""
    found = []
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        rel = os.path.relpath(dirpath, root)
        parts = [] if rel == "." else rel.split(os.sep)
        if not all(SEGMENT.match(p) for p in parts):
            dirnames[:] = []
            continue
        for name in filenames:
            path = os.path.join(dirpath, name)
            stem = name[:-3]
            if name.endswith(".ft") and SEGMENT.match(stem) and not os.path.islink(path):
                module = ".".join(["std"] + parts + [stem])
                opened = root.rstrip("/") + "/" + "/".join(parts + [name])
                found.append((module, opened))
    return sorted(found, key=lambda m: m[0].encode())


def source_hash(root):
    digest = hashlib.sha256()
    for module, opened in modules_of(root):
        data = pathlib.Path(opened).read_bytes()
        digest.update(("%s\n%s\n%d\n" % (value(module), value(opened), len(data))).encode())
        digest.update(data)
    return digest.hexdigest()


def manifest_text(args, root, artifact):
    """The manifest that toolchain.md 2.2 gives for this build."""
    os_name, arch, _ = TARGETS[args.target]
    os_value = "linux" if os_name == "linux" else "macos"
    pairs = sorted([("target_os", os_value), ("target_arch", arch), ("target_abi", "")],
                   key=lambda p: p[0].encode())
    lines = ["fort-manifest 1", "library std",
             "compiler " + sha256(pathlib.Path(args.fort).read_bytes()),
             "target " + value(args.target), "mode checked", "bounds on"]
    lines += ["cfg %s %s" % (value(k), value(v)) for k, v in pairs]
    lines += ["cc " + value(args.cc), "ar " + value(args.ar), "source " + source_hash(root)]
    head = "".join(line + "\n" for line in lines)
    tail = ["key " + sha256(head.encode()),
            "interface " + sha256((artifact / "interface.ft").read_bytes()),
            "archive " + sha256((artifact / "lib.a").read_bytes()),
            "summaries 1"]
    return head + "".join(line + "\n" for line in tail)


# ---- the four checks ----------------------------------------------------------------------

def build_artifact(args, out):
    must([args.fort, "--cc", args.cc, "--ar", args.ar, "--lib", "std", args.std_dir, "-o", out,
          "--cache-dir", out.parent / "cache"], "fort --lib std")


def check_determinism(args, work):
    first, second = work / "first" / "std.flib", work / "second" / "std.flib"
    first.parent.mkdir()
    second.parent.mkdir()
    build_artifact(args, first)
    build_artifact(args, second)
    problems = []
    for artifact in (first, second):
        entries = sorted(p.name for p in artifact.iterdir())
        if entries != ["interface.ft", "lib.a", "manifest", "summaries"]:
            problems.append("%s holds %s" % (artifact, entries))
        if any((artifact / "summaries").iterdir()):
            problems.append("%s/summaries holds a file" % artifact)
    for name in ("interface.ft", "lib.a", "manifest"):
        if (first / name).read_bytes() != (second / name).read_bytes():
            problems.append("two builds wrote different bytes of %s" % name)
    return first, problems


def check_manifest(args, artifact):
    got = (artifact / "manifest").read_text()
    want = manifest_text(args, args.std_dir, artifact)
    if got != want:
        return ["the manifest differs:\n%s\nwant:\n%s" % (got, want)]
    return []


def check_ir(args, work):
    problems = []
    ir = work / "std.ll"
    must([args.fort, "--lib", "std", args.std_dir, "-S", "-o", ir], "fort --lib std -S")
    must([args.opt, "-passes=verify", "-disable-output", ir], "opt over std.ll")
    text = ir.read_text()
    if re.search(r"^define .*@main\(", text, re.M):
        problems.append("the library module defines a C main")
    for name in ("@.file.0 = ", "@.str.0 = "):
        if "\n" + name not in text:
            problems.append("the library module has no %s" % name)
    if "declare dso_local" in text or "external dso_local" in text:
        problems.append("the library module declares a fort definition")
    # the other target, from a root that holds std/*.ft and std/<other>/*.ft
    other = next(t for t in sorted(TARGETS) if t != args.target)
    root = work / "other-std"
    source = SOURCE_ROOT / "std"
    for path in sorted(source.rglob("*.ft")):
        rel = path.relative_to(source)
        if rel.parts[0] in ("linux", "darwin"):
            if rel.parts[0] != TARGETS[other][0]:
                continue
            rel = pathlib.Path(*rel.parts[1:])
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, root / rel)
    other_ir = work / "other.ll"
    must([args.fort, "--lib", "std", root, "-S", "--target", other, "-o", other_ir],
         "fort --lib std -S --target " + other)
    must([args.opt, "-passes=verify", "-disable-output", other_ir], "opt over other.ll")
    if not other_ir.read_text().startswith('target triple = "%s"\n' % TARGETS[other][2]):
        problems.append("the module of %s has another triple" % other)
    must([args.cc, "--target=" + other, "-O1", "-fPIE", "-Wno-override-module", "-c", "-o",
          work / "other.o", other_ir], "the compile of other.ll")
    return ir, problems


DECLARE = re.compile(r'^declare dso_local (.*?)(@"[^"]+")\((.*)\) #0$', re.M)
DEFINE = re.compile(r'^define dso_local (.*?)(@"[^"]+")\((.*)\) #\d+ \{$', re.M)


def without_names(params):
    return re.sub(r" %[^,]+(,|$)", r"\1", params)


def check_declarations(entry_ir, library):
    """Each `declare` of the entry unit has the result and the parameters of the definition."""
    problems = []
    defined = {m.group(2): (m.group(1), without_names(m.group(3)))
               for m in DEFINE.finditer(library)}
    declared = DECLARE.findall(entry_ir)
    for result, name, params in declared:
        if defined.get(name) != (result, params):
            problems.append("%s is declared as %r and defined as %r"
                            % (name, (result, params), defined.get(name)))
    if not declared:
        problems.append("the entry unit declares no function")
    return len(declared), problems


def run_program(binary):
    proc = run([binary] + PROGRAM_ARGS)
    return proc.returncode, proc.stdout, proc.stderr


def check_programs(args, work, artifact, library_ir):
    library = library_ir.read_text()
    use = ["--cc", args.cc, "--use", "std=%s" % artifact, "--cache-dir", work / "cache"]
    problems = []
    declarations = 0
    programs = sorted(PROGRAMS.glob("*.ft"))
    for program in programs:
        name = program.stem
        whole = work / ("whole_" + name)
        must([args.fort, "--std-dir", args.std_dir, "--cc", args.cc, "-o", whole, program],
             "the whole-program build of " + name)
        entry_ir = work / (name + ".ll")
        must([args.fort] + use + ["-S", "-o", entry_ir, program], "the entry unit of " + name)
        text = entry_ir.read_text()
        if re.search(r'^define .*@"std\.', text, re.M):
            problems.append("the entry unit of %s defines a function of std" % name)
        count, found = check_declarations(text, library)
        declarations += count
        problems.extend("%s: %s" % (name, p) for p in found)
        linked = work / ("linked_" + name)
        must([args.fort] + use + ["-o", linked, program], "the build of %s with --use" % name)
        want = run_program(whole)
        got = run_program(linked)
        if got != want:
            problems.append("%s: the linked program gives %r, the whole program %r"
                            % (name, got, want))
        if not want[1]:
            problems.append("%s writes nothing to stdout" % name)
    return len(programs), declarations, problems


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    with tempfile.TemporaryDirectory(prefix="fort-artifact-") as tmp:
        work = pathlib.Path(tmp)
        artifact, problems = check_determinism(args, work)
        problems.extend(check_manifest(args, artifact))
        library_ir, ir_problems = check_ir(args, work)
        problems.extend(ir_problems)
        programs, declarations, program_problems = check_programs(args, work, artifact,
                                                                  library_ir)
        problems.extend(program_problems)
        size = (artifact / "lib.a").stat().st_size
    for problem in problems:
        print("FAIL", problem)
    print("artifact_test: lib.a of %d bytes; %d programs linked, %d declarations checked; "
          "%d problems" % (size, programs, declarations, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
