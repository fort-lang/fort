#!/usr/bin/env python3
"""Unit tests of tools/check_comments.py: the scan and the command line.

The tool reports each real `/*` opener. It ignores openers inside strings, characters, and
`//` comments. Run with
`python3 -m unittest check_comments_test` from this directory.
"""

import contextlib
import io
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import check_comments  # noqa: E402


def lines_of(text):
    """The lines reported by a scan of text."""
    return [line for line, _ in check_comments.scan_text(text)]


def messages_of(text):
    """The messages reported by a scan of text."""
    return [message for _, message in check_comments.scan_text(text)]


class ScanTest(unittest.TestCase):
    def test_clean_file_has_no_problem(self):
        text = textwrap.dedent(
            """\
            // A clean source (D2.2).
            #include <stdint.h>

            int64_t twice(int64_t n) {
                return n * 2;  // doubled
            }
            """
        )
        self.assertEqual(check_comments.scan_text(text), [])

    def test_block_comment_is_reported_with_its_line(self):
        text = "// fine\nint a = 1;\n/* not fine */\n"
        self.assertEqual(check_comments.scan_text(text), [(3, "block comment")])

    def test_block_comment_in_the_middle_of_a_line_is_reported(self):
        text = "int a = f(/* n */ 3);\n"
        self.assertEqual(check_comments.scan_text(text), [(1, "block comment")])

    def test_every_block_comment_is_reported(self):
        text = "/* one */\nint a = 1;\n/* two\n   more */\n/* three */\n"
        self.assertEqual(lines_of(text), [1, 3, 5])

    def test_opener_inside_a_string_literal_is_ignored(self):
        text = 'const char* s = "/*";\nconst char* t = "a */ b";\n'
        self.assertEqual(check_comments.scan_text(text), [])

    def test_opener_inside_a_character_literal_is_ignored(self):
        text = "int magic = '/*';\nchar slash = '/';\n"
        self.assertEqual(check_comments.scan_text(text), [])

    def test_opener_inside_a_line_comment_is_ignored(self):
        text = "// a /* here is prose\nint a = 1;  // and /* here too\n"
        self.assertEqual(check_comments.scan_text(text), [])

    def test_escaped_quote_does_not_end_a_string(self):
        text = 'const char* s = "\\"/*\\"";\nint a = 1;\n'
        self.assertEqual(check_comments.scan_text(text), [])

    def test_escaped_quote_does_not_end_a_character_literal(self):
        text = "char q = '\\'';\nint a = '/*';\n"
        self.assertEqual(check_comments.scan_text(text), [])

    def test_unterminated_string_is_reported(self):
        text = 'const char* s = "oops;\nint a = 1;\n'
        self.assertEqual(check_comments.scan_text(text), [(1, "unterminated string literal")])

    def test_unterminated_string_at_the_end_of_the_file_is_reported(self):
        self.assertEqual(messages_of('"oops'), ["unterminated string literal"])

    def test_unterminated_character_literal_is_reported(self):
        text = "char c = 'a;\n"
        self.assertEqual(check_comments.scan_text(text), [(1, "unterminated character literal")])

    def test_scan_resumes_after_an_unterminated_string(self):
        text = 'const char* s = "oops;\n/* seen */\n'
        self.assertEqual(
            check_comments.scan_text(text),
            [(1, "unterminated string literal"), (2, "block comment")],
        )

    def test_a_line_continuation_inside_a_string_counts_a_line(self):
        text = 'const char* s = "a\\\nb";\n/* here */\n'
        self.assertEqual(check_comments.scan_text(text), [(3, "block comment")])


class MainTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def run_main(self, argv):
        """Run main(argv), returning (status, stdout, stderr)."""
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = check_comments.main(argv)
        return status, out.getvalue(), err.getvalue()

    def test_clean_file_exits_zero_and_says_nothing(self):
        path = self.write("clean.c", "// fine\nint a = 1;\n")
        status, out, err = self.run_main([str(path)])
        self.assertEqual(status, 0)
        self.assertEqual(out, "")
        self.assertEqual(err, "")

    def test_block_comment_exits_non_zero_and_names_the_file_and_line(self):
        path = self.write("bad.c", "int a = 1;\n/* no */\n")
        status, out, err = self.run_main([str(path)])
        self.assertEqual(status, 1)
        self.assertEqual(out, f"{path}:2: block comment\n")
        self.assertIn("D2.2", err)

    def test_the_default_globs_cover_the_c_sources(self):
        self.write("bootstrap0/src/a.c", "/* no */\n")
        self.write("bootstrap0/src/b.h", "/* no */\n")
        self.write("bootstrap0/test/c_test.c", "/* no */\n")
        self.write("test/lang/ffi/helpers.c", "/* no */\n")
        self.write("test/lang/run/x.ft", "/* not a C source */\n")
        self.write("notes/x.md", "/* not a C source */\n")
        status, out, err = self.run_main(["--root", str(self.root)])
        self.assertEqual(status, 1)
        reported = sorted(line.split(":")[0] for line in out.splitlines())
        self.assertEqual(
            reported,
            [
                "bootstrap0/src/a.c",
                "bootstrap0/src/b.h",
                "bootstrap0/test/c_test.c",
                "test/lang/ffi/helpers.c",
            ],
        )
        self.assertIn("4 block comment", err)

    def test_a_clean_tree_exits_zero(self):
        self.write("bootstrap0/src/a.c", '// fine\nconst char* s = "/*";\n')
        status, out, err = self.run_main(["--root", str(self.root)])
        self.assertEqual(status, 0)
        self.assertEqual(out, "")
        self.assertEqual(err, "")

    def test_the_repository_itself_is_clean(self):
        root = Path(__file__).resolve().parent.parent
        status, out, err = self.run_main(["--root", str(root)])
        self.assertEqual(status, 0, out + err)


if __name__ == "__main__":
    unittest.main()
