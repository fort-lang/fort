#!/usr/bin/env python3
"""Unit tests of tools/knowledge_lint.py: every rule, and every rule broken.

A check that cannot fail is worse than no check, because it stops the next
reader looking. So each rule here is watched twice: once on a seeded tree that
keeps it, where the lint exits 0, and once on the same tree with the rule
broken, where the lint prints a named problem and exits 1.

The marker test is an oracle and not a mirror: FIXTURES holds four seeded
sources and EXPECTED holds the mark notes/style.md 1.3 asks for at each comment
line, written by hand from the rule rather than read off the tool's output
(T-108's `marker_probe.py` is the model).

Run with `python3 -m unittest knowledge_lint_test` from this directory.
"""

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import knowledge_lint as lint  # noqa: E402

# ---- the seeded sources -----------------------------------------------------

HEADER = """\
// The header block of a seeded C header (notes/style.md 1.2).
//
// D1.1
#ifndef PROBE_H
#define PROBE_H

// A note above a preprocessor line, which declares nothing a caller reads.
#define PROBE_MAX 8

// ---- the probe --------------------------------------------------------------
typedef struct {
    /// The value the caller reads.
    long value;
    long count;  // how many reads it took,
                 // which wraps onto this line
    /// D3.1
    long kind;
} probe_t;

/// What the caller must hold true before it calls.
long probe_read(const probe_t *p);

/// --probe names the option this helper reads. A banner rule that matched two
/// dashes would read this contract as navigation and ask for `//`.
long probe_option(void);

static inline long probe_twice(long n) {
    // The doubling is exact over the range the caller promises.
    if (n > 0) {
        // A comment inside a nested block.
        n += 0;
    }
    return n * 2;
}

// A free note that stands alone and marks nothing.

#endif
"""

SOURCE = """\
// The header block of a seeded C source.
//
// D1.1
#include "probe.h"

// The definition's comment is an implementation note: the contract stands in
// the header.
long probe_read(const probe_t *p) {
    // The order matters here.
    return p->value;
}
"""

MODULE = """\
// The header block of a seeded fort module.
//
// D9.6
import std.io;

// A note above an import, which no other module reads.
import std.mem;

// ---- the constants ----------------------------------------------------------
u64 PROBE_MAX = 8;

/// The largest probe this module counts.
u64 PROBE_LIMIT = 16;

struct probe {
    /// The value the caller reads.
    u64 value;
    u64 count;  // how many reads it took
}

/// What the caller gets back.
fn u64 read(probe* p) {
    // The order matters here.
    return p->value;
}

// A free note that stands alone and marks nothing.

/// D9.6: a module exports every top-level declaration
fn u64 twice(u64 n) {
    return n * 2;
}

/// The sum of two probes. The signature wraps, so a comment inside it stands
/// in a parameter list and not on a declaration.
fn u64 add(u64 a,
           // the second operand, named here because the line ran out
           u64 b) {
    return a + b;
}
"""

PROGRAM = """\
//! run
//! stdout:
//| ok
import std.io;

// The header block of a seeded fort test program.
//
// D9.6

// A program is the end of the chain, so nothing here is an interface.
fn void probe() {
    io.println("ok");
}
"""

FIXTURES = {
    "src/bootstrap/probe.h": HEADER,
    "src/bootstrap/probe.c": SOURCE,
    "src/fort/probe.ft": MODULE,
    "test/fort/probe_test.ft": PROGRAM,
}

