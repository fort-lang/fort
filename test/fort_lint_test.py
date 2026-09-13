#!/usr/bin/env python3
"""Unit tests of tools/fort_lint.py, the D1.4 identifier check (AGENTS.md).

Two halves. The rules themselves are pure functions over the index records of
D20.3 and are tested here directly, one case per clause of the decision; the
plumbing that runs the compiler is tested against a fake `fort` written for
the occasion, so a document the real one never produces (no output, a crash, a
diagnostic) is covered too.

The second half runs the real compiler when the environment names one in
FORT_BINARY, as the ctest does: it lints test/fort_lint/bad_names.ft, which
holds one violation of every rule, and holds the whole verdict against the
list below. A rule that stopped firing fails there rather than going quiet.

Run with `python3 -m unittest fort_lint_test` from this directory. Standard
library only; Python 3.12.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
ROOT = TEST_DIR.parent
DECISIONS_PATH = ROOT / "spec" / "decisions.md"
sys.path.insert(0, str(ROOT / "tools"))

import check_decisions  # noqa: E402
import fort_lint  # noqa: E402

FIXTURE_DIR = TEST_DIR / "fort_lint"
SOURCE_GLOBS = fort_lint.SOURCE_GLOBS

# The whole verdict over test/fort_lint/bad_names.ft, in the order the tool
# prints it. Every line is one rule of D1.4 (the last one is the width check).
BAD_NAMES_PROBLEMS = [
    "test/fort_lint/bad_names.ft:5:19: module 'Mem' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:8:5: constant 'max_nodes' is not UPPER_CASE (D1.4)",
    "test/fort_lint/bad_names.ft:11:9: global 'Counter' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:13:8: struct 'Point' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:14:9: field 'X' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:17:6: enum 'Color' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:17:14: enum member 'Red' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:19:8: fn 'addTwo' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:19:19: parameter 'aValue' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:20:9: local 'Sum' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:26:11: local 'Point' is not lower_case (D1.4)",
    "test/fort_lint/bad_names.ft:26:11: local 'Point' takes the name of its type (D1.4)",
    "test/fort_lint/bad_names.ft:41:22: parameter 'pair' takes the name of its type (D1.4)",
    "test/fort_lint/bad_names.ft:45:101: line is 124 columns, over the 100 of the house style",
]


# The whole verdict over test/fort_lint/broken.ft: the checker's own error and
# the two names it resolved anyway, which a lint that stopped at the first
# diagnostic would never have judged.
BROKEN_PROBLEMS = [
    "test/fort_lint/broken.ft:1:1: fort could not check this closure: "
    "test/fort_lint/broken.ft:7:22: unknown name 'nowhere'",
    "test/fort_lint/broken.ft:6:9: local 'badName' is not lower_case (D1.4)",
    "test/fort_lint/broken.ft:10:8: fn 'alsoBad' is not lower_case (D1.4)",
]


def record(kind, name, type_text="", is_decl=True, file="a.ft", line=1, col=1):
    """One index record of D20.3, with only the members the lint reads."""
    return {
        "file": file,
        "line": line,
        "col": col,
        "name": name,
        "kind": kind,
        "type": type_text,
        "is_decl": is_decl,
    }


class CaseRules(unittest.TestCase):
    """D1.4: everything lower_case, module-level constants UPPER_CASE."""

    def test_every_kind_the_project_names_must_be_lower_case(self):
        for kind in fort_lint.LOWER_KINDS:
            with self.subTest(kind=kind):
                self.assertIsNone(fort_lint.name_problem(kind, "str_buf"))
                self.assertIsNone(fort_lint.name_problem(kind, "parse_i64"))
                self.assertIsNone(fort_lint.name_problem(kind, "utf8_len2"))
                problem = fort_lint.name_problem(kind, "strBuf")
                self.assertEqual(problem, "%s 'strBuf' is not lower_case (D1.4)" % kind)

    def test_an_upper_case_name_is_a_violation_for_every_lower_kind(self):
        for kind in fort_lint.LOWER_KINDS:
            for name in ("Point", "MAX", "aValue", "_Bad"):
                with self.subTest(kind=kind, name=name):
                    self.assertIsNotNone(fort_lint.name_problem(kind, name))

    def test_a_module_constant_is_upper_case(self):
        self.assertIsNone(fort_lint.name_problem("constant", "MAX_NODES"))
        self.assertIsNone(fort_lint.name_problem("constant", "U64_MAX"))
        self.assertIsNone(fort_lint.name_problem("constant", "CHUNK"))
        self.assertEqual(
            fort_lint.name_problem("constant", "max_nodes"),
            "constant 'max_nodes' is not UPPER_CASE (D1.4)",
        )
        self.assertIsNotNone(fort_lint.name_problem("constant", "MaxNodes"))

    def test_an_extern_keeps_the_c_symbol_spelling(self):
        """The foreign library chose the name; renaming it breaks the link."""
        self.assertIsNone(fort_lint.name_problem("extern fn", "SDL_Init"))
        self.assertIsNone(fort_lint.name_problem("extern fn", "printf"))

    def test_a_builtin_is_named_by_the_language(self):
        self.assertIsNone(fort_lint.name_problem("builtin", "len"))

    def test_the_two_tables_do_not_overlap(self):
        self.assertEqual(set(fort_lint.LOWER_KINDS) & set(fort_lint.UPPER_KINDS), set())
        self.assertNotIn("extern fn", fort_lint.LOWER_KINDS)
        self.assertNotIn("builtin", fort_lint.LOWER_KINDS)


class TypeBaseName(unittest.TestCase):
    """The name of the type a declaration spells (D5.2, D5.3)."""

    def test_a_plain_type_is_its_own_name(self):
        self.assertEqual(fort_lint.type_base_name("i32"), "i32")
        self.assertEqual(fort_lint.type_base_name("str_buf"), "str_buf")

    def test_the_markers_after_the_name_are_not_part_of_it(self):
        self.assertEqual(fort_lint.type_base_name("str_buf mut*"), "str_buf")
        self.assertEqual(fort_lint.type_base_name("u8 mut@ own"), "u8")
        self.assertEqual(fort_lint.type_base_name("node mut* mut"), "node")
        self.assertEqual(fort_lint.type_base_name("i32[4]"), "i32")
        self.assertEqual(fort_lint.type_base_name(" i32 "), "i32")

    def test_a_qualified_type_keeps_its_last_component(self):
        """The module qualifier is a binding a local may shadow (D7.9)."""
        self.assertEqual(fort_lint.type_base_name("strbuf.str_buf"), "str_buf")
        self.assertEqual(fort_lint.type_base_name("strbuf.str_buf mut*"), "str_buf")

    def test_a_function_type_names_nothing(self):
        self.assertIsNone(fort_lint.type_base_name("fn i32(i32)"))
        self.assertIsNone(fort_lint.type_base_name("fn void()"))

    def test_no_type_at_all(self):
        self.assertIsNone(fort_lint.type_base_name(""))
        self.assertIsNone(fort_lint.type_base_name(None))
        self.assertIsNone(fort_lint.type_base_name("[4]i32"))


class ShadowRule(unittest.TestCase):
    """D1.4: a variable never takes its type's name, a field may (D7.9)."""

    def test_a_variable_named_after_its_type_is_a_violation(self):
        for kind in fort_lint.SHADOW_KINDS:
            with self.subTest(kind=kind):
                self.assertEqual(
                    fort_lint.shadow_problem(kind, "point", "point"),
                    "%s 'point' takes the name of its type (D1.4)" % kind,
                )
                self.assertEqual(
                    fort_lint.shadow_problem(kind, "node", "node mut*"),
                    "%s 'node' takes the name of its type (D1.4)" % kind,
                )

    def test_a_field_may_take_its_type_name(self):
        self.assertIsNone(fort_lint.shadow_problem("field", "node", "node mut*"))
        self.assertIsNone(fort_lint.shadow_problem("enum member", "color", "color"))

    def test_a_variable_named_otherwise_is_fine(self):
        self.assertIsNone(fort_lint.shadow_problem("local", "p", "point"))
        self.assertIsNone(fort_lint.shadow_problem("parameter", "bx", "box"))

    def test_a_variable_may_take_the_module_qualifier(self):
        """import std.io forbids no parameter named io (D7.9)."""
        self.assertIsNone(fort_lint.shadow_problem("local", "strbuf", "strbuf.str_buf"))

    def test_a_variable_of_function_type_is_not_compared(self):
        self.assertIsNone(fort_lint.shadow_problem("local", "fn", "fn i32(i32)"))


