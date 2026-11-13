#!/usr/bin/env python3
"""Test tools/typed_pins.py over a small corpus and a compiler that a shell script stands for."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("typed_pins", REPOSITORY / "tools/typed_pins.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)

# The compiler: it needs the flags of the test, refuses a file with the words `compile error`,
# and writes a program from the lines `// run: <command>` of the entry.
COMPILER = """#!/bin/sh
case " $* " in *" -I support "*) ;; *) echo "no flags" >&2; exit 1 ;; esac
while [ $# -gt 1 ]; do [ "$1" = "-o" ] && out="$2"; shift; done
grep -q 'compile error' "$1" && { echo "$1: error" >&2; exit 1; }
{ echo '#!/bin/sh'; sed -n 's|^// run: ||p' "$1"; } > "$out"
chmod +x "$out"
"""

HEAD = "//! run\n//! flags: -I support\n//! stdout:\n"
OLD = HEAD + "//| old\n//| lines\n// The comment.\n// run: echo one\n// run: echo; echo ' three'\n"
NEW = HEAD + "//| one\n//|\n//|  three\n// The comment.\n// run: echo one\n// run: echo; echo ' three'\n"


class Corpus:
    """A corpus with the tests `files` of the typed family in a temporary directory."""

    def __init__(self, files):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.family = self.root / tool.CORPUS / tool.FAMILY
        self.family.mkdir(parents=True)
        self.fort = self.root / "fort"
        self.fort.write_text(COMPILER)
        self.fort.chmod(0o755)
        for name, text in files.items():
            (self.family / (name + ".ft")).write_text(text)

    def read(self, name):
        return (self.family / (name + ".ft")).read_text()

    def run(self, *args):
        """Runs the tool; returns (status, the sorted lines of stdout)."""
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = tool.main(["--root", str(self.root), "--fort", str(self.fort)] + list(args))
        return status, sorted(out.getvalue().splitlines())

    def close(self):
        self.dir.cleanup()


class TypedPinsTest(unittest.TestCase):
    def corpus(self, files):
        corpus = Corpus(files)
        self.addCleanup(corpus.close)
        return corpus

    def test_pinned_replaces_the_lines_after_the_directive_only(self):
        self.assertEqual(tool.pinned(OLD, "one\n\n three\n"), NEW)

    def test_pinned_is_stable_on_its_own_result(self):
        self.assertEqual(tool.pinned(NEW, "one\n\n three\n"), NEW)

    def test_pinned_takes_no_stdout_and_no_old_line(self):
        self.assertEqual(tool.pinned(HEAD + "fn main() i32 {\n", ""), HEAD + "fn main() i32 {\n")
        self.assertEqual(tool.pinned(HEAD + "fn main() i32 {\n", "a\n"),
                         HEAD + "//| a\nfn main() i32 {\n")

    def test_pinned_keeps_a_pin_line_that_stands_after_another_line(self):
        text = HEAD + "//| old\n// A comment.\n//| no pin\n"
        self.assertEqual(tool.pinned(text, "new\n"), HEAD + "//| new\n// A comment.\n//| no pin\n")

    def test_pinned_refuses_a_file_with_no_stdout_directive(self):
        with self.assertRaises(ValueError):
            tool.pinned("//! run\n//| old\n", "new\n")

    def test_the_default_tests_are_the_8_parts_and_the_cells(self):
        self.assertEqual(len(tool.PINNED), 9)
        self.assertEqual(tool.PINNED[0], "part_1_test")
        self.assertEqual(tool.PINNED[7:], ["part_8_test", "cells_test"])

    def test_a_run_writes_the_stdout_and_a_second_run_changes_nothing(self):
        corpus = self.corpus({"a_test": OLD, "b_test": NEW})
        self.assertEqual(corpus.run("a_test", "b_test"),
                         (0, ["a_test.ft: written", "b_test.ft: same"]))
        self.assertEqual(corpus.read("a_test"), NEW)
        self.assertEqual(corpus.read("b_test"), NEW)
        self.assertEqual(corpus.run("a_test"), (0, ["a_test.ft: same"]))

    def test_check_writes_nothing_and_exits_1_on_a_change(self):
        corpus = self.corpus({"a_test": OLD, "b_test": NEW})
        self.assertEqual(corpus.run("--check", "a_test", "b_test"),
                         (1, ["a_test.ft: differs", "b_test.ft: same"]))
        self.assertEqual(corpus.read("a_test"), OLD)
        self.assertEqual(corpus.run("--check", "b_test"), (0, ["b_test.ft: same"]))

    def test_a_run_that_exits_1_writes_no_file(self):
        text = OLD + "// run: exit 1\n"
        corpus = self.corpus({"a_test": text})
        status, lines = corpus.run("a_test")
        self.assertEqual(status, 1)
        self.assertEqual(len(lines), 1)
        self.assertIn("exited 1", lines[0])
        self.assertEqual(corpus.read("a_test"), text)

    def test_a_compile_that_exits_1_writes_no_file(self):
        text = OLD + "// compile error\n"
        corpus = self.corpus({"a_test": text})
        status, lines = corpus.run("a_test")
        self.assertEqual(status, 1)
        self.assertIn("a_test.ft: error", lines[0])
        self.assertEqual(corpus.read("a_test"), text)

    def test_a_file_with_no_flags_does_not_reach_the_program(self):
        text = OLD.replace("//! flags: -I support\n", "")
        corpus = self.corpus({"a_test": text})
        status, lines = corpus.run("a_test")
        self.assertEqual(status, 1)
        self.assertIn("no flags", lines[0])
        self.assertEqual(corpus.read("a_test"), text)


if __name__ == "__main__":
    unittest.main()