# The mark 1.3 asks for at each comment line of each fixture, and why. Written
# from the rule, not from the tool.
EXPECTED = {
    "src/bootstrap/probe.h": {
        1: ("//", "the header block"),
        2: ("//", "the header block"),
        3: ("//", "the header block"),
        7: ("//", "a preprocessor line is not a declaration"),
        10: ("//", "a section banner is navigation"),
        12: ("///", "a struct field"),
        14: ("//", "the end of a line of code"),
        15: ("//", "the wrap of a trailing comment"),
        16: ("///", "a citation on a struct field"),
        20: ("///", "a declaration of a header"),
        23: ("///", "a declaration whose contract opens on an option name"),
        24: ("///", "a declaration whose contract opens on an option name"),
        28: ("//", "inside a function body"),
        30: ("//", "inside a nested block"),
        36: ("//", "a note that marks nothing"),
    },
    "src/bootstrap/probe.c": {
        1: ("//", "the header block"),
        2: ("//", "the header block"),
        3: ("//", "the header block"),
        6: ("//", "a .c is no interface"),
        7: ("//", "a .c is no interface"),
        9: ("//", "inside a function body"),
    },
    "src/fort/probe.ft": {
        1: ("//", "the header block"),
        2: ("//", "the header block"),
        3: ("//", "the header block"),
        6: ("//", "an import is not a declaration"),
        9: ("//", "a section banner is navigation"),
        12: ("///", "a module constant, which D9.6 exports"),
        16: ("///", "a struct field"),
        18: ("//", "the end of a line of code"),
        21: ("///", "a top-level function"),
        23: ("//", "inside a function body"),
        27: ("//", "a note that marks nothing"),
        29: ("///", "a citation on a top-level function"),
        34: ("///", "a top-level function whose signature wraps"),
        35: ("///", "a top-level function whose signature wraps"),
        37: ("//", "inside a parameter list"),
    },
    "test/fort/probe_test.ft": {
        6: ("//", "a test program is no interface"),
        7: ("//", "a test program is no interface"),
        8: ("//", "a test program is no interface"),
        10: ("//", "a test program is no interface"),
    },
}

AGENTS = "# fort\n" + "".join(f"line {n}\n" for n in range(2, 21))

DECISIONS = """\
# The decisions

## D1 Scope

### D1.1 The first decision
- owner: spec/project-overview.md
- rule: the first rule.

### D1.2 The second decision
- owner: spec/project-overview.md
- rule: the second rule.
- history: Amended 2026-09-13 (T-103): nothing changed.

## D3 Types

### D3.1 The third decision
- owner: spec/project-overview.md
- rule: the third rule.

## D9 Modules

### D9.6 The fourth decision
- owner: spec/project-overview.md
- rule: the fourth rule.
"""

OVERVIEW = "# fort\n\nThe map of the specification.\n"

STYLE = """\
# fort style

## 4. Markdown and specification text

- **Moving a document**: cite a document by its section, never by a line
  number, so `notes/style.md` 4 is a citation and `notes/style.md:158` is not
  (T-105).
"""


def seed(root, files):
    """Write a seeded tree and return its root."""
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return root


def clean_tree(root, **changes):
    """A tree that keeps every rule, with the named files replaced."""
    files = dict(FIXTURES)
    files["AGENTS.md"] = AGENTS
    files["spec/decisions.md"] = DECISIONS
    files["spec/project-overview.md"] = OVERVIEW
    files["notes/style.md"] = STYLE
    files.update(changes)
    return seed(root, files)


def run(root, *rules):
    """Run the lint over a tree; return (exit status, the lines it printed)."""
    out = io.StringIO()
    argv = ["--root", str(root), "--quiet"]
    for rule in rules:
        argv += ["--rule", rule]
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
        status = lint.main(argv)
    return status, [line for line in out.getvalue().split("\n") if line]


def comments_at(name):
    """The comments of one fixture, by line number."""
    path = Path(name)
    text = FIXTURES[name]
    return {c.line: c for c in lint.comments_of(path, text)}, lint.kind_of(path, text)


class ScannerTest(unittest.TestCase):
    def test_a_comment_inside_a_string_is_not_a_comment(self):
        # test/gen_module_test.c holds a fort program as a C string (T-108)
        text = 'const char *s = "// D1.1 not a comment";\n'
        self.assertEqual(lint.comments_of(Path("test/probe.c"), text), [])

    def test_a_uri_inside_a_string_is_not_a_comment(self):
        text = '    assert(uri == "file:///a.ft");\n'
        self.assertEqual(lint.comments_of(Path("test/fort/probe.ft"), text), [])

    def test_a_comment_inside_a_char_literal_is_not_a_comment(self):
        text = "char slash = '/';  // the real comment\n"
        found = lint.comments_of(Path("src/bootstrap/probe.c"), text)
        self.assertEqual([(c.line, c.marker, c.trailing) for c in found],
                         [(1, "//", True)])

    def test_a_harness_directive_is_not_a_comment(self):
        text = "//! run\n//| ok\n//< in\n//@ x\n//^ y\n// a real one\n"
        found = lint.comments_of(Path("test/fort/probe_test.ft"), text)
        self.assertEqual([c.line for c in found], [6])

    def test_a_struct_body_is_not_a_function_body(self):
        found, _ = comments_at("src/bootstrap/probe.h")
        self.assertEqual(found[12].context, "record")
        self.assertEqual(found[28].context, "body")

    def test_an_initialiser_at_file_scope_is_a_block(self):
        text = "int table[] = {\n    // not a field\n    1,\n};\n"
        found = lint.comments_of(Path("src/bootstrap/probe.c"), text)
        self.assertEqual(found[0].context, "body")


