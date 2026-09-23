"""Unit tests for tools/lines.py.

The tests hold the glob matcher. The guest runs Python 3.12, which has no
pathlib.Path.full_match. Thus, lines.py compiles the globs. A mistake can count
the wrong files without an error. Standard library only; Python 3.12.
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
        self.assertFalse(self.matches("bootstrap/src/*.c", "bootstrap/src/x.c.bak"))
        self.assertFalse(self.matches("bootstrap/src/*.c", "other/bootstrap/src/x.c"))

    def test_a_dot_is_not_a_wildcard(self):
        self.assertFalse(self.matches("test/*.c", "test/diagXc"))

    def test_the_real_globs_sort_the_files_they_are_meant_to(self):
        source = [lines.glob_to_regex(g) for g in lines.SOURCE_GLOBS]
        tests = [lines.glob_to_regex(g) for g in lines.TEST_GLOBS]
        for path in ("bootstrap/src/diag.c", "bootstrap/src/diag.h", "src/fort/lexer.ft",
                     "std/darwin/libc.ft"):
            self.assertTrue(lines.path_matches(path, source), path)
            self.assertFalse(lines.path_matches(path, tests), path)
        for path in ("bootstrap/test/diag_test.c", "bootstrap/test/common/types_helpers.h",
                     "test/lang/run/arrays/001_x.ft", "test/lang/ffi/helpers.c",
                     "test/fort/containers_test.ft", "test/darwin/open_tail_probe.c"):
            self.assertTrue(lines.path_matches(path, tests), path)
            self.assertFalse(lines.path_matches(path, source), path)
        for path in ("tools/lines.py", "spec/decisions.md", "editors/vscode/extension.js"):
            self.assertFalse(lines.path_matches(path, source), path)
            self.assertFalse(lines.path_matches(path, tests), path)

    def test_the_runtime_counts_with_the_standard_library(self):
        """The runtime is std.rt, so no glob names a runtime directory."""
        source = [lines.glob_to_regex(g) for g in lines.SOURCE_GLOBS]
        tests = [lines.glob_to_regex(g) for g in lines.TEST_GLOBS]
        self.assertTrue(lines.path_matches("std/rt.ft", source))
        self.assertFalse(lines.path_matches("std/rt.ft", tests))
        # No glob names the removed C runtime.
        # A restored file there would count on neither side.
        for path in ("runtime/fort_rt.c", "runtime/fort_rt.h"):
            self.assertFalse(lines.path_matches(path, source), path)
            self.assertFalse(lines.path_matches(path, tests), path)

    def test_the_standard_library_is_source_and_not_test(self):
        """The project ships common and Darwin standard files as source."""
        source = [lines.glob_to_regex(g) for g in lines.SOURCE_GLOBS]
        tests = [lines.glob_to_regex(g) for g in lines.TEST_GLOBS]
        for path in ("std/io.ft", "std/strbuf.ft", "std/darwin/libc.ft"):
            self.assertTrue(lines.path_matches(path, source), path)
            self.assertFalse(lines.path_matches(path, tests), path)
        self.assertFalse(lines.path_matches("std/README.md", source))
        self.assertFalse(lines.path_matches("std/sub/deep.ft", source))

    def test_every_ft_file_under_test_counts_as_a_test(self):
        """Fixtures outside test/lang are test data too, test/highlight/scopes.ft above all."""
        tests = [lines.glob_to_regex(g) for g in lines.TEST_GLOBS]
        for path in ("test/highlight/scopes.ft", "test/fort_lint/good.ft", "test/bare.ft"):
            self.assertTrue(lines.path_matches(path, tests), path)

    def test_the_real_standard_library_is_counted(self):
        """End to end over the repository itself: the std files are in the file list."""
        counted = lines.collect(ROOT, lines.SOURCE_GLOBS)
        names = {p.name for p in counted if p.parent.name == "std"}
        self.assertIn("io.ft", names)
        self.assertGreaterEqual(len(names), 8)
        self.assertIn("rt.ft", names)
        self.assertIn(ROOT / "std/darwin/libc.ft", counted)

    def test_the_real_darwin_c_probe_is_counted(self):
        counted = lines.collect(ROOT, lines.TEST_GLOBS)
        self.assertIn(ROOT / "test/darwin/open_tail_probe.c", counted)


class Ratio(unittest.TestCase):
    def test_no_source_line_has_no_ratio(self):
        self.assertIsNone(lines.ratio_of(10, 0))

    def test_the_ratio_is_tests_over_source(self):
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

    def make_repo(self, tmp, source_lines, test_lines):
        repo = Path(tmp)
        self.git(repo, "init", "-q")
        self.git(repo, "config", "user.email", "t@example.com")
        self.git(repo, "config", "user.name", "t")
        (repo / "bootstrap" / "src").mkdir(parents=True)
        (repo / "bootstrap" / "test").mkdir()
        (repo / "README").write_text("base\n")
        self.git(repo, "add", "-A")
        self.git(repo, "commit", "-qm", "base")
        (repo / "bootstrap" / "src" / "x.c").write_text("x\n" * source_lines)
        (repo / "bootstrap" / "test" / "x_test.c").write_text("t\n" * test_lines)
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

    def test_a_branch_with_no_source_line_passes(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self.make_repo(tmp, 0, 20)
            got = self.run_lines(repo, "--since", "HEAD~1", "--min", "3.0")
            self.assertEqual(got.returncode, 0, got.stderr)
            self.assertIn("no source lines added", got.stdout)


if __name__ == "__main__":
    unittest.main()
