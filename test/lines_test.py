"""Unit tests for tools/lines.py (D14.6, AGENTS.md "Build and test").

The glob matcher is the part worth pinning: the guest runs Python 3.12, where
pathlib.Path.full_match does not exist, so lines.py compiles the globs itself
and a mistake there would silently count the wrong files. Standard library
only, Python 3.12.
"""

import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import lines  # noqa: E402


class GlobMatching(unittest.TestCase):
    def matches(self, pattern, path):
        return lines.path_matches(path, [lines.glob_to_regex(pattern)])

    def test_a_star_stays_within_one_name(self):
        self.assertTrue(self.matches("test/*.c", "test/diag_test.c"))
        self.assertFalse(self.matches("test/*.c", "test/lang/ffi/helpers.c"))

    def test_two_stars_cross_any_number_of_directories(self):
        for path in ("test/lang/a.ft", "test/lang/run/arrays/001_x.ft"):
            self.assertTrue(self.matches("test/lang/**/*.ft", path), path)
        self.assertFalse(self.matches("test/lang/**/*.ft", "test/other/a.ft"))

    def test_the_whole_path_must_match(self):
        self.assertFalse(self.matches("src/bootstrap/*.c", "src/bootstrap/x.c.bak"))
        self.assertFalse(self.matches("src/bootstrap/*.c", "other/src/bootstrap/x.c"))

    def test_a_dot_is_not_a_wildcard(self):
        self.assertFalse(self.matches("test/*.c", "test/diagXc"))

    def test_the_real_globs_sort_the_files_they_are_meant_to(self):
        compiler = [lines.glob_to_regex(g) for g in lines.COMPILER_GLOBS]
        tests = [lines.glob_to_regex(g) for g in lines.TEST_GLOBS]
        for path in ("src/bootstrap/diag.c", "src/bootstrap/diag.h", "src/fort/lexer.ft"):
            self.assertTrue(lines.path_matches(path, compiler), path)
            self.assertFalse(lines.path_matches(path, tests), path)
        for path in ("test/diag_test.c", "test/types_helpers.h",
                     "test/lang/run/arrays/001_x.ft", "test/lang/ffi/helpers.c"):
            self.assertTrue(lines.path_matches(path, tests), path)
            self.assertFalse(lines.path_matches(path, compiler), path)
        for path in ("runtime/fort_rt.c", "std/io.ft", "tools/lines.py", "notes/decisions.md"):
            self.assertFalse(lines.path_matches(path, compiler), path)
            self.assertFalse(lines.path_matches(path, tests), path)


class Ratio(unittest.TestCase):
    def test_no_compiler_line_has_no_ratio(self):
        self.assertIsNone(lines.ratio_of(10, 0))

    def test_the_ratio_is_tests_over_compiler(self):
        self.assertAlmostEqual(lines.ratio_of(30, 10), 3.0)


class Since(unittest.TestCase):
    """End to end over a throwaway repository, so no fixture can go stale."""

    def run_lines(self, repo, *args):
        return subprocess.run(
            [sys.executable, str(ROOT / "tools" / "lines.py"), "--root", str(repo), *args],
            capture_output=True,
            text=True,
            cwd=repo,
        )

    def git(self, repo, *args):
        subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True)

    def make_repo(self, tmp, compiler_lines, test_lines):
        repo = Path(tmp)
        self.git(repo, "init", "-q")
        self.git(repo, "config", "user.email", "t@example.com")
        self.git(repo, "config", "user.name", "t")
        (repo / "src" / "bootstrap").mkdir(parents=True)
        (repo / "test").mkdir()
        (repo / "README").write_text("base\n")
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-qm", "base")
        (repo / "src" / "bootstrap" / "x.c").write_text("x\n" * compiler_lines)
        (repo / "test" / "x_test.c").write_text("t\n" * test_lines)
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-qm", "work")
        return repo

    def test_a_branch_above_the_minimum_passes(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_repo(tmp, 10, 40)
            got = self.run_lines(repo, "--since", "HEAD~1", "--min", "3.0")
            self.assertEqual(got.returncode, 0, got.stderr)
            self.assertIn("ratio:    4.00", got.stdout)

    def test_a_branch_below_the_minimum_fails(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_repo(tmp, 10, 20)
            got = self.run_lines(repo, "--since", "HEAD~1", "--min", "3.0")
            self.assertEqual(got.returncode, 1)
            self.assertIn("below the minimum", got.stderr)

    def test_a_branch_with_no_compiler_line_passes(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_repo(tmp, 0, 20)
            got = self.run_lines(repo, "--since", "HEAD~1", "--min", "3.0")
            self.assertEqual(got.returncode, 0, got.stderr)
            self.assertIn("no compiler lines added", got.stdout)


if __name__ == "__main__":
    unittest.main()