class KindTest(unittest.TestCase):
    def test_each_fixture_gets_its_kind(self):
        kinds = {name: comments_at(name)[1] for name in FIXTURES}
        self.assertEqual(kinds, {
            "src/bootstrap/probe.h": "c_header",
            "src/bootstrap/probe.c": "c_source",
            "src/fort/probe.ft": "fort_module",
            "test/fort/probe_test.ft": "fort_program",
        })

    def test_the_directive_is_what_tells_a_program_from_a_module(self):
        # 173 programs of test/fort carry one and the 11 modules of
        # test/fort/support carry none (T-103, Notes / Design)
        self.assertEqual(lint.kind_of(Path("a.ft"), "//! run\nfn void f() {}\n"),
                         "fort_program")
        self.assertEqual(lint.kind_of(Path("a.ft"), "// a module\nfn void f() {}\n"),
                         "fort_module")


class MarkerTest(unittest.TestCase):
    def test_every_seeded_position_gets_the_mark_the_rule_asks_for(self):
        for name, table in EXPECTED.items():
            found, kind = comments_at(name)
            for line, (want, why) in table.items():
                with self.subTest(file=name, line=line, why=why):
                    self.assertEqual(lint.expected_marker(kind, found[line]), want)

    def test_the_table_covers_every_comment_of_every_fixture(self):
        for name in FIXTURES:
            found, _ = comments_at(name)
            self.assertEqual(sorted(found), sorted(EXPECTED[name]), name)

    def test_the_fixtures_as_written_break_no_rule(self):
        for name in FIXTURES:
            found, kind = comments_at(name)
            self.assertEqual(lint.marker_problems(name, kind, found.values()), [])

    def test_a_doc_comment_inside_a_function_body_is_reported(self):
        # the defect T-108 shipped at 37 sites, and the reason the rule reads
        # brace structure and never a column
        text = HEADER.replace("    // The doubling", "    /// The doubling")
        found = lint.comments_of(Path("src/bootstrap/probe.h"), text)
        self.assertEqual(
            lint.marker_problems("src/bootstrap/probe.h", "c_header", found),
            ["src/bootstrap/probe.h:28: /// on a function body; "
             "notes/style.md 1.3 asks for //"])

    def test_a_plain_comment_on_a_field_is_reported(self):
        text = MODULE.replace("    /// The value the caller reads.",
                              "    // the value")
        found = lint.comments_of(Path("src/fort/probe.ft"), text)
        self.assertEqual(
            lint.marker_problems("src/fort/probe.ft", "fort_module", found),
            ["src/fort/probe.ft:16: // on a field; notes/style.md 1.3 asks for ///"])

    def test_a_plain_comment_on_a_module_declaration_is_reported(self):
        text = MODULE.replace("/// What the caller gets back.", "// what it gives")
        found = lint.comments_of(Path("src/fort/probe.ft"), text)
        self.assertEqual(
            lint.marker_problems("src/fort/probe.ft", "fort_module", found),
            ["src/fort/probe.ft:21: // on a declaration; "
             "notes/style.md 1.3 asks for ///"])

    def test_a_doc_comment_in_a_test_program_is_reported(self):
        text = PROGRAM.replace("// A program is", "/// A program is")
        found = lint.comments_of(Path("test/fort/probe_test.ft"), text)
        self.assertEqual(
            lint.marker_problems("test/fort/probe_test.ft", "fort_program", found),
            ["test/fort/probe_test.ft:10: /// on a test program; "
             "notes/style.md 1.3 asks for //"])

    def test_a_doc_comment_in_a_c_source_is_reported(self):
        text = SOURCE.replace("// The definition's", "/// The definition's")
        found = lint.comments_of(Path("src/bootstrap/probe.c"), text)
        self.assertEqual(
            [p for p in lint.marker_problems("src/bootstrap/probe.c", "c_source", found)],
            ["src/bootstrap/probe.c:6: /// on a .c file; notes/style.md 1.3 asks for //"])

    def test_a_trailing_doc_comment_is_reported(self):
        # T-103 rules that a trailing comment keeps `//`: 0 of the tree's
        # comments carry `///` at the end of a line of code
        text = MODULE.replace("u64 count;  // how many", "u64 count;  /// how many")
        found = lint.comments_of(Path("src/fort/probe.ft"), text)
        self.assertEqual(
            lint.marker_problems("src/fort/probe.ft", "fort_module", found),
            ["src/fort/probe.ft:18: /// on the end of a line of code; "
             "notes/style.md 1.3 asks for //"])

    def test_a_doc_comment_on_a_section_banner_is_reported(self):
        text = MODULE.replace("// ---- the constants", "/// ---- the constants")
        found = lint.comments_of(Path("src/fort/probe.ft"), text)
        self.assertEqual(
            lint.marker_problems("src/fort/probe.ft", "fort_module", found),
            ["src/fort/probe.ft:9: /// on a section banner; "
             "notes/style.md 1.3 asks for //"])