class RecordProblems(unittest.TestCase):
    def test_a_use_carries_no_problem(self):
        self.assertEqual(
            fort_lint.record_problems(record("local", "Sum", "i32", is_decl=False)), []
        )

    def test_a_declaration_reports_both_rules_case_first(self):
        problems = fort_lint.record_problems(record("local", "Point", "Point"))
        self.assertEqual(
            problems,
            [
                "local 'Point' is not lower_case (D1.4)",
                "local 'Point' takes the name of its type (D1.4)",
            ],
        )

    def test_a_clean_declaration_reports_nothing(self):
        self.assertEqual(fort_lint.record_problems(record("local", "total", "i32 mut")), [])


class ModuleName(unittest.TestCase):
    """A module is named by its file (D9.1), so the stem carries D1.4."""

    def test_a_lower_case_stem_passes(self):
        self.assertIsNone(fort_lint.module_name_problem("std/io.ft"))
        self.assertIsNone(fort_lint.module_name_problem("/a/b/str_buf.ft"))

    def test_a_camel_case_stem_fails(self):
        self.assertEqual(
            fort_lint.module_name_problem("src/fort/badModule.ft"),
            "module 'badModule' is not lower_case (D1.4)",
        )


class Width(unittest.TestCase):
    """The 100 columns of the house style, the one rule not from D1.4."""

    def test_a_line_at_the_limit_passes(self):
        self.assertEqual(fort_lint.width_problems("x" * fort_lint.MAX_COLUMNS + "\n"), [])

    def test_a_line_over_the_limit_is_reported_at_the_first_column_past_it(self):
        text = "ok\n" + "x" * (fort_lint.MAX_COLUMNS + 4) + "\nok\n"
        self.assertEqual(
            fort_lint.width_problems(text),
            [(2, 101, "line is 104 columns, over the 100 of the house style")],
        )

    def test_every_long_line_is_reported(self):
        text = ("y" * 120 + "\n") * 3
        self.assertEqual(len(fort_lint.width_problems(text)), 3)


