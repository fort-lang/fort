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
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import check_decisions  # noqa: E402
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
fn read(probe* p) u64 {
    // The order matters here.
    return p->value;
}

// A free note that stands alone and marks nothing.

/// D9.6: a module exports every top-level declaration
fn twice(u64 n) u64 {
    return n * 2;
}

/// The sum of two probes. The signature wraps, so a comment inside it stands
/// in a parameter list and not on a declaration.
fn add(u64 a,
           // the second operand, named here because the line ran out
           u64 b) u64 {
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
fn probe() void {
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

# The history field of spec/decisions.md, seeded one note for each verdict the
# rule of notes/style.md 4 gives. HISTORY_VERDICT below says what the rule asks
# of each entry and why, written from the rule and not from the tool.
HISTORY_LOG = """\
# The decisions

## D1 Scope

### D1.1 A note in the past tense
- owner: spec/project-overview.md
- rule: the first rule.
- history: Amended 2026-09-13 (T-121): the citation was wrong. Until then the rule read
  "the draft is the one in force".

### D1.2 A note that says what the amendment made true
- owner: spec/project-overview.md
- rule: the second rule.
- history: Amended 2026-09-13 (T-121): the rule cited `AGENTS.md`. The rule now names
  `notes/style.md` 4.

### D1.3 A note that reports another rule in the present tense
- owner: spec/project-overview.md
- rule: the third rule.
- history: Amended 2026-09-13 (T-121): the citation was wrong. D1.1's rule still says the
  draft is the one in force.

### D1.4 A note that quotes the sentence it replaces
- owner: spec/project-overview.md
- rule: the fourth rule.
- history: Amended 2026-09-13 (T-121): the citation was wrong. Until then the rule read
  "this decision requires two runs".

### D1.5 A note that reports its own rule in the present tense
- owner: spec/project-overview.md
- rule: the fifth rule.
- history: Amended 2026-09-13 (T-121): the citation was wrong. This decision does not ask
  for a second run.

### D1.6 A report that a dated note corrects
- owner: spec/project-overview.md
- rule: the sixth rule.
- history: Amended 2026-09-12 (T-120): the citation was wrong. This decision does not ask
  for a second run. Amended 2026-09-13 (T-121): it does ask for one from this date, so the
  sentence above describes the log as it stood on 2026-09-12.

### D1.7 A note whose report wraps over two lines
- owner: spec/project-overview.md
- rule: the seventh rule.
- history: Amended 2026-09-13 (T-121): the citation was wrong. D1.1's rule
  still says the draft is the one in force.

### D1.8 A note whose past tense does not silence the rule
- owner: spec/project-overview.md
- rule: the eighth rule.
- history: Amended 2026-09-13 (T-121): the citation was wrong. Until then the rule read:
  this decision requires two runs.

### D1.9 A note whose subject is a bare decision tag
- owner: spec/project-overview.md
- rule: the ninth rule.
- history: Amended 2026-09-13 (T-122): the citation was wrong. D1.1 requires two runs of
  the compiler.

### D1.10 A bare decision tag inside a relative clause
- owner: spec/project-overview.md
- rule: the tenth rule.
- history: Amended 2026-09-13 (T-122): the rule rejected every test file D1.1 names
  `NNN_name.ft`.
"""

# What the rule asks of each seeded entry: the line of the phrase it reports, or
# None for an entry it lets through, and why. The lines are counted by hand off
# the text above, where line 1 is `# The decisions`.
HISTORY_VERDICT = {
    "D1.1": (None, "the past tense carries the date of its note with it"),
    "D1.2": (None, "`now` says the amendment itself made the rule read this way"),
    "D1.3": (20, "a present-tense report of another entry's rule, with no date"),
    "D1.4": (None, "the words stand inside a quotation, so they are the old rule"),
    "D1.5": (32, "a present-tense report of this entry's own rule, with no date"),
    "D1.6": (None, "a later note dates the report, the repair notes/style.md 4 asks for"),
    "D1.7": (45, "the same report wrapped over two lines, reported at the first"),
    "D1.8": (52, "the past tense of the note does not reach the clause after the colon"),
    "D1.9": (57, "a bare tag at the head of a sentence is the subject of a report"),
    "D1.10": (None, "a bare tag inside a sentence is a relative clause, which "
                    "reports nothing"),
}

# The phrase the rule quotes back for each entry it reports.
HISTORY_PHRASE = {
    "D1.3": "D1.1's rule still says",
    "D1.5": "This decision does not ask",
    "D1.7": "D1.1's rule still says",
    "D1.8": "this decision requires",
    "D1.9": "D1.1 requires",
}

# The two notes the log really held, quoted from spec/decisions.md at 8a1eb48,
# the revision before T-109 repaired them. A rule measured on seeded text alone
# says nothing about the text it ships to read, so these two stay here as the
# oracle: the rule must report them and must report nothing after the repair
# T-109 made, which the tree itself is (T-121).
REAL_LOG = """\
# The decisions

## D19 The IR contract

### D19.5 A real note, as it stood before T-109
- owner: `toolchain.md` (6, the IR contract).
- rule: the rule of the real entry.
- history: Amended 2026-09-10 with how the naming half is checked: `opt -passes=verify` cannot
  enforce it, because an instruction after a terminator makes the verifier *create* an
  implicit number rather than reject the module. `opt-18` exits 0 on a block whose
  `unreachable` is followed by a `br`, splits it and prints the remainder as `0: ; No
  predecessors!` -- an implicitly numbered block, which is the one thing this decision
  forbids. Amended 2026-09-12 (T-039) with which comparison performs it. The module
  comparison it does run is stage1's against stage2's, which this decision does not ask for
  and which is stronger, since it holds the two compilers against each other rather than one
  compiler against itself. It runs in both build modes, as this decision requires, and
  `diff` is still the debugging output.

## D20 The language server

### D20.5 A real note, as it stood before T-109
- owner: `lsp.md` (1, the milestones).
- rule: the rule of the real entry.
- history: Amended 2026-09-12 (T-105): two citations. The first said the protocol and the
  server "land after the bootstrap fixpoint (D19.5)". D19.5 requires the emitted text to be
  a function of the program and states the condition of the fixpoint, and it decides no
  milestone. Its rule still says the bootstrap script compares the two stages with `cmp`
  over `-S` output.
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


class HistoryTest(unittest.TestCase):
    """The tense rule of notes/style.md 4 over seeded notes and over real ones."""

    def problems_of(self, text):
        """The problems of one seeded log, by the entry each names."""
        problems, _, _ = lint.history_problems(text)
        by_entry = {}
        for problem in problems:
            by_entry.setdefault(problem.split(": ")[1].split(" ")[0], []).append(problem)
        return problems, by_entry

    def test_every_seeded_note_gets_the_verdict_the_rule_asks_for(self):
        _, by_entry = self.problems_of(HISTORY_LOG)
        for tag, (line, why) in HISTORY_VERDICT.items():
            with self.subTest(entry=tag, why=why):
                if line is None:
                    self.assertEqual(by_entry.get(tag, []), [])
                    continue
                self.assertEqual(len(by_entry.get(tag, [])), 1, by_entry.get(tag))
                self.assertTrue(by_entry[tag][0].startswith(
                    f"spec/decisions.md:{line}: {tag} "), by_entry[tag][0])
                self.assertIn(f'"{HISTORY_PHRASE[tag]}"', by_entry[tag][0])

    def test_the_verdict_table_covers_every_seeded_entry(self):
        entries, _ = check_decisions.read_fielded(HISTORY_LOG)
        self.assertEqual(sorted(entries), sorted(HISTORY_VERDICT))

    def test_the_seeded_log_reports_exactly_five_notes(self):
        problems, _ = self.problems_of(HISTORY_LOG)
        self.assertEqual(len(problems), 5, problems)

    def test_one_seeded_note_is_reported_as_this_exact_line(self):
        _, by_entry = self.problems_of(HISTORY_LOG)
        self.assertEqual(by_entry["D1.3"], [
            "spec/decisions.md:20: D1.3 reports a rule of the log in the present "
            'tense: "D1.1\'s rule still says"; write the past tense or append a '
            "dated note (notes/style.md 4)"])

    def test_a_bare_tag_at_the_head_of_a_sentence_is_reported_as_this_exact_line(self):
        # the oracle of the widening T-122 made: D1.9 heads its sentence with a
        # tag and is reported, D1.10 carries the same tag inside a relative
        # clause and is not. The line is written out here by hand, as the
        # D1.3 test above writes its own
        _, by_entry = self.problems_of(HISTORY_LOG)
        self.assertEqual(by_entry["D1.9"], [
            "spec/decisions.md:57: D1.9 reports a rule of the log in the present "
            'tense: "D1.1 requires"; write the past tense or append a '
            "dated note (notes/style.md 4)"])
        self.assertEqual(by_entry.get("D1.10", []), [])

    def test_the_count_line_names_the_fields_and_the_dated_ones(self):
        _, fields, dated = lint.history_problems(HISTORY_LOG)
        self.assertEqual((fields, dated), (10, 1))

    def test_all_five_real_reports_the_log_held_are_reported(self):
        # the whole reason the rule exists: these are the words of D19.5 and
        # D20.5 at 8a1eb48, before T-109 repaired them. The revision yields 5
        # problems, so REAL_LOG carries all 5 and not a sample (T-121, round 2).
        # The fourth is the bare tag T-122 widened the subject to read, and the
        # words are the ones the log really held (T-122)
        problems, _, _ = lint.history_problems(REAL_LOG)
        self.assertEqual(problems, [
            "spec/decisions.md:12: D19.5 reports a rule of the log in the present "
            'tense: "this decision forbids"; write the past tense or append a '
            "dated note (notes/style.md 4)",
            "spec/decisions.md:14: D19.5 reports a rule of the log in the present "
            'tense: "this decision does not ask"; write the past tense or append a '
            "dated note (notes/style.md 4)",
            "spec/decisions.md:16: D19.5 reports a rule of the log in the present "
            'tense: "this decision requires"; write the past tense or append a '
            "dated note (notes/style.md 4)",
            "spec/decisions.md:25: D20.5 reports a rule of the log in the present "
            'tense: "D19.5 requires"; write the past tense or append a '
            "dated note (notes/style.md 4)",
            "spec/decisions.md:27: D20.5 reports a rule of the log in the present "
            'tense: "Its rule still says"; write the past tense or append a '
            "dated note (notes/style.md 4)"])

    def test_a_field_joins_its_wrapped_lines_before_it_reads_them(self):
        # the repair sentence of D19.5 wraps between "describe the log as" and
        # "it stood on 2026-09-12", so a line-at-a-time reader sees neither half
        text = HISTORY_LOG.replace("sentence above describes the log as it stood on",
                                   "sentence above describes the log as\n  it stood on")
        problems, _, dated = lint.history_problems(text)
        self.assertEqual(dated, 1)
        self.assertEqual(len(problems), 5, problems)

    def test_the_past_tense_of_the_note_is_not_what_silences_the_rule(self):
        # D1.8 is the seeded proof: `until then the rule read:` is past, and the
        # clause after the colon is a sentence of its own and still present. So
        # the quotation is the silencer and the note's own tense is not
        # (T-121, round 2; the tool and notes/style.md 4 both say so now)
        _, by_entry = self.problems_of(HISTORY_LOG)
        self.assertEqual(len(by_entry["D1.8"]), 1)
        self.assertEqual(by_entry.get("D1.4", []), [])

    def test_a_report_inside_a_quotation_is_the_old_rule_and_not_a_claim(self):
        text = " Until then the rule read \"this decision requires two runs\"."
        self.assertTrue(lint.HISTORY_REPORT.search(text))
        self.assertIsNone(lint.HISTORY_REPORT.search(lint.unquoted(text)))

    def test_a_sentence_ends_at_a_stop_a_semicolon_or_a_colon(self):
        found = lint.sentences_of("one. two; three: four")
        self.assertEqual(found, [(0, "one."), (5, "two;"), (10, "three:"), (17, "four")])

    def test_the_line_of_each_character_follows_the_wrapping(self):
        _, body, at = lint.history_fields(HISTORY_LOG)[6]
        self.assertEqual(body.split(". ")[-1], "D1.1's rule still says the draft "
                                               "is the one in force.")
        self.assertEqual(at[body.index("D1.1's")], 45)
        self.assertEqual(at[body.index("still says")], 46)


class HistoryLimitTest(unittest.TestCase):
    """Each gap HISTORY_LIMITS states, measured rather than asserted.

    A limit in prose rots. These four tests hold the numbers the docstring of
    tools/knowledge_lint.py quotes, so a gap that closes fails a test and the
    prose is rewritten with it (T-121).
    """

    def log(self):
        """The decision log of this repository."""
        root = Path(__file__).resolve().parent.parent
        return (root / "spec/decisions.md").read_text(encoding="utf-8")

    def with_a_tag_anywhere(self):
        """HISTORY_REPORT with the head-of-sentence test dropped from the tag.

        The shipped subject reads a bare tag only at the head of a sentence.
        This pattern reads one anywhere, and the difference between the two is
        what the first limit measures (T-122).
        """
        anywhere = lint.HISTORY_SUBJECT.replace(
            lint.HISTORY_HEAD_TAG, r"D\d+(?:\.\d+)?")
        return re.compile(
            r"\b(" + anywhere + lint.HISTORY_BETWEEN + r"(?:" + lint.HISTORY_VERBS
            + r"|" + lint.HISTORY_NEGATED + r"))\b", re.IGNORECASE)

    def sweep(self, pattern, field="history", dated=True, anchored=False, text=None):
        """The sentences of a decision log this pattern reports.

        `anchored=True` asks for the opposite set: the sentences the pattern
        matches and HISTORY_ANCHOR then excuses. `text` defaults to the log of
        this repository.
        """
        text = self.log() if text is None else text
        entries, _ = check_decisions.read_fielded(text)
        found = []
        for tag in sorted(entries, key=check_decisions.key_of):
            body = " ".join(entries[tag].get(field, "").split())
            if not body:
                continue
            if dated and field == "history" and lint.field_is_dated(body):
                continue
            for _, sentence in lint.sentences_of(body):
                clean = lint.unquoted(sentence)
                if bool(lint.HISTORY_ANCHOR.search(clean)) != anchored:
                    continue
                found += [(tag, m.group(1)) for m in pattern.finditer(clean)]
        return found

    def test_the_rule_reports_nine_sentences_in_three_fields_without_the_escape(self):
        found = self.sweep(lint.HISTORY_REPORT, dated=False)
        # each phrase is written out, so a run that bridged one subject on to
        # another subject's verb would read wrongly here rather than pass a
        # count (T-122, round 2, the seventh limit)
        self.assertEqual(found, [
            ("D17.4", "D3.10 decides"),
            ("D19.5", "this decision forbids"),
            ("D19.5", "this decision does not ask"),
            ("D19.5", "this decision requires"),
            ("D19.5", "The rule names"),
            ("D19.5", "The rule also carries"),
            ("D19.5", "the rule itself requires"),
            ("D20.5", "D19.5 requires"),
            ("D20.5", "Its rule still says")])
        self.assertEqual(sorted({tag for tag, _ in found}),
                         ["D17.4", "D19.5", "D20.5"])
        # the third limit: 2 of the 7 stand after the dated note that excuses
        # them, so the escape is coarser than the reason it is right
        after = [phrase for _, phrase in found
                 if phrase in ("The rule names", "The rule also carries")]
        self.assertEqual(len(after), 2)

    def test_a_bare_tag_inside_a_sentence_is_the_one_gap_that_is_left(self):
        # the first limit: the shipped subject reads a bare tag at the head of a
        # sentence, and dropping that test would add exactly one sentence over
        # the log, D9.1's `every test file D14.4 names NNN_name.ft`. It is a
        # relative clause and reports nothing, so the gap that is left holds no
        # true report today (T-122)
        for dated in (True, False):
            with self.subTest(dated=dated):
                shipped = self.sweep(lint.HISTORY_REPORT, dated=dated)
                anywhere = self.sweep(self.with_a_tag_anywhere(), dated=dated)
                self.assertEqual([hit for hit in anywhere if hit not in shipped],
                                 [("D9.1", "D14.4 names")])

    def test_a_bare_tag_reports_only_where_it_heads_its_sentence(self):
        # the same difference on two hand-written sentences rather than on the
        # log, so the pattern is watched where a reader can read both halves
        head = "D3.10 decides function types and function pointers"
        clause = "every test file D14.4 names NNN_name.ft"
        self.assertEqual(lint.HISTORY_REPORT.search(head).group(1), "D3.10 decides")
        self.assertIsNone(lint.HISTORY_REPORT.search(clause))
        self.assertEqual(self.with_a_tag_anywhere().search(clause).group(1),
                         "D14.4 names")

    def test_the_widened_subject_catches_the_motivating_case_it_used_to_miss(self):
        # D20.5 before T-109 carried a bare-tag report of its own, inside the
        # very case the rule was built for, and the old subject missed it
        # (T-121, round 2). The widened subject reads the log at 8a1eb48 and
        # reports 6 sentences where the old one reported 4 (T-122)
        old = subprocess.run(["git", "show", "8a1eb48:spec/decisions.md"],
                             cwd=Path(__file__).resolve().parent.parent,
                             capture_output=True, check=False)
        if old.returncode != 0:
            raise unittest.SkipTest("8a1eb48 is not in this checkout")
        text = old.stdout.decode()
        found = self.sweep(lint.HISTORY_REPORT, text=text)
        self.assertEqual(found, [("D17.4", "D3.10 decides"),
                                 ("D19.5", "this decision forbids"),
                                 ("D19.5", "this decision does not ask"),
                                 ("D19.5", "this decision requires"),
                                 ("D20.5", "D19.5 requires"),
                                 ("D20.5", "Its rule still says")])
        # the relative clause of D9.1 stood in that revision too, and the
        # widened subject leaves it alone there as it does today
        self.assertNotIn(("D9.1", "D14.4 names"), found)

    def test_a_report_one_subordinator_from_the_head_is_missed(self):
        # the first limit says the position test is a proxy for the reading,
        # and this is the case that separates the two questions: one clause,
        # read at the head of a sentence and not read behind `because`
        # (T-122, round 2)
        self.assertEqual(lint.HISTORY_REPORT.search(
            "D3.10 decides function types and function pointers").group(1),
            "D3.10 decides")
        self.assertIsNone(lint.HISTORY_REPORT.search(
            "the citation was wrong, because D3.10 decides function types and "
            "function pointers"))

    def test_the_same_signature_over_the_rule_fields_reports_two_sentences(self):
        # the fourth limit: a rule states a requirement in the present tense by
        # design, and both hits are D19.5's rule describing its own scope
        found = self.sweep(lint.HISTORY_REPORT, field="rule")
        self.assertEqual(found, [("D19.5", "this rule states"),
                                 ("D19.5", "this rule requires holds")])

    def test_the_same_signature_over_the_rationale_fields_reports_nothing(self):
        # the other half of the fourth limit: 17 entries carry a rationale and
        # the gap costs 0 today, which is what makes it a gap and not a defect
        entries, _ = check_decisions.read_fielded(self.log())
        carried = [t for t, f in entries.items() if f.get("rationale", "").strip()]
        self.assertEqual(len(carried), 17)
        self.assertEqual(self.sweep(lint.HISTORY_REPORT, field="rationale"), [])

    def test_a_wider_subject_and_verb_list_reports_thirty_nine(self):
        # the fifth limit: `it`, `nothing`, `the text` and `the log` as subjects
        # and `is`, `has`, `stands` and `compares` as verbs take 9 to 39. The
        # limit claims no split of the 32 into prose and report: which is which
        # is a reading, and no test holds a reading (T-121, round 2). T-124's
        # note on D4.5 took 31 to 35 and the tags from 9 to 10; all four of the
        # sentences are prose about the ruling that note makes, and none of them
        # is a report of what another decision says. T-131's notes on D13.2,
        # D18.1 and D19.5 took 35 to 39 and the tags from 10 to 11, D18.1 being
        # the new one: `it reads`, `it holds`, `it, and T-131's log carries` and
        # `it compares`, all four prose about those rulings for the same reason
        wide = re.compile(
            r"\b((?:" + lint.HISTORY_SUBJECT[3:-1] + r"|it|nothing|the (?:text|log))"
            + lint.HISTORY_BETWEEN + r"(?:" + lint.HISTORY_VERBS
            + r"|is|has|stands|compares|" + lint.HISTORY_NEGATED + r"))\b",
            re.IGNORECASE)
        found = self.sweep(wide, dated=False)
        self.assertEqual(len(found), 39)
        self.assertEqual(len({tag for tag, _ in found}), 11)

    def test_the_anchor_excuses_seven_sentences_the_pattern_would_report(self):
        # the sixth limit, measured: `now` and a date in the same sentence as
        # the report. Without this test the seeded D1.2 would pass vacuously
        found = self.sweep(lint.HISTORY_REPORT, dated=False, anchored=True)
        self.assertEqual([tag for tag, _ in found],
                         ["D1.2", "D1.3", "D2.2", "D17.4", "D19.5", "D20.5", "D20.5"])

    def test_the_run_between_subject_and_verb_costs_nothing_and_buys_four(self):
        # the seventh limit: a report built only from words both lists hold used
        # to escape, because the subject had to stand against the verb. Widening
        # the run to HISTORY_RUN words reports 0 over the tree, adds 1 sentence
        # to the sweep without the escape, and catches four paraphrases of
        # D20.5's real defect (T-121, round 2)
        tight = re.compile(
            r"\b(" + lint.HISTORY_SUBJECT + lint.HISTORY_ADVERB + r"\s+(?:"
            + lint.HISTORY_VERBS + r"|" + lint.HISTORY_NEGATED + r"))\b",
            re.IGNORECASE)
        self.assertEqual(self.sweep(lint.HISTORY_REPORT), [])
        self.assertEqual(len(self.sweep(lint.HISTORY_REPORT, dated=False)), 9)
        self.assertEqual(len(self.sweep(tight, dated=False)), 8)
        for paraphrase in (
                "the rule of D19.5 says the bootstrap compares with cmp",
                "this decision, after T-109, says the two stages are compared",
                "the rule still only says the bootstrap script compares them",
                "D19.5's rule, which nobody amended, says the same thing"):
            with self.subTest(paraphrase=paraphrase):
                self.assertIsNone(tight.search(paraphrase))
                self.assertIsNotNone(lint.HISTORY_REPORT.search(paraphrase))

    def test_the_bound_stops_a_long_subject_bridging_and_not_a_one_token_one(self):
        # the other half of the seventh limit, narrowed by T-122: past the bound
        # a report is missed, and that stops a long subject reaching a verb of
        # another subject. A one-token subject leaves the whole run free, and a
        # bare tag is the only one-token subject either list holds, so the
        # widening opened this case: the match names `the ticket` as the real
        # subject and reports the tag instead
        self.assertIsNone(lint.HISTORY_REPORT.search(
            "the rule of the other entry, which nobody has touched since, "
            "requires two runs"))
        bridged = lint.HISTORY_REPORT.search(
            "D9.1 aside, the ticket names the file wrongly")
        self.assertEqual(bridged.group(1), "D9.1 aside, the ticket names")
        # and 0 sentences of the log stand in the gap, which the nine phrases
        # of test_the_rule_reports_nine_sentences_in_three_fields... pin

    def test_every_limit_names_a_gap_and_says_what_stands_in_it(self):
        self.assertEqual(len(lint.HISTORY_LIMITS), 7)
        for gap, why in lint.HISTORY_LIMITS:
            with self.subTest(gap=gap):
                self.assertGreater(len(why), 80, gap)


class HistoryRepositoryTest(unittest.TestCase):
    def test_a_tree_with_no_decision_log_reports_nothing_rather_than_raising(self):
        # `known_tags` guards the same read with `exists()`, and `--root` over a
        # partial tree is the workflow that reaches both (T-121, round 2)
        with tempfile.TemporaryDirectory() as name:
            problems, count = lint.rule_history(Path(name))
            self.assertEqual(problems, [])
            self.assertEqual(count, "history: 0 history fields, 0 with a dated note")

    def test_the_log_of_this_repository_reports_nothing(self):
        root = Path(__file__).resolve().parent.parent
        problems, count = lint.rule_history(root)
        self.assertEqual(problems, [])
        self.assertEqual(count, "history: 59 history fields, 4 with a dated note")

    def test_the_field_count_is_the_one_grep_counts(self):
        # `grep -c '^- history:' spec/decisions.md` says 59 (notes/style.md 4)
        root = Path(__file__).resolve().parent.parent
        text = (root / "spec/decisions.md").read_text(encoding="utf-8")
        self.assertEqual(len(lint.history_fields(text)), 59)
        self.assertEqual(
            sum(1 for line in text.split("\n") if line.startswith("- history:")), 59)


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

    def test_the_history_rule_goes_red_on_its_own_tree(self):
        broken = DECISIONS.replace(
            "- history: Amended 2026-09-13 (T-103): nothing changed.",
            "- history: Amended 2026-09-13 (T-103): nothing changed. D1.1's rule"
            " still says the first rule.")
        with tempfile.TemporaryDirectory() as name:
            root = clean_tree(Path(name), **{"spec/decisions.md": broken})
            status, printed = run(root, "history")
            self.assertEqual(status, 1)
            self.assertEqual(printed, [
                "spec/decisions.md:12: D1.2 reports a rule of the log in the present "
                'tense: "D1.1\'s rule still says"; write the past tense or append a '
                "dated note (notes/style.md 4)"])

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
        self.assertEqual(len(lint.collect(root, lint.SOURCE_GLOBS)), 365)


if __name__ == "__main__":
    unittest.main()