# Every shape the citation rule refuses, and the control beside it. The second
# item is the fault the rule must name, or "" for a line it accepts.
CITATIONS = (
    (" D17.5", ""),
    (" D17.5: an own lvalue moves", ""),
    (" D11.5, D11.7", ""),
    (" D3 to D8, D12, D14.2", ""),
    (" D9.1 to D9.6", ""),
    (" T-091: the runtime is a fort module", ""),
    (" D9.5, D19.6, T-037: the emitter writes the data section", ""),
    (" The bytes of U+D800, which no code point names", ""),
    (" D17.5 says an own lvalue moves", "outside the shape"),
    (" pointers print as 0x (D11.7)", "outside the shape"),
    (" D17.5: an own lvalue moves.", "outside the shape"),
    (" D17.5, and D11.7", "outside the shape"),
    (" D17.5 : an own lvalue moves", "outside the shape"),
    (" T-091", "a ticket number with no clause"),
    (" D9.5, T-037", "a ticket number with no clause"),
    (" D9.5 to D9.1", "does not ascend"),
    (" D3.1 to D3.1", "does not ascend"),
    (" D3 to D9.1", "mixes a section and a decision"),
    (" D4.1 to D4.6, D3.14, D4.4", "the range of the same list covers it"),
)

# The punctuation a cut tag leaves, one probe for each pattern, and the control
# line under it that must stay clean.
MANGLED_PROBES = (
    (" the rule  applies here", "two spaces where a tag was cut out"),
    (" the rule (: text) applies", "a parenthesis that opens on a separator"),
    (" the rule (item 8,) applies", "a parenthesis that closes on a separator"),
    (" the rule () applies", "an empty parenthesis"),
    (" the rule [] applies", "an empty bracket"),
    (" the rule (see ) applies", "a one-word parenthesis that ends in a space"),
    (" the rule applies under .", "a separator with nothing before it"),
    (" the 's rule is the one", "a possessive whose owner is gone"),
    (" the rule (the note on) applies", "a connective left hanging"),
    (" the header and.c agree", "a space that went with the tag"),
)

CLEAN_PROSE = (
    " the rule applies here",
    " the rule (item 8) applies",
    " `a  b` is a code span with two spaces",
    " the header x.h and the source x.c agree",
    " the caller's rule is the one",
    " a list: one, two, three",
)