class SameFile(unittest.TestCase):
    def test_a_record_of_another_file_is_not_this_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.ft").write_text("")
            (root / "b.ft").write_text("")
            self.assertTrue(fort_lint.same_file(str(root), "a.ft", "a.ft"))
            self.assertTrue(fort_lint.same_file(str(root), str(root / "a.ft"), "a.ft"))
            self.assertTrue(fort_lint.same_file(str(root), "./a.ft", "a.ft"))
            self.assertFalse(fort_lint.same_file(str(root), "b.ft", "a.ft"))


class DocumentProblems(unittest.TestCase):
    """The filtering and the guards around the records of one document."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for name in ("a.ft", "b.ft"):
            (self.root / name).write_text("")

    def tearDown(self):
        self.tmp.cleanup()

    def problems(self, document):
        return fort_lint.document_problems(document, str(self.root), "a.ft")

    def test_a_record_of_another_file_is_skipped(self):
        document = {
            "diagnostics": [],
            "symbols": [record("local", "ok", "i32"), record("local", "Bad", "i32", file="b.ft")],
        }
        self.assertEqual(self.problems(document), [])

    def test_records_are_sorted_by_position(self):
        document = {
            "diagnostics": [],
            "symbols": [
                record("local", "Later", "i32", line=9, col=2),
                record("local", "Sooner", "i32", line=3, col=7),
            ],
        }
        self.assertEqual([p[:2] for p in self.problems(document)], [(3, 7), (9, 2)])

    def test_one_position_reports_one_message_once(self):
        same = record("local", "Bad", "i32", line=4, col=5)
        document = {"diagnostics": [], "symbols": [same, dict(same)]}
        self.assertEqual(len(self.problems(document)), 1)

    def test_a_rejected_file_is_still_judged_on_what_resolved(self):
        """D20.3 indexes everything that resolved, so the rules still run."""
        document = {
            "diagnostics": [{"file": "b.ft", "line": 2, "col": 3, "message": "unknown name 'q'"}],
            "symbols": [record("local", "Bad", "i32", line=5, col=9)],
        }
        problems = self.problems(document)
        self.assertEqual(len(problems), 2)
        self.assertEqual(problems[0][:2], (1, 1))
        self.assertIn("b.ft:2:3: unknown name 'q'", problems[0][2])
        self.assertEqual(problems[1], (5, 9, "local 'Bad' is not lower_case (D1.4)"))

    def test_a_diagnostic_in_an_imported_module_does_not_blind_this_one(self):
        """One broken module must not switch the lint off for its importers."""
        document = {
            "diagnostics": [{"file": "b.ft", "line": 1, "col": 1, "message": "bad"}],
            "symbols": [record("fn", "alsoBad", "fn void()", line=3, col=8)],
        }
        self.assertIn("alsoBad", self.problems(document)[1][2])

    def test_diagnostics_are_ordered_by_the_position_they_name(self):
        """Not by the text of the line, or a 20-error module lists them by message."""
        document = {
            "diagnostics": [
                {"file": "a.ft", "line": 20, "col": 1, "message": "aaa"},
                {"file": "a.ft", "line": 3, "col": 1, "message": "zzz"},
            ],
            "symbols": [record("local", "ok", "i32")],
        }
        messages = [p[2] for p in self.problems(document)]
        self.assertIn("a.ft:3:1: zzz", messages[0])
        self.assertIn("a.ft:20:1: aaa", messages[1])

    def test_a_document_naming_no_record_of_the_file_is_a_problem(self):
        """The guard: a filter that matched nothing would lint nothing."""
        document = {"diagnostics": [], "symbols": [record("local", "ok", "i32", file="b.ft")]}
        problems = self.problems(document)
        self.assertEqual(len(problems), 1)
        self.assertIn("no identifier", problems[0][2])

    def test_the_empty_index_guard_is_off_when_a_diagnostic_explains_it(self):
        """A syntax error stops the file before the checker (D14.2): no records."""
        document = {
            "diagnostics": [{"file": "a.ft", "line": 1, "col": 1, "message": "expected ';'"}],
            "symbols": [],
        }
        problems = self.problems(document)
        self.assertEqual(len(problems), 1)
        self.assertNotIn("no identifier", problems[0][2])

    def test_a_file_that_declares_nothing_is_not_a_problem(self):
        """An empty module or a stub of comments, plausible in Phase B."""
        document = {"diagnostics": [], "symbols": []}
        self.assertEqual(
            fort_lint.document_problems(document, str(self.root), "a.ft", has_source_text=False),
            [],
        )


class SourceText(unittest.TestCase):
    """Whether a file holds anything a declaration could be in (D2.2)."""

    def test_blank_and_comment_lines_are_not_source(self):
        self.assertFalse(fort_lint.has_source_text(""))
        self.assertFalse(fort_lint.has_source_text("\n\n"))
        self.assertFalse(fort_lint.has_source_text("// a stub\n//\n   // indented\n"))

    def test_a_declaration_is_source(self):
        self.assertTrue(fort_lint.has_source_text("// c\nfn void f() {}\n"))
        self.assertTrue(fort_lint.has_source_text("import std.io;"))


class KindTables(unittest.TestCase):
    """The tables of fort_lint.py against the kind list of D20.3 itself.

    The repository's pattern for a list that lives in two places: read the
    decision log, so the two cannot drift (test/highlight_test.py does it for
    the D2.4 keywords). A thirteenth kind added to the index would otherwise be
    exempt from D1.4 for ever, silently.
    """

    def decision_kinds(self):
        text = DECISIONS_PATH.read_text(encoding="utf-8")
        # The rule wraps at 100 columns, so the sentence is read with its
        # whitespace collapsed and the search never depends on a line break.
        rule = " ".join(check_decisions.rule_of(text, "D20.3").split())
        sentence = re.search(
            r'`"kind"` is the kind of what the name denotes[^:]*:(.*?)\(D', rule, re.DOTALL
        )
        self.assertIsNotNone(sentence, "D20.3 no longer spells its kind list the same way")
        return set(re.findall(r"`([^`]*)`", sentence.group(1)))

    def test_the_decision_list_was_found(self):
        kinds = self.decision_kinds()
        self.assertIn("enum member", kinds)
        self.assertIn("extern fn", kinds)
        self.assertEqual(len(kinds), 12)

    def test_every_kind_of_d203_is_in_exactly_one_table(self):
        tables = (fort_lint.LOWER_KINDS, fort_lint.UPPER_KINDS, fort_lint.EXEMPT_KINDS)
        known = [kind for table in tables for kind in table]
        self.assertEqual(len(known), len(set(known)), "a kind is in two tables")
        self.assertEqual(set(known), self.decision_kinds())

    def test_an_unknown_kind_is_reported_and_not_exempted(self):
        problem = fort_lint.name_problem("trait", "Whatever")
        self.assertIsNotNone(problem)
        self.assertIn("in no table", problem)


class FakeCompiler(unittest.TestCase):
    """The plumbing around the compiler, driven by a fort that is a script."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        # A declaration, so the empty-index guard is live for these tests.
        (self.root / "a.ft").write_text("// a module\nfn void f() {}\n")

    def tearDown(self):
        self.tmp.cleanup()

    def fake_fort(self, body):
        """Write a fake compiler whose whole behaviour is the given body."""
        path = self.root / "fake_fort"
        path.write_text("#!%s\nimport sys\n" % sys.executable + body)
        path.chmod(0o755)
        return str(path)

    def run_lint(self, fort, *args):
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "fort_lint.py"),
                "--root",
                str(self.root),
                "--fort",
                fort,
                *args,
            ],
            capture_output=True,
            text=True,
            cwd=str(self.root),
            env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
        )

    def test_a_clean_document_passes(self):
        document = (
            '{"diagnostics":[],"symbols":[{"file":"a.ft","line":1,"col":1,"name":"ok",'
            '"kind":"fn","type":"fn void()","is_decl":true}]}'
        )
        fort = self.fake_fort("print(%r)\n" % document)
        got = self.run_lint(fort, "a.ft")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertIn("no violation of D1.4", got.stdout)

    def test_a_violation_fails_the_run(self):
        document = (
            '{"diagnostics":[],"symbols":[{"file":"a.ft","line":2,"col":3,"name":"Bad",'
            '"kind":"fn","type":"fn void()","is_decl":true}]}'
        )
        fort = self.fake_fort("print(%r)\n" % document)
        got = self.run_lint(fort, "a.ft")
        self.assertEqual(got.returncode, 1)
        self.assertIn("a.ft:2:3: fn 'Bad' is not lower_case (D1.4)", got.stdout)
        self.assertIn("1 problem(s) in 1 of 1 file(s)", got.stderr)

    def test_a_compiler_that_writes_no_document_is_an_error(self):
        fort = self.fake_fort("print('not json')\n")
        got = self.run_lint(fort, "a.ft")
        self.assertEqual(got.returncode, 1)
        self.assertIn("wrote no document", got.stdout)

    def test_a_compiler_that_fails_without_a_diagnostic_is_an_error(self):
        """Status 1 means "reported a diagnostic" (D20.2); a sanitizer report is not one."""
        fort = self.fake_fort(
            "sys.stderr.write('LeakSanitizer\\n')\n"
            'print(\'{"diagnostics":[],"symbols":[]}\')\nsys.exit(1)\n'
        )
        got = self.run_lint(fort, "a.ft")
        self.assertEqual(got.returncode, 1)
        self.assertIn("reported no diagnostic", got.stdout)

    def test_a_compiler_that_dies_is_an_error(self):
        fort = self.fake_fort("sys.stderr.write('boom\\n')\nsys.exit(2)\n")
        got = self.run_lint(fort, "a.ft")
        self.assertEqual(got.returncode, 1)
        self.assertIn("fort exited with status 2", got.stdout)

    def test_a_broken_compiler_does_not_cost_the_checks_that_read_the_text(self):
        (self.root / "wide.ft").write_text("// " + "x" * 120 + "\n")
        fort = self.fake_fort("sys.exit(3)\n")
        got = self.run_lint(fort, "wide.ft")
        self.assertEqual(got.returncode, 1)
        self.assertIn("over the 100 of the house style", got.stdout)
        self.assertIn("fort exited with status 3", got.stdout)

    def test_the_search_roots_are_passed_to_the_compiler(self):
        """One `-I` per root, before the file (D9.2)."""
        fort = self.fake_fort(
            'print(\'{"diagnostics":[],"symbols":[]}\' if sys.argv[1:] == '
            "['--index', '-I', 'x', '-I', 'y', 'a.ft'] else "
            '\'{"diagnostics":[],"symbols":[{"file":"a.ft",'
            '"line":1,"col":1,"name":"Wrong","kind":"fn","type":"",'
            '"is_decl":true}]}\')\n'
        )
        got = self.run_lint(fort, "-I", "x", "-I", "y", "a.ft")
        # The empty index trips the guard, which proves the arguments matched.
        self.assertIn("no identifier", got.stdout)
        self.assertNotIn("Wrong", got.stdout)

    def test_the_file_is_passed_to_the_compiler(self):
        fort = self.fake_fort(
            'print(\'{"diagnostics":[],"symbols":[]}\' if sys.argv[1:] == '
            '[\'--index\', \'a.ft\'] else \'{"diagnostics":[],"symbols":[{"file":"a.ft",'
            '"line":1,"col":1,"name":"Wrong","kind":"fn","type":"",'
            '"is_decl":true}]}\')\n'
        )
        got = self.run_lint(fort, "a.ft")
        # The empty index trips the guard, which proves the arguments matched.
        self.assertIn("no identifier", got.stdout)


