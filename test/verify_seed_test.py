#!/usr/bin/env python3
"""Tests for the Linux and Darwin bootstrap seed verifier."""

from contextlib import redirect_stderr
import hashlib
import importlib.util
import io
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
VERIFY = ROOT / "tools" / "verify_seed.py"
BASELINE = ROOT / "tools" / "bootstrap.seed"
SOURCE_SHA = subprocess.run(
    ["git", "-C", str(ROOT), "rev-parse", "HEAD^0"],
    capture_output=True,
    check=True,
    text=True,
).stdout.strip()
VERIFY_SPEC = importlib.util.spec_from_file_location("verify_seed", VERIFY)
VERIFY_MODULE = importlib.util.module_from_spec(VERIFY_SPEC)
VERIFY_SPEC.loader.exec_module(VERIFY_MODULE)


class SeedVerifierTest(unittest.TestCase):
    """Break each seed contract rule and keep one control beside it."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="verify-seed-test-")
        self.work = Path(self.temp.name)
        self.std_dir = self.work / "std root"
        self.std_dir.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def seed(self, triple, status=0, write_module=True, replace_original=False):
        """Write an executable that records its arguments and emits one module."""
        path = self.work / "seed compiler"
        lines = [
            "#!/usr/bin/env python3",
            "import sys",
            "from pathlib import Path",
            "args = sys.argv[1:]",
            "Path(%r).write_text('\\n'.join(args), encoding='utf-8')"
            % str(self.work / "arguments"),
            "if '--target' in args:",
            "    raise SystemExit(70)",
        ]
        if replace_original:
            replacement = "#!/usr/bin/env python3\nraise SystemExit(88)\n"
            lines.extend(
                (
                    "original = Path(%r)" % str(path),
                    "if Path(__file__).resolve() == original.resolve():",
                    "    raise SystemExit(71)",
                    "original.write_text(%r, encoding='utf-8')" % replacement,
                    "original.chmod(0o700)",
                )
            )
        if write_module:
            lines.extend(
                (
                    "output = args[args.index('-o') + 1]",
                    "Path(output).write_text(%r, encoding='utf-8')" % triple,
                )
            )
        lines.append("raise SystemExit(%d)" % status)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def run_verify(self, target, seed, std_dir=None, ref=None):
        """Run the verifier with one test seed."""
        command = [str(VERIFY), target, str(seed), str(std_dir or self.std_dir)]
        if ref is not None:
            command.extend(("--ref", str(ref)))
        return subprocess.run(command, capture_output=True, text=True, check=False)

    def assert_identity(self, target, triple):
        """Check one accepted target and its complete identity record."""
        seed = self.seed('target triple = "%s"\n' % triple)
        result = self.run_verify(target, seed)
        self.assertEqual(result.returncode, 0, result.stderr)
        digest = hashlib.sha256(seed.read_bytes()).hexdigest()
        self.assertEqual(
            result.stdout.splitlines(),
            [
                "seed target: %s" % target,
                "seed path: %s" % seed.resolve(),
                "seed SHA-256: %s" % digest,
                "source baseline SHA: %s"
                % BASELINE.read_text(encoding="utf-8").split()[-1],
                "default triple: %s" % triple,
            ],
        )
        arguments = (self.work / "arguments").read_text(encoding="utf-8").splitlines()
        self.assertNotIn("--target", arguments)
        self.assertEqual(arguments.count("-S"), 1)
        self.assertIn(str(self.std_dir.resolve()), arguments)

    def test_linux_seed_uses_its_default_triple(self):
        self.assert_identity("linux", "x86_64-unknown-linux-gnu")

    def test_darwin_seed_uses_its_default_triple(self):
        self.assert_identity("darwin", "arm64-apple-macosx26.6.2")

    def test_the_hash_and_triple_come_from_one_executable_snapshot(self):
        triple = "x86_64-unknown-linux-gnu"
        seed = self.seed(
            'target triple = "%s"\n' % triple,
            replace_original=True,
        )
        snapshot_hash = hashlib.sha256(seed.read_bytes()).hexdigest()
        result = self.run_verify("linux", seed)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("seed SHA-256: %s\n" % snapshot_hash, result.stdout)
        self.assertIn("default triple: %s\n" % triple, result.stdout)
        self.assertNotEqual(hashlib.sha256(seed.read_bytes()).hexdigest(), snapshot_hash)

    def test_a_third_target_is_a_usage_error_and_does_not_run_the_seed(self):
        seed = self.seed('target triple = "aarch64-unknown-freebsd"\n')
        (self.work / "arguments").unlink(missing_ok=True)
        result = self.run_verify("freebsd", seed)
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid choice: 'freebsd'", result.stderr)
        self.assertFalse((self.work / "arguments").exists())

    def test_each_target_rejects_the_other_default_triple(self):
        cases = (
            ("linux", "arm64-apple-macosx26.6.2"),
            ("darwin", "x86_64-unknown-linux-gnu"),
        )
        for target, triple in cases:
            with self.subTest(target=target):
                result = self.run_verify(
                    target, self.seed('target triple = "%s"\n' % triple))
                self.assertEqual(result.returncode, 1)
                self.assertIn("seed emitted default triple", result.stderr)
                self.assertEqual(result.stdout, "")

    def test_darwin_requires_three_numeric_version_parts(self):
        for triple in ("arm64-apple-macosx26.6", "arm64-apple-macosx26.6.beta"):
            with self.subTest(triple=triple):
                result = self.run_verify(
                    "darwin", self.seed('target triple = "%s"\n' % triple))
                self.assertEqual(result.returncode, 1)

    def test_the_seed_must_write_exactly_one_target_triple(self):
        cases = (
            "define i32 @main() { ret i32 0 }\n",
            'target triple = "x86_64-unknown-linux-gnu"\n'
            'target triple = "x86_64-unknown-linux-gnu"\n',
        )
        for module in cases:
            with self.subTest(module=module):
                result = self.run_verify("linux", self.seed(module))
                self.assertEqual(result.returncode, 1)
                self.assertIn("exactly one target triple", result.stderr)

    def test_the_seed_must_be_an_executable_file(self):
        missing = self.work / "missing"
        result = self.run_verify("linux", missing)
        self.assertEqual(result.returncode, 1)
        self.assertIn("cannot resolve seed", result.stderr)
        directory = self.work / "directory"
        directory.mkdir()
        result = self.run_verify("linux", directory)
        self.assertEqual(result.returncode, 1)
        self.assertIn("seed is not a file", result.stderr)
        seed = self.seed('target triple = "x86_64-unknown-linux-gnu"\n')
        seed.chmod(stat.S_IRUSR | stat.S_IWUSR)
        result = self.run_verify("linux", seed)
        self.assertEqual(result.returncode, 1)
        self.assertIn("seed is not executable", result.stderr)

    def test_the_standard_root_must_be_a_directory(self):
        seed = self.seed('target triple = "x86_64-unknown-linux-gnu"\n')
        result = self.run_verify("linux", seed, self.work / "missing std")
        self.assertEqual(result.returncode, 1)
        self.assertIn("cannot resolve standard root", result.stderr)
        regular = self.work / "not a directory"
        regular.write_text("x", encoding="utf-8")
        result = self.run_verify("linux", seed, regular)
        self.assertEqual(result.returncode, 1)
        self.assertIn("standard root is not a directory", result.stderr)

    def test_a_failed_seed_and_a_missing_module_write_no_identity(self):
        result = self.run_verify(
            "linux",
            self.seed('target triple = "x86_64-unknown-linux-gnu"\n', status=9),
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("seed exited 9", result.stderr)
        self.assertEqual(result.stdout, "")
        result = self.run_verify(
            "linux",
            self.seed("", write_module=False),
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("seed wrote no readable IR module", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_the_baseline_has_one_full_commit_record(self):
        seed = self.seed('target triple = "x86_64-unknown-linux-gnu"\n')
        cases = (
            ("empty", "# none\n", "exactly one record"),
            ("two", "baseline %s\nbaseline %s\n" % (SOURCE_SHA, SOURCE_SHA),
             "exactly one record"),
            ("short", "baseline %s\n" % SOURCE_SHA[:12], "40-hex-sha"),
            ("upper", "baseline %s\n" % SOURCE_SHA.upper(), "40-hex-sha"),
            ("unknown", "baseline %s\n" % ("0" * 40), "not a commit"),
        )
        for name, content, message in cases:
            with self.subTest(name=name):
                ref = self.work / (name + ".seed")
                ref.write_text(content, encoding="utf-8")
                result = self.run_verify("linux", seed, ref=ref)
                self.assertEqual(result.returncode, 1)
                self.assertIn(message, result.stderr)
                self.assertEqual(result.stdout, "")

    def test_the_baseline_must_be_in_the_revision_history(self):
        repo = self.work / "repository"
        subprocess.run(("git", "init", "-q", str(repo)), check=True)
        subprocess.run(("git", "-C", str(repo), "config", "user.name", "fort"), check=True)
        subprocess.run(
            ("git", "-C", str(repo), "config", "user.email", "fort@example.invalid"),
            check=True,
        )
        subprocess.run(
            ("git", "-C", str(repo), "commit", "-q", "--allow-empty", "-m", "base"),
            check=True,
        )
        base = subprocess.run(
            ("git", "-C", str(repo), "rev-parse", "HEAD"),
            capture_output=True,
            check=True,
            text=True,
        ).stdout.strip()
        subprocess.run(
            ("git", "-C", str(repo), "commit", "-q", "--allow-empty", "-m", "head"),
            check=True,
        )
        head = subprocess.run(
            ("git", "-C", str(repo), "rev-parse", "HEAD"),
            capture_output=True,
            check=True,
            text=True,
        ).stdout.strip()
        subprocess.run(("git", "-C", str(repo), "checkout", "-q", "--detach", base), check=True)
        subprocess.run(
            ("git", "-C", str(repo), "commit", "-q", "--allow-empty", "-m", "outside"),
            check=True,
        )
        outside = subprocess.run(
            ("git", "-C", str(repo), "rev-parse", "HEAD"),
            capture_output=True,
            check=True,
            text=True,
        ).stdout.strip()
        subprocess.run(("git", "-C", str(repo), "checkout", "-q", "--detach", head), check=True)
        ref = self.work / "outside.seed"
        ref.write_text("baseline %s\n" % outside, encoding="utf-8")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit):
            VERIFY_MODULE.read_baseline(repo, ref)
        self.assertIn("not in the history of HEAD", error.getvalue())


if __name__ == "__main__":
    unittest.main()