class CitationTest(unittest.TestCase):
    def test_each_seeded_citation_gets_the_verdict_the_rule_asks_for(self):
        for body, fault in CITATIONS:
            with self.subTest(body=body):
                comment = lint.Comment(Path("a.ft"), 1, "//", body)
                problems, _ = lint.citation_problems("a.ft", [comment])
                if fault == "":
                    self.assertEqual(problems, [], body)
                else:
                    self.assertEqual(len(problems), 1, problems)
                    self.assertIn(fault, problems[0])

    def test_a_tag_after_a_plus_is_a_code_point_and_not_a_citation(self):
        # the first wrong answer of T-101's lint (T-108)
        self.assertEqual(lint.TAG.findall("U+D800 is a surrogate"), [])
        self.assertEqual(len(lint.TAG.findall("D8.1 and D800")), 2)

    def test_each_seeded_cut_is_reported(self):
        for body, why in MANGLED_PROBES:
            with self.subTest(body=body):
                comment = lint.Comment(Path("a.ft"), 1, "//", body)
                problems, _ = lint.citation_problems("a.ft", [comment])
                self.assertEqual(len(problems), 1, problems)
                self.assertIn(why, problems[0])

    def test_an_aligned_column_inside_a_comment_is_not_a_cut(self):
        # a two-column table inside a comment aligns its second column, and the
        # cut rule must not call that a tag cut out of prose (T-103, round 2)
        text = ("// kind   what it holds\n// k_int  a number, verbatim\n"
                "// k_str  the decoded bytes\nfn void f() {}\n")
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(problems, [])

    def test_two_lines_cut_at_one_column_are_two_cuts_and_not_a_table(self):
        # the false negative the two-line rule of round 2 traded for the false
        # positive above: a sweep that cuts one parenthesised tag out of two
        # wrapped lines of one paragraph leaves two runs at one column, and
        # T-101 made 11 of that shape in 9 files (T-103, round 3)
        text = ("// the checker refuses it  and the emitter answers\n"
                "// the checker accepts it  and the emitter writes\nfn void f() {}\n")
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(len(problems), 2, problems)
        self.assertIn("two spaces where a tag was cut out", problems[0])

    def test_a_two_row_table_is_read_as_a_cut_which_is_the_stated_cost(self):
        # the residue of the rule above, stated in the tool's docstring: a table
        # of two rows goes red with a message that names the wrong cause. 0 such
        # tables stand in the tree, and a false positive somebody must silence
        # is the failure to prefer over a defect that ships.
        text = "// kind   what it holds\n// k_int  a number, verbatim\nfn void f() {}\n"
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(len(problems), 2, problems)

    def test_a_run_of_spaces_no_neighbour_aligns_with_is_a_cut(self):
        text = "// the rule  applies here\n// and nothing aligns with it\nfn void f() {}\n"
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("two spaces where a tag was cut out", problems[0])

    def test_a_neighbour_that_aligns_elsewhere_does_not_excuse_a_cut(self):
        text = "// the rule  applies here\n// a b  c d e f g h i j k\nfn void f() {}\n"
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(len(problems), 2, problems)

    def test_prose_that_holds_no_cut_stays_clean(self):
        for body in CLEAN_PROSE:
            with self.subTest(body=body):
                comment = lint.Comment(Path("a.ft"), 1, "//", body)
                problems, _ = lint.citation_problems("a.ft", [comment])
                self.assertEqual(problems, [])

    def test_a_tag_that_names_no_entry_of_the_log_is_reported(self):
        comment = lint.Comment(Path("a.ft"), 1, "//", " D99.9")
        problems, _ = lint.citation_problems("a.ft", [comment], {"D1.1", "D15"})
        self.assertEqual(problems, [
            "a.ft:1: D99.9 is in no entry and no section of spec/decisions.md: // D99.9"])

    def test_a_section_tag_of_the_log_is_a_citation(self):
        # D15 and D16 are `## ` headings of spec/decisions.md and hold no entry
        # of their own, and both are cited in the sources
        comment = lint.Comment(Path("a.ft"), 1, "//", " D15, D1.1")
        problems, _ = lint.citation_problems("a.ft", [comment], {"D1.1", "D15"})
        self.assertEqual(problems, [])

    def test_the_log_of_this_repository_names_every_tag_its_sources_cite(self):
        root = Path(__file__).resolve().parent.parent
        known = lint.known_tags(root)
        self.assertEqual(len(known), 164)
        self.assertIn("D15", known)
        self.assertIn("D16", known)
        self.assertNotIn("D99.9", known)

    def test_a_caller_that_passes_no_log_asks_for_no_such_check(self):
        comment = lint.Comment(Path("a.ft"), 1, "//", " D99.9")
        self.assertEqual(lint.citation_problems("a.ft", [comment])[0], [])

    def test_two_citation_lines_with_no_clause_are_one_split_citation(self):
        text = "// D1.4\n// D17.2\nfn void f() {}\n"
        found = lint.comments_of(Path("a.ft"), text)
        problems, tagged = lint.citation_problems("a.ft", found)
        self.assertEqual(tagged, 2)
        self.assertEqual(len(problems), 1)
        self.assertIn("a citation split over two lines", problems[0])

    def test_two_citation_lines_that_each_carry_a_clause_stand_together(self):
        text = "// D1.4: the shape of a tag\n// D17.2: an own lvalue moves\n"
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(problems, [])


    def test_a_trailing_citation_below_a_citation_line_is_not_a_split(self):
        # src/fort/ast.ft:156 stands this way: a citation line above the
        # declaration and a citation with a clause at the end of its line
        text = "/// The bits of `flags`.\n/// D1.4\nu32 FLAG_OWN = 1; // D17.2: an own\n"
        found = lint.comments_of(Path("src/fort/probe.ft"), text)
        problems, tagged = lint.citation_problems("src/fort/probe.ft", found)
        self.assertEqual(tagged, 2)
        self.assertEqual(problems, [])

    def test_two_citation_lines_at_different_columns_are_not_a_split(self):
        text = "// D1.4\nstruct s {\n    // D17.2\n    u64 f;\n}\n"
        found = lint.comments_of(Path("a.ft"), text)
        problems, _ = lint.citation_problems("a.ft", found)
        self.assertEqual(problems, [])