class RunPerClosure(unittest.TestCase):
    """One `fort --index` run judges every file of the closure it indexed.

    The tool ran one `fort --index` per file until T-095. Each run re-checked
    the whole import closure of its file, so the checker did O(n^2) work and
    the ctest took 48.9 s under the debug preset for 166 files. A run now
    judges every file of the set that its document names (D20.3), and the
    fake compiler here counts the runs.
    """

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.log = self.root / "runs.log"
        for name in ("a.ft", "b.ft", "c.ft"):
            # A declaration, so the empty-index guard is live for these tests.
            (self.root / name).write_text("// %s\nfn void f() {}\n" % name)

    def tearDown(self):
        self.tmp.cleanup()

    def fake_fort(self, body):
        """A fake compiler that logs the file it was given, then runs the body."""
        path = self.root / "fake_fort"
        path.write_text(
            "#!%s\nimport sys\n" % sys.executable
            + "entry = sys.argv[-1]\n"
            + "open(%r, 'a').write(entry + '\\n')\n" % str(self.log)
            + body
        )
        path.chmod(0o755)
        return str(path)

    def runs(self):
        """The files the fake compiler was asked to index, in order."""
        if not self.log.exists():
            return []
        return self.log.read_text().split()

    def lint(self, fort, *names):
        return fort_lint.lint_files(fort, self.root, [(self.root / name, ()) for name in names])

    def document(self, *records):
        return '{"diagnostics":[],"symbols":[%s]}' % ",".join(records)

    def declaration(self, file, name, line=1, col=1):
        return (
            '{"file":"%s","line":%d,"col":%d,"name":"%s",'
            '"kind":"fn","type":"fn void()","is_decl":true}' % (file, line, col, name)
        )

    def test_one_run_judges_every_file_of_its_closure(self):
        document = self.document(
            self.declaration("a.ft", "ok"),
            self.declaration("b.ft", "also_ok", line=2),
            self.declaration("c.ft", "Bad", line=3, col=4),
        )
        fort = self.fake_fort("print(%r)\n" % document)
        reports, runs = self.lint(fort, "a.ft", "b.ft", "c.ft")
        self.assertEqual(runs, 1)
        self.assertEqual(self.runs(), ["a.ft"])
        self.assertEqual(reports[0], [])
        self.assertEqual(reports[1], [])
        self.assertEqual(reports[2], ["c.ft:3:4: fn 'Bad' is not lower_case (D1.4)"])

    def test_a_file_no_closure_names_becomes_an_entry_itself(self):
        """The rule that keeps every file judged: coverage decides, not the closure's size."""
        fort = self.fake_fort(
            "print(%r if entry == 'a.ft' else %r)\n"
            % (
                self.document(self.declaration("a.ft", "ok"), self.declaration("c.ft", "ok")),
                self.document(self.declaration("b.ft", "Bad", line=5, col=6)),
            )
        )
        reports, runs = self.lint(fort, "a.ft", "b.ft", "c.ft")
        self.assertEqual(runs, 2)
        self.assertEqual(self.runs(), ["a.ft", "b.ft"])
        self.assertEqual(reports[1], ["b.ft:5:6: fn 'Bad' is not lower_case (D1.4)"])
        self.assertEqual(reports[2], [])

    def test_a_file_with_other_search_roots_takes_a_run_of_its_own(self):
        """A file keeps the roots its own set gives it (D9.2), so the roots must agree."""
        document = self.document(
            self.declaration("a.ft", "ok"), self.declaration("b.ft", "Bad", line=3, col=2)
        )
        fort = self.fake_fort("print(%r)\n" % document)
        files = [(self.root / "a.ft", ()), (self.root / "b.ft", ("x",))]
        reports, runs = fort_lint.lint_files(fort, self.root, files)
        self.assertEqual(runs, 2)
        self.assertEqual(self.runs(), ["a.ft", "b.ft"])
        self.assertEqual(reports[1], ["b.ft:3:2: fn 'Bad' is not lower_case (D1.4)"])

    def test_a_record_that_spells_the_file_differently_still_matches(self):
        """Real paths decide, so `./c.ft` is c.ft (the copy of std beside the compiler)."""
        document = self.document(
            self.declaration("a.ft", "ok"), self.declaration("./c.ft", "Bad", line=7, col=8)
        )
        fort = self.fake_fort("print(%r)\n" % document)
        reports, runs = self.lint(fort, "a.ft", "c.ft")
        self.assertEqual(runs, 1)
        self.assertEqual(reports[1], ["c.ft:7:8: fn 'Bad' is not lower_case (D1.4)"])

    def test_a_file_that_fails_to_compile_is_judged_by_nothing(self):
        """D20.3 gives a file the checker never reached no record, so no rule runs on it.

        b.ft holds a name D1.4 forbids. The compiler reports a diagnostic and
        indexes nothing, so the run reports the diagnostic and no D1.4
        problem. The lint must not widen: it judges records, not text.
        """
        broken = (
            '{"diagnostics":[{"file":"b.ft","line":2,"col":1,"message":"expected \';\'"}],'
            '"symbols":[]}'
        )
        fort = self.fake_fort(
            "print(%r if entry == 'a.ft' else %r)\n"
            % (self.document(self.declaration("a.ft", "ok")), broken)
            + "sys.exit(0 if entry == 'a.ft' else 1)\n"
        )
        reports, runs = self.lint(fort, "a.ft", "b.ft")
        self.assertEqual(runs, 2)
        self.assertEqual(reports[0], [])
        self.assertEqual(len(reports[1]), 1)
        self.assertIn("expected ';'", reports[1][0])
        self.assertNotIn("D1.4", reports[1][0])

    def test_a_run_gives_its_diagnostics_to_every_file_it_judges(self):
        """The one behaviour T-095 changed, and the shape no other test has.

        The run holds a diagnostic and judges two files. Both hear about it.
        One run per file gave a file the diagnostics of its own closure; a run
        now gives them to every file of the closure it indexed, which reports
        more and never less. A rule that sent them to the entry alone, or to
        the wrong file, would pass every other test of this class.
        """
        document = (
            '{"diagnostics":[{"file":"c.ft","line":4,"col":2,"message":"unknown name \'q\'"}],'
            '"symbols":[%s,%s]}'
            % (self.declaration("a.ft", "ok"), self.declaration("b.ft", "Bad", line=6, col=3))
        )
        fort = self.fake_fort("print(%r)\nsys.exit(1)\n" % document)
        reports, runs = self.lint(fort, "a.ft", "b.ft")
        self.assertEqual(runs, 1)
        self.assertEqual(len(reports[0]), 1)
        self.assertEqual(
            reports[0][0], "a.ft:1:1: fort could not check this closure: c.ft:4:2: unknown name 'q'"
        )
        self.assertEqual(
            reports[1],
            [
                "b.ft:1:1: fort could not check this closure: c.ft:4:2: unknown name 'q'",
                "b.ft:6:3: fn 'Bad' is not lower_case (D1.4)",
            ],
        )

    def test_a_failed_run_leaves_every_other_file_a_run_of_its_own(self):
        """A broken environment must not take the other files down with it."""
        fort = self.fake_fort("sys.stderr.write('boom\\n')\nsys.exit(2)\n")
        reports, runs = self.lint(fort, "a.ft", "b.ft", "c.ft")
        self.assertEqual(runs, 3)
        self.assertEqual(self.runs(), ["a.ft", "b.ft", "c.ft"])
        for report in reports:
            self.assertEqual(len(report), 1)
            self.assertIn("fort exited with status 2", report[0])

    def test_the_checks_that_read_the_text_run_for_every_file_of_a_closure(self):
        """The width check is the file's own, so one run must not cost it."""
        (self.root / "b.ft").write_text("// " + "x" * 120 + "\nfn void f() {}\n")
        document = self.document(
            self.declaration("a.ft", "ok"), self.declaration("b.ft", "ok", line=2)
        )
        fort = self.fake_fort("print(%r)\n" % document)
        reports, runs = self.lint(fort, "a.ft", "b.ft")
        self.assertEqual(runs, 1)
        self.assertEqual(len(reports[1]), 1)
        self.assertIn("over the 100 of the house style", reports[1][0])

    def test_the_empty_index_guard_still_covers_a_file_of_a_closure(self):
        """A file the run indexed nothing for is reported, not passed in silence."""
        fort = self.fake_fort(
            "print(%r)\n" % self.document(self.declaration("a.ft", "ok")) + "sys.exit(0)\n"
        )
        reports, runs = self.lint(fort, "a.ft", "b.ft")
        # b.ft is not in a.ft's document, so it takes a run of its own, whose
        # document names no record of it either.
        self.assertEqual(runs, 2)
        self.assertEqual(len(reports[1]), 1)
        self.assertIn("no identifier", reports[1][0])


