#!/usr/bin/env python3
"""Unit tests of tools/knowledge_lint.py: every rule, and every rule broken.

A check that cannot fail is worse than no check. Each rule runs on a clean
seeded tree. Each rule also runs on a seeded tree that breaks it.

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
# history rule gives. HISTORY_VERDICT below says what the rule asks
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

# These notes ensure that the rule reads repository text and seeded text.
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
    files = {}
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


class HistoryTest(unittest.TestCase):
    """Test the history tense rule over seeded and repository notes."""

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
        # A tag at the sentence head is reported.
        # The same tag inside a relative clause is not reported.
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
        # REAL_LOG contains all 5 problems from the stored revision.
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
        # The repair sentence wraps between "describe the log as" and
        # "it stood on 2026-09-12", so a line-at-a-time reader sees neither half
        text = HISTORY_LOG.replace("sentence above describes the log as it stood on",
                                   "sentence above describes the log as\n  it stood on")
        problems, _, dated = lint.history_problems(text)
        self.assertEqual(dated, 1)
        self.assertEqual(len(problems), 5, problems)

    def test_the_past_tense_of_the_note_is_not_what_silences_the_rule(self):
        # `until then the rule read:` is past, but the clause after the colon is present.
        # clause after the colon is a sentence of its own and still present. So
        # the quotation is the silencer and the note's own tense is not
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
    """Measure each gap that HISTORY_LIMITS states.

    These four tests hold the numbers in tools/knowledge_lint.py.
    A closed gap fails a test and requires an update.
    """

    def log(self):
        """The decision log of this repository."""
        root = Path(__file__).resolve().parent.parent
        return (root / "spec/decisions.md").read_text(encoding="utf-8")

    def with_a_tag_anywhere(self):
        """HISTORY_REPORT with the head-of-sentence test dropped from the tag.

        The shipped subject reads a bare tag only at the head of a sentence.
        This pattern reads one anywhere, and the difference between the two is
        what this test measures.
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
        # Each phrase is explicit. A pattern that bridges two subjects fails this list.
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
        # Two reports follow the dated note that excuses the field.
        after = [phrase for _, phrase in found
                 if phrase in ("The rule names", "The rule also carries")]
        self.assertEqual(len(after), 2)

    def test_a_bare_tag_inside_a_sentence_is_the_one_gap_that_is_left(self):
        # The subject reads a bare tag only at the sentence head.
        # Dropping that condition adds exactly one sentence from the log. It is a
        # relative clause and reports nothing, so the gap that is left holds no
        # true report today.
        for dated in (True, False):
            with self.subTest(dated=dated):
                shipped = self.sweep(lint.HISTORY_REPORT, dated=dated)
                anywhere = self.sweep(self.with_a_tag_anywhere(), dated=dated)
                self.assertEqual([hit for hit in anywhere if hit not in shipped],
                                 [("D9.1", "D14.4 names")])

    def test_a_bare_tag_reports_only_where_it_heads_its_sentence(self):
        # Compare the two positions with short, explicit sentences.
        head = "D3.10 decides function types and function pointers"
        clause = "every test file D14.4 names NNN_name.ft"
        self.assertEqual(lint.HISTORY_REPORT.search(head).group(1), "D3.10 decides")
        self.assertIsNone(lint.HISTORY_REPORT.search(clause))
        self.assertEqual(self.with_a_tag_anywhere().search(clause).group(1),
                         "D14.4 names")

    def test_the_widened_subject_catches_the_motivating_case_it_used_to_miss(self):
        # The narrow subject misses a bare-tag report in this case.
        # The wider subject reports 6 sentences. The narrow subject reports 4.
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
        # The relative-clause case remains outside the match.
        self.assertNotIn(("D9.1", "D14.4 names"), found)

    def test_a_report_one_subordinator_from_the_head_is_missed(self):
        # Position is a proxy for meaning. This case separates the two questions.
        # The clause matches at a sentence head but not after `because`.
        self.assertEqual(lint.HISTORY_REPORT.search(
            "D3.10 decides function types and function pointers").group(1),
            "D3.10 decides")
        self.assertIsNone(lint.HISTORY_REPORT.search(
            "the citation was wrong, because D3.10 decides function types and "
            "function pointers"))

    def test_the_same_signature_over_the_rule_fields_reports_two_sentences(self):
        # A rule states a requirement in the present tense by design.
        # Both hits describe the rule's own scope.
        found = self.sweep(lint.HISTORY_REPORT, field="rule")
        self.assertEqual(found, [("D19.5", "this rule states"),
                                 ("D19.5", "this rule requires holds")])

    def test_the_same_signature_over_the_rationale_fields_reports_nothing(self):
        # The rationale fields produce no report.
        entries, _ = check_decisions.read_fielded(self.log())
        carried = [t for t, f in entries.items() if f.get("rationale", "").strip()]
        self.assertEqual(len(carried), 18)
        self.assertEqual(self.sweep(lint.HISTORY_REPORT, field="rationale"), [])

    def test_a_wider_subject_and_verb_list_reports_thirty_nine(self):
        # Expand both closed lists. This raises the match count from 9 to 39.
        # The test does not classify the 30 added matches as prose or reports.
        wide = re.compile(
            r"\b((?:" + lint.HISTORY_SUBJECT[3:-1] + r"|it|nothing|the (?:text|log))"
            + lint.HISTORY_BETWEEN + r"(?:" + lint.HISTORY_VERBS
            + r"|is|has|stands|compares|" + lint.HISTORY_NEGATED + r"))\b",
            re.IGNORECASE)
        found = self.sweep(wide, dated=False)
        self.assertEqual(len(found), 39)
        self.assertEqual(len({tag for tag, _ in found}), 11)

    def test_the_anchor_excuses_seven_sentences_the_pattern_would_report(self):
        # `now` or a date excuses a report in the same sentence.
        # The seeded case prevents a vacuous pass.
        found = self.sweep(lint.HISTORY_REPORT, dated=False, anchored=True)
        self.assertEqual([tag for tag, _ in found],
                         ["D1.2", "D1.3", "D2.2", "D17.4", "D19.5", "D20.5", "D20.5"])

    def test_the_run_between_subject_and_verb_costs_nothing_and_buys_four(self):
        # A run of words can separate the subject and verb.
        # HISTORY_RUN adds no repository problem and one unescaped sweep sentence.
        # It also catches four related forms.
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
        # Past the bound, a report is missed. This prevents a cross-clause match.
        # A one-token tag can still reach another subject's verb.
        self.assertIsNone(lint.HISTORY_REPORT.search(
            "the rule of the other entry, which nobody has touched since, "
            "requires two runs"))
        bridged = lint.HISTORY_REPORT.search(
            "D9.1 aside, the ticket names the file wrongly")
        self.assertEqual(bridged.group(1), "D9.1 aside, the ticket names")
        # No repository sentence falls into this gap.

    def test_every_limit_names_a_gap_and_says_what_stands_in_it(self):
        self.assertEqual(len(lint.HISTORY_LIMITS), 7)
        for gap, why in lint.HISTORY_LIMITS:
            with self.subTest(gap=gap):
                self.assertGreater(len(why), 80, gap)