class BudgetTest(unittest.TestCase):
    def test_the_tree_of_this_repository_is_inside_the_budget(self):
        root = Path(__file__).resolve().parent.parent
        problems, count = lint.rule_budget(root)
        self.assertEqual(problems, [])
        self.assertIn(f"of {lint.BUDGET_LIMIT}", count)

    def test_a_file_of_exactly_the_budget_passes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "AGENTS.md").write_text("x\n" * lint.BUDGET_LIMIT)
            problems, count = lint.rule_budget(root)
            self.assertEqual(problems, [])
            self.assertIn(f"{lint.BUDGET_LIMIT} lines", count)

    def test_one_line_over_the_budget_is_reported(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "AGENTS.md").write_text("x\n" * (lint.BUDGET_LIMIT + 1))
            problems, _ = lint.rule_budget(root)
            self.assertEqual(problems, [
                f"AGENTS.md:{lint.BUDGET_LIMIT + 1}: {lint.BUDGET_LIMIT + 1} lines, "
                f"over the budget of {lint.BUDGET_LIMIT}; the routing table of "
                f"AGENTS.md says where a fact goes"])

    def test_a_file_with_no_last_newline_counts_its_last_line_once(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            (root / "AGENTS.md").write_text("a\nb\nc")
            _, count = lint.rule_budget(root)
            self.assertIn("3 lines", count)


class DecisionsTest(unittest.TestCase):
    def test_the_log_of_this_repository_has_an_owner_and_a_rule_everywhere(self):
        root = Path(__file__).resolve().parent.parent
        problems, count = lint.rule_decisions(root)
        self.assertEqual(problems, [])
        self.assertIn("144 entries", count)

    def test_a_seeded_log_passes(self):
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"spec/decisions.md": DECISIONS,
                                     "spec/project-overview.md": OVERVIEW})
            problems, count = lint.rule_decisions(root)
            self.assertEqual(problems, [])
            self.assertEqual(count, "decisions: 4 entries, each with owner and rule")

    def test_an_entry_with_no_owner_is_reported(self):
        text = DECISIONS.replace("- owner: spec/project-overview.md\n"
                                 "- rule: the first rule.\n", "- rule: the first rule.\n")
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"spec/decisions.md": text,
                                     "spec/project-overview.md": OVERVIEW})
            problems, _ = lint.rule_decisions(root)
            self.assertEqual(problems,
                             ["spec/decisions.md:5: D1.1 has no owner field"])

    def test_an_entry_with_an_empty_rule_is_reported(self):
        text = DECISIONS.replace("- rule: the second rule.\n", "- rule:\n")
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"spec/decisions.md": text,
                                     "spec/project-overview.md": OVERVIEW})
            problems, _ = lint.rule_decisions(root)
            self.assertEqual(problems,
                             ["spec/decisions.md:9: D1.2 has no rule field"])