class DefaultFileSet(unittest.TestCase):
    """The guard against a lint that is green because it checked nothing."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def problems(self):
        return fort_lint.empty_set_problems(self.root, fort_lint.collect(self.root, SOURCE_GLOBS))

    def test_a_root_with_no_fort_source_is_a_problem(self):
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn("no fort source matched", problems[0])

    def test_a_directory_that_exists_but_holds_no_ft_is_a_problem(self):
        """A renamed extension or a mistyped glob, which would pass silently."""
        (self.root / "std").mkdir()
        (self.root / "std" / "io.fort").write_text("")
        (self.root / "src" / "fort").mkdir(parents=True)
        (self.root / "src" / "fort" / "lexer.ft").write_text("")
        problems = self.problems()
        self.assertEqual(len(problems), 1)
        self.assertIn("std", problems[0])
        self.assertIn("no file matched", problems[0])

    def test_a_directory_that_does_not_exist_yet_is_not_a_problem(self):
        """src/fort/ before Phase B fills it is absent, not empty."""
        (self.root / "std").mkdir()
        (self.root / "std" / "io.ft").write_text("")
        self.assertEqual(self.problems(), [])

    def test_the_real_repository_passes_the_guard(self):
        paths = fort_lint.collect(ROOT, SOURCE_GLOBS)
        self.assertGreaterEqual(len(paths), 8)
        self.assertEqual(fort_lint.empty_set_problems(ROOT, paths), [])


class IncludeRoots(unittest.TestCase):
    """The module search roots the default file set carries (D9.2, T-079)."""

    def roots(self, name):
        for path, includes in fort_lint.default_file_set(ROOT):
            if path.relative_to(ROOT).as_posix() == name:
                return includes
        self.fail("%s is not in the default file set" % name)
        return None

    def test_a_standard_library_module_needs_no_search_root(self):
        """An import of std resolves beside the compiler, not through -I."""
        self.assertEqual(self.roots("std/vec.ft"), ())

    def test_a_compiler_source_needs_no_search_root(self):
        self.assertEqual(self.roots("src/fort/containers.ft"), ())

    def test_a_module_test_gets_the_compiler_and_the_fixtures(self):
        """`-I ../../src/fort -I support` from the root of test/fort."""
        self.assertEqual(
            self.roots("test/fort/containers_test.ft"), ("src/fort", "test/fort/support")
        )

    def test_a_shared_fixture_gets_them_too(self):
        self.assertEqual(
            self.roots("test/fort/support/types_env.ft"), ("src/fort", "test/fort/support")
        )

    def test_the_set_holds_every_file_of_every_glob_once(self):
        files = fort_lint.default_file_set(ROOT)
        paths = [path for path, _ in files]
        skipped = {(ROOT / name).resolve() for name in fort_lint.SKIPPED}
        self.assertEqual(len(paths), len(set(paths)))
        self.assertEqual(paths, sorted(paths))
        for glob in fort_lint.SOURCE_GLOBS:
            matched = {p for p in fort_lint.collect(ROOT, (glob,)) if p.resolve() not in skipped}
            self.assertTrue(matched <= set(paths), glob)

    def test_the_float_modules_are_left_to_the_compiler_that_has_floats(self):
        """std/math.ft and std/rt_float.ft hold floats and the C bootstrap
        rejects them (D18.1, T-042), so the default set leaves them out and the
        ctest fort_lint_float lints them with stage2. A skipped file that does
        not exist would be a typo nothing reports, so it is checked as well."""
        files = {path.relative_to(ROOT).as_posix() for path, _ in fort_lint.default_file_set(ROOT)}
        self.assertEqual(fort_lint.SKIPPED, ("std/math.ft", "std/rt_float.ft"))
        for name in fort_lint.SKIPPED:
            self.assertNotIn(name, files)
            self.assertTrue((ROOT / name).is_file(), name)

    def test_the_set_is_the_four_corpora(self):
        """The count the ctest reports, so a glob that stops matching is seen."""
        files = fort_lint.default_file_set(ROOT)
        counts = {}
        for path, _ in files:
            counts[path.parent.relative_to(ROOT).as_posix()] = (
                counts.get(path.parent.relative_to(ROOT).as_posix(), 0) + 1
            )
        self.assertEqual(sorted(counts), ["src/fort", "std", "test/fort", "test/fort/support"])
        self.assertGreaterEqual(counts["test/fort"], 80)
        self.assertGreaterEqual(counts["test/fort/support"], 4)

    def test_an_overlapping_glob_takes_the_roots_of_the_first(self):
        """The table's order decides, not the filesystem's."""
        sets = (("std/*.ft", ("first",)), ("std/vec.ft", ("second",)))
        files = fort_lint.default_file_set(ROOT, sets)
        self.assertTrue(all(includes == ("first",) for _, includes in files))


@unittest.skipUnless(os.environ.get("FORT_BINARY"), "FORT_BINARY is not set")
class RealCompiler(unittest.TestCase):
    """The whole tool over the fixtures, with the compiler the build made."""

    def run_lint(self, *args):
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "fort_lint.py"),
                "--fort",
                os.environ["FORT_BINARY"],
                *args,
            ],
            capture_output=True,
            text=True,
            cwd=str(ROOT),
        )

    def test_the_conforming_fixture_is_silent(self):
        got = self.run_lint("test/fort_lint/good.ft")
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        self.assertEqual(got.stdout.strip(), "fort_lint: 1 file(s), no violation of D1.4")

    def test_the_violating_fixture_reports_every_rule(self):
        got = self.run_lint("test/fort_lint/bad_names.ft")
        self.assertEqual(got.returncode, 1)
        self.assertEqual(got.stdout.strip().split("\n"), BAD_NAMES_PROBLEMS)

    def test_the_standard_library_conforms(self):
        """Every module of std but the ones SKIPPED names, which the compiler
        this test runs -- the C bootstrap -- cannot check (D18.1)."""
        skipped = {(ROOT / name).resolve() for name in fort_lint.SKIPPED}
        files = [p for p in sorted((ROOT / "std").glob("*.ft")) if p.resolve() not in skipped]
        self.assertEqual(len(files) + len(skipped), len(list((ROOT / "std").glob("*.ft"))))
        got = self.run_lint(*[str(p.relative_to(ROOT)) for p in files])
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)

    def test_a_rejected_file_is_reported_and_still_judged(self):
        """The record the checker did resolve is judged beside the error (D20.3)."""
        got = self.run_lint("test/fort_lint/broken.ft")
        self.assertEqual(got.returncode, 1)
        self.assertEqual(got.stdout.strip().split("\n"), BROKEN_PROBLEMS)

    def test_a_module_test_without_its_roots_cannot_resolve_its_imports(self):
        """The hole T-079 closed: no -I meant no index and no D1.4 at all."""
        got = self.run_lint("test/fort/containers_test.ft")
        self.assertEqual(got.returncode, 1)
        self.assertIn("module 'containers' not found", got.stdout)

    def test_a_module_test_with_its_roots_is_judged(self):
        got = self.run_lint(
            "-I", "src/fort", "-I", "test/fort/support", "test/fort/containers_test.ft"
        )
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)
        self.assertEqual(got.stdout.strip(), "fort_lint: 1 file(s), no violation of D1.4")

    def test_a_shared_fixture_with_its_roots_is_judged(self):
        got = self.run_lint(
            "-I", "src/fort", "-I", "test/fort/support", "test/fort/support/types_env.ft"
        )
        self.assertEqual(got.returncode, 0, got.stdout + got.stderr)

    def test_a_file_outside_the_repository_is_linted_too(self):
        """The scratch file goes under build/, which is gitignored (AGENTS.md, T-022)."""
        scratch = ROOT / "build"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=str(scratch)) as tmp:
            path = Path(tmp) / "spare.ft"
            path.write_text("fn i32 badOne() {\n    return 0;\n}\n")
            got = self.run_lint(str(path.relative_to(ROOT)))
            self.assertEqual(got.returncode, 1)
            self.assertIn("fn 'badOne' is not lower_case (D1.4)", got.stdout)


if __name__ == "__main__":
    unittest.main()