class HistoryRepositoryTest(unittest.TestCase):
    def test_a_tree_with_no_decision_log_reports_nothing_rather_than_raising(self):
        # `rule_history` returns no problem when the decision log is absent.
        # This behavior lets `--root` inspect a partial tree.
        with tempfile.TemporaryDirectory() as name:
            problems, count = lint.rule_history(Path(name))
            self.assertEqual(problems, [])
            self.assertEqual(count, "history: 0 history fields, 0 with a dated note")

    def test_the_log_of_this_repository_reports_nothing(self):
        root = Path(__file__).resolve().parent.parent
        problems, count = lint.rule_history(root)
        self.assertEqual(problems, [])
        self.assertEqual(count, "history: 62 history fields, 4 with a dated note")

    def test_the_field_count_is_the_one_grep_counts(self):
        # `grep -c '^- history:' spec/decisions.md` says 62.
        root = Path(__file__).resolve().parent.parent
        text = (root / "spec/decisions.md").read_text(encoding="utf-8")
        self.assertEqual(len(lint.history_fields(text)), 62)
        self.assertEqual(
            sum(1 for line in text.split("\n") if line.startswith("- history:")), 62)


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
        self.assertIn("148 entries", count)

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


class RepositoryTest(unittest.TestCase):
    """The tree this lint ships with keeps every rule it states."""

    def test_the_lint_reports_nothing_over_this_repository(self):
        root = Path(__file__).resolve().parent.parent
        status, printed = run(root)
        self.assertEqual(printed, [])
        self.assertEqual(status, 0)


if __name__ == "__main__":
    unittest.main()