class PathsTest(unittest.TestCase):
    def test_a_seeded_tree_of_good_citations_stays_clean(self):
        text = STYLE + "\nSee `notes/style.md` 1 and `spec/decisions.md`.\n"
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"notes/style.md": text,
                                     "spec/decisions.md": DECISIONS,
                                     "spec/project-overview.md": OVERVIEW})
            problems, count = lint.rule_paths(root)
            self.assertEqual(problems, [])
            self.assertIn("references to a document", count)

    def test_a_notes_path_where_a_spec_file_is_meant_is_reported(self):
        text = STYLE + "\nThe log is `notes/decisions.md`.\n"
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"notes/style.md": text,
                                     "spec/decisions.md": DECISIONS,
                                     "spec/project-overview.md": OVERVIEW})
            problems, _ = lint.rule_paths(root)
            self.assertEqual(problems, [
                "notes/style.md:9: notes/decisions.md does not exist; "
                "the file is spec/decisions.md (notes/style.md 4)"])

    def test_a_spec_path_where_a_notes_file_is_meant_is_reported(self):
        text = STYLE + "\nThe conventions are in `spec/style.md`.\n"
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"notes/style.md": text,
                                     "spec/decisions.md": DECISIONS,
                                     "spec/project-overview.md": OVERVIEW})
            problems, _ = lint.rule_paths(root)
            self.assertEqual(problems, [
                "notes/style.md:9: spec/style.md does not exist; "
                "the file is notes/style.md (notes/style.md 4)"])

    def test_a_line_number_in_a_citation_is_reported(self):
        text = STYLE + "\nThe rule is at `notes/style.md:12`.\n"
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"notes/style.md": text})
            problems, _ = lint.rule_paths(root)
            self.assertEqual(problems, [
                "notes/style.md:9: notes/style.md:12 cites a line number; "
                "cite the section (notes/style.md 4)"])

    def test_the_sentence_that_states_the_rule_is_excused(self):
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"notes/style.md": STYLE})
            self.assertEqual(lint.rule_paths(root)[0], [])

    def test_an_exemption_that_matches_nothing_is_reported(self):
        text = STYLE.replace("`notes/style.md:158` is not", "a line number is not")
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"notes/style.md": text})
            problems, _ = lint.rule_paths(root)
            self.assertEqual(len(problems), 1)
            self.assertIn("matches nothing; delete it", problems[0])

    def test_a_comment_of_a_source_is_read_too(self):
        text = "// The rule is in `notes/decisions.md`.\nfn void f() {}\n"
        with tempfile.TemporaryDirectory() as name:
            root = seed(Path(name), {"src/fort/probe.ft": text,
                                     "spec/decisions.md": DECISIONS,
                                     "spec/project-overview.md": OVERVIEW})
            problems, _ = lint.rule_paths(root)
            self.assertEqual(len(problems), 1)
            self.assertIn("the file is spec/decisions.md", problems[0])


