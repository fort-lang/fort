#!/usr/bin/env python3
"""Verify one user-supplied Linux or Darwin compiler seed."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


BASELINE_RE = re.compile(r"baseline ([0-9a-f]{40})")
TRIPLE_RE = re.compile(r'^target triple = "([^"]+)"$')
DARWIN_TRIPLE_RE = re.compile(r"arm64-apple-macosx[0-9]+\.[0-9]+\.[0-9]+")
LINUX_TRIPLE = "x86_64-unknown-linux-gnu"


def fail(message):
    """Report one verification failure."""
    print("verify_seed.py: %s" % message, file=sys.stderr)
    raise SystemExit(1)


def parse_args(argv):
    """Read the two target names and the seed inputs."""
    parser = argparse.ArgumentParser(
        description="verify one user-supplied fort bootstrap seed")
    parser.add_argument("target", choices=("linux", "darwin"))
    parser.add_argument("seed", help="executable fort seed")
    parser.add_argument("std_dir", help="standard root for the selected target")
    parser.add_argument(
        "--ref",
        default=str(Path(__file__).resolve().with_name("bootstrap.seed")),
        help="source baseline file",
    )
    return parser.parse_args(argv)


def resolve_seed(path):
    """Return the canonical executable seed path."""
    try:
        seed = Path(path).resolve(strict=True)
    except OSError as error:
        fail("cannot resolve seed '%s': %s" % (path, error))
    if not seed.is_file():
        fail("seed is not a file: %s" % seed)
    if not os.access(seed, os.X_OK):
        fail("seed is not executable: %s" % seed)
    return seed


def resolve_std_dir(path):
    """Return the canonical standard root path."""
    try:
        std_dir = Path(path).resolve(strict=True)
    except OSError as error:
        fail("cannot resolve standard root '%s': %s" % (path, error))
    if not std_dir.is_dir():
        fail("standard root is not a directory: %s" % std_dir)
    return std_dir


def read_baseline(root, path):
    """Read one full commit SHA from the baseline file."""
    try:
        lines = Path(path).read_text(encoding="utf-8").splitlines()
    except OSError as error:
        fail("cannot read source baseline '%s': %s" % (path, error))
    records = [line for line in lines if line and not line.startswith("#")]
    if len(records) != 1:
        fail("source baseline must contain exactly one record")
    match = BASELINE_RE.fullmatch(records[0])
    if match is None:
        fail("source baseline record must be 'baseline <40-hex-sha>'")
    source_sha = match.group(1)
    result = subprocess.run(
        ["git", "-C", str(root), "cat-file", "-e", source_sha + "^{commit}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        fail("source baseline is not a commit of this repository: %s" % source_sha)
    result = subprocess.run(
        ["git", "-C", str(root), "merge-base", "--is-ancestor", source_sha, "HEAD"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        fail("source baseline is not in the history of HEAD: %s" % source_sha)
    return source_sha


def sha256(path):
    """Return the lower-case SHA-256 value of one file."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            data = handle.read(1024 * 1024)
            if not data:
                return digest.hexdigest()
            digest.update(data)


def expected_triple(target, triple):
    """Return whether the emitted default triple matches the target."""
    if target == "linux":
        return triple == LINUX_TRIPLE
    return DARWIN_TRIPLE_RE.fullmatch(triple) is not None


def emitted_triple(seed, std_dir):
    """Compile one source without --target and return its one target triple."""
    with tempfile.TemporaryDirectory(prefix="fort-seed-") as directory:
        work = Path(directory)
        source = work / "main.ft"
        module = work / "main.ll"
        source.write_text("fn main() i32 { return 0; }\n", encoding="utf-8")
        command = [
            str(seed),
            "-S",
            "--std-dir",
            str(std_dir),
            "-o",
            str(module),
            str(source),
        ]
        try:
            result = subprocess.run(command, capture_output=True, text=True, check=False)
        except OSError as error:
            fail("cannot run seed: %s" % error)
        if result.returncode != 0:
            if result.stdout:
                sys.stderr.write(result.stdout)
            if result.stderr:
                sys.stderr.write(result.stderr)
            fail("seed exited %d while it emitted default-target IR" % result.returncode)
        try:
            text = module.read_text(encoding="utf-8")
        except OSError as error:
            fail("seed wrote no readable IR module: %s" % error)
    triples = []
    for line in text.splitlines():
        match = TRIPLE_RE.fullmatch(line)
        if match is not None:
            triples.append(match.group(1))
    if len(triples) != 1:
        fail("seed IR must contain exactly one target triple; found %d" % len(triples))
    return triples[0]


def snapshot_identity(seed, std_dir):
    """Hash and run one stable executable snapshot."""
    with tempfile.TemporaryDirectory(prefix="fort-seed-executable-") as directory:
        snapshot = Path(directory) / "fort"
        try:
            shutil.copyfile(seed, snapshot)
            snapshot.chmod(0o700)
            seed_hash = sha256(snapshot)
        except OSError as error:
            fail("cannot snapshot seed '%s': %s" % (seed, error))
        triple = emitted_triple(snapshot, std_dir)
    return seed_hash, triple


def main(argv=None):
    """Verify the seed and write its identity record."""
    args = parse_args(argv)
    root = Path(__file__).resolve().parent.parent
    seed = resolve_seed(args.seed)
    std_dir = resolve_std_dir(args.std_dir)
    source_sha = read_baseline(root, args.ref)
    seed_hash, triple = snapshot_identity(seed, std_dir)
    if not expected_triple(args.target, triple):
        fail("%s seed emitted default triple '%s'" % (args.target, triple))
    print("seed target: %s" % args.target)
    print("seed path: %s" % seed)
    print("seed SHA-256: %s" % seed_hash)
    print("source baseline SHA: %s" % source_sha)
    print("default triple: %s" % triple)


if __name__ == "__main__":
    main()