class CommandLineTest(unittest.TestCase):
    def test_a_clean_seeded_tree_exits_0(self):
        with tempfile.TemporaryDirectory() as name:
            status, printed = run(clean_tree(Path(name)))
            self.assertEqual((status, printed), (0, []))

    def test_each_rule_can_run_alone(self):
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name))
            for rule in sorted(lint.RULES):
                self.assertEqual(run(root, rule), (0, []), rule)

    def test_the_budget_rule_goes_red_on_its_own_tree(self):
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name), **{"AGENTS.md": "x\n" * 400})
            status, printed = run(root)
            self.assertEqual(status, 1)
            self.assertEqual(len(printed), 1)
            self.assertIn("400 lines, over the budget", printed[0])

    def test_the_citation_rule_goes_red_on_its_own_tree(self):
        broken = MODULE.replace("/// D9.6: a module exports every top-level declaration",
                                "/// the module exports everything (D9.6)")
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name), **{"src/fort/probe.ft": broken})
            status, printed = run(root)
            self.assertEqual(status, 1)
            self.assertEqual(printed, [
                "src/fort/probe.ft:29: outside the shape of notes/style.md 1.4: "
                "/// the module exports everything (D9.6)"])

    def test_the_marker_rule_goes_red_on_its_own_tree(self):
        broken = HEADER.replace("    // The doubling", "    /// The doubling")
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name), **{"src/bootstrap/probe.h": broken})
            status, printed = run(root)
            self.assertEqual(status, 1)
            self.assertEqual(printed, [
                "src/bootstrap/probe.h:28: /// on a function body; "
                "notes/style.md 1.3 asks for //"])

    def test_the_decisions_rule_goes_red_on_its_own_tree(self):
        broken = DECISIONS.replace("- owner: spec/project-overview.md\n"
                                   "- rule: the second rule.\n",
                                   "- rule: the second rule.\n")
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name), **{"spec/decisions.md": broken})
            status, printed = run(root)
            self.assertEqual(status, 1)
            self.assertEqual(printed,
                             ["spec/decisions.md:9: D1.2 has no owner field"])

    def test_the_paths_rule_goes_red_on_its_own_tree(self):
        broken = STYLE + "\nThe log is `notes/decisions.md`.\n"
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name), **{"notes/style.md": broken})
            status, printed = run(root)
            self.assertEqual(status, 1)
            self.assertEqual(printed, [
                "notes/style.md:9: notes/decisions.md does not exist; "
                "the file is spec/decisions.md (notes/style.md 4)"])

    def test_the_count_lines_name_every_rule(self):
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                status = lint.main(["--root", str(root)])
            printed = [line for line in out.getvalue().split("\n") if line]
            self.assertEqual(status, 0)
            self.assertEqual([line.split(":")[0] for line in printed],
                             sorted(lint.RULES))


def tracked_code_files(root):
    """Every .c, .h and .ft file git tracks, as paths relative to the root.

    A tree that `git archive` exported is not a checkout, and the `--root`
    workflow of the lint makes those, so the two closure tests skip there rather
    than raise (T-103, round 3).
    """
    got = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True,
                         check=False)
    if got.returncode != 0:
        raise unittest.SkipTest(
            f"{root} is not a git checkout, so git ls-files says nothing")
    return [name for name in got.stdout.decode().split("\0")
            if name.endswith((".c", ".h", ".ft"))]


class RepositoryTest(unittest.TestCase):
    """The tree this lint ships with keeps every rule it states."""

    def test_the_lint_reports_nothing_over_this_repository(self):
        root = Path(__file__).resolve().parent.parent
        status, printed = run(root)
        self.assertEqual(printed, [])
        self.assertEqual(status, 0)

    def test_every_code_file_is_read_or_excluded(self):
        # A lint with a silent hole is worse than no lint, because the next
        # reader trusts it: SOURCE_GLOBS left 13 files out for no stated reason
        # until a review counted them (T-103, round 2). Every .c, .h and .ft
        # file git tracks is read by the lint or stands under a prefix EXCLUDED
        # names with its reason.
        root = Path(__file__).resolve().parent.parent
        collected = lint.collect(root, lint.SOURCE_GLOBS)
        read = {str(path.relative_to(root)) for path in collected}
        prefixes = tuple(prefix for prefix, _ in lint.EXCLUDED)
        silent = sorted(name for name in tracked_code_files(root)
                        if name not in read and not name.startswith(prefixes))
        self.assertEqual(silent, [], "code neither read nor named in EXCLUDED")

    def test_every_exclusion_names_code_that_exists(self):
        # the mirror: a prefix that matches no file is a hole in the other
        # direction, a reason kept for a directory that has gone
        root = Path(__file__).resolve().parent.parent
        tracked = tracked_code_files(root)
        for prefix, reason in lint.EXCLUDED:
            with self.subTest(prefix=prefix):
                self.assertTrue([n for n in tracked if n.startswith(prefix)], prefix)
                self.assertGreater(len(reason), 40, prefix)

    def test_the_corpus_the_lint_reads_is_the_measured_one(self):
        root = Path(__file__).resolve().parent.parent
        self.assertEqual(len(lint.collect(root, lint.SOURCE_GLOBS)), 356)


if __name__ == "__main__":
    unittest.main()
