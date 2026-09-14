#!/usr/bin/env python3
"""Unit tests of tools/check_decisions.py.

The tool has two jobs and this file tests both. It lints the shape of the
decision log, so every problem it reports is asserted here over a small
synthetic log. And it holds two revisions of the log together, which is the
acceptance criterion of T-100: the words of an entry must survive an edit that
only moves them between the fields. The tests therefore include an edit that
moves a sentence (reported, not a failure) and an edit that changes a word
(reported as a failure), because a comparison that only accepts the good case
proves nothing.

The last class runs the tool over the real spec/decisions.md, so the file in
the repository answers for its own shape.

Run with `python3 -m unittest check_decisions_test` from this directory.
Standard library only; Python 3.12.
"""

import io
import re
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
ROOT = TEST_DIR.parent
DECISIONS_PATH = ROOT / "spec" / "decisions.md"
sys.path.insert(0, str(ROOT / "tools"))

import check_decisions  # noqa: E402

GOOD = """# A log

## D1 First section

### D1.1 The first rule
- owner: `one.md`.
- rule: The first rule says one thing.

### D1.2 The second rule
- owner: `one.md`.
- rule: The second rule says another thing.
- rationale: the user asked for it.
- history: Amended 2026-09-12 (T-100): it said something else.

## D2 Second section

### D2.1 The third rule
- owner: `two.md`.
- rule: The third rule carries a table.

  | Declaration | Meaning |
  |-------------|---------|
  | `i32 mut x` | writable |
"""

BULLETS = """# A log

## D1 First section

Owner: `one.md`.

- **D1.1** The first rule says one thing.
- **D1.2** The second rule says another thing. Rationale: the user asked for it.
  Amended 2026-09-12 (T-100): it said something else.

## D2 Second section

Owner: `two.md`.

- **D2.1** The third rule carries a table.

  | Declaration | Meaning |
  |-------------|---------|
  | `i32 mut x` | writable |
"""


def write(directory, name, text):
    path = Path(directory) / name
    path.write_text(text, encoding="utf-8")
    return str(path)


class ReadFielded(unittest.TestCase):
    """The reader of the form T-100 gave the log."""

    def setUp(self):
        self.entries, self.lines = check_decisions.read_fielded(GOOD)

    def test_every_entry_is_found(self):
        self.assertEqual(sorted(self.entries), ["D1.1", "D1.2", "D2.1"])

    def test_a_field_keeps_its_text(self):
        self.assertEqual(self.entries["D1.1"]["rule"], "The first rule says one thing.")
        self.assertEqual(self.entries["D1.2"]["rationale"], "the user asked for it.")

    def test_an_entry_without_a_field_does_not_carry_it(self):
        self.assertNotIn("history", self.entries["D1.1"])
        self.assertIn("history", self.entries["D1.2"])

    def test_a_table_stays_in_the_field_it_belongs_to(self):
        self.assertIn("| `i32 mut x` | writable |", self.entries["D2.1"]["rule"])

    def test_the_line_of_each_entry_is_recorded(self):
        self.assertEqual(GOOD.split("\n")[self.lines["D1.1"] - 1], "### D1.1 The first rule")

    def test_a_heading_ends_the_entry_before_it(self):
        self.assertNotIn("Second section", self.entries["D1.2"]["history"])


class ReadBullets(unittest.TestCase):
    """The reader of the form that came before T-100."""

    def test_every_entry_is_found(self):
        entries = check_decisions.read_bullets(BULLETS)
        self.assertEqual(sorted(entries), ["D1.1", "D1.2", "D2.1"])

    def test_a_continuation_line_belongs_to_its_entry(self):
        entries = check_decisions.read_bullets(BULLETS)
        self.assertIn("Amended 2026-09-12", entries["D1.2"])

    def test_a_table_belongs_to_its_entry(self):
        entries = check_decisions.read_bullets(BULLETS)
        self.assertIn("| `i32 mut x` | writable |", entries["D2.1"])

    def test_the_owner_line_belongs_to_no_entry(self):
        entries = check_decisions.read_bullets(BULLETS)
        for text in entries.values():
            self.assertNotIn("Owner:", text)


class RuleOf(unittest.TestCase):
    """The helper the two test suites that read a rule call."""

    def test_it_returns_the_rule_and_not_the_other_fields(self):
        rule = check_decisions.rule_of(GOOD, "D1.2")
        self.assertEqual(rule, "The second rule says another thing.")

    def test_an_unknown_tag_raises(self):
        with self.assertRaises(KeyError):
            check_decisions.rule_of(GOOD, "D9.9")


class Lint(unittest.TestCase):
    """One assertion for each problem the lint reports."""

    def problems(self, text):
        return check_decisions.lint("log.md", text)

    def test_a_good_log_has_no_problem(self):
        self.assertEqual(self.problems(GOOD), [])

    def test_an_entry_without_an_owner(self):
        text = GOOD.replace("- owner: `one.md`.\n- rule: The first", "- rule: The first")
        self.assertIn("D1.1 has no owner field", " ".join(self.problems(text)))

    def test_an_entry_without_a_rule(self):
        text = GOOD.replace("- rule: The first rule says one thing.\n", "")
        self.assertIn("D1.1 has no rule field", " ".join(self.problems(text)))

    def test_an_unknown_field(self):
        text = GOOD.replace("- rationale: the user", "- reason: the user")
        self.assertIn("unknown field 'reason'", " ".join(self.problems(text)))

    def test_fields_out_of_order(self):
        text = GOOD.replace(
            "- owner: `two.md`.\n- rule: The third rule carries a table.",
            "- rule: The third rule carries a table.\n- owner: `two.md`.",
        )
        self.assertIn("D2.1 writes its fields out of order", " ".join(self.problems(text)))

    def test_a_line_over_a_hundred_columns(self):
        text = GOOD.replace("one thing.", "one thing " + "x" * 100 + ".")
        problems = " ".join(self.problems(text))
        self.assertIn("columns, over 100", problems)

    def test_an_entry_outside_its_section(self):
        text = GOOD.replace("### D2.1 The third rule", "### D3.1 The third rule")
        self.assertIn("D3.1 is not in section D2", " ".join(self.problems(text)))

    def test_one_field_written_twice(self):
        """The second text used to replace the first with no report."""
        text = GOOD.replace(
            "- rule: The second rule says another thing.",
            "- rule: The second rule says another thing.\n- rule: and a second sentence.",
        )
        self.assertIn("D1.2 writes the rule field twice", " ".join(self.problems(text)))

    def test_a_field_written_twice_keeps_both_texts(self):
        """The reader must lose no word, or the comparison would not see it."""
        text = GOOD.replace(
            "- rule: The second rule says another thing.",
            "- rule: The second rule says another thing.\n- rule: and a second sentence.",
        )
        entries, _ = check_decisions.read_fielded(text)
        self.assertIn("and a second sentence", entries["D1.2"]["rule"])
        self.assertIn("says another thing", entries["D1.2"]["rule"])

    def test_text_that_belongs_to_no_field(self):
        """A sentence between the heading and the first field is invisible to
        the reader, so the lint reports it rather than dropping it."""
        text = GOOD.replace(
            "### D1.1 The first rule\n",
            "### D1.1 The first rule\nA sentence with no field.\n",
        )
        self.assertIn("D1.1 has text that belongs to no field", " ".join(self.problems(text)))

    def test_one_identifier_twice(self):
        text = GOOD.replace("### D1.2 The second rule", "### D1.1 The second rule")
        self.assertIn("appears twice", " ".join(self.problems(text)))


class Compare(unittest.TestCase):
    """The before-and-after comparison, the criterion of T-100."""

    def changed(self, old, new, allowed=()):
        return check_decisions.compare(old, new, set(allowed))[0]

    def moved(self, old, new, allowed=()):
        return check_decisions.compare(old, new, set(allowed))[1]

    def test_the_two_forms_of_one_log_agree(self):
        self.assertEqual(self.changed(BULLETS, GOOD), [])
        self.assertEqual(self.moved(BULLETS, GOOD), [])

    def test_a_changed_word_is_a_failure(self):
        broken = GOOD.replace("says one thing", "says one other thing")
        changed = self.changed(BULLETS, broken)
        self.assertEqual([name for name, _ in changed], ["D1.1"])
        self.assertIn("other", changed[0][1])

    def test_a_deleted_sentence_is_a_failure(self):
        broken = GOOD.replace("- rationale: the user asked for it.\n", "")
        self.assertEqual([name for name, _ in self.changed(BULLETS, broken)], ["D1.2"])

    def test_a_lost_entry_is_a_failure(self):
        broken = GOOD.replace("### D1.1 The first rule", "### D1.9 The first rule")
        names = [name for name, _ in self.changed(BULLETS, broken)]
        self.assertEqual(names, ["D1.1", "D1.9"])

    def test_a_permutation_that_inverts_a_rule_is_a_move(self):
        """The case the review built: two words swapped inside one rule keep
        the multiset and invert the rule, so the comparison must report it and
        main() must exit 1 unless --allow-move names the entry."""
        inverted = GOOD.replace(
            "- rule: The first rule says one thing.",
            "- rule: The first says one rule thing.")
        self.assertEqual(self.changed(BULLETS, inverted), [])
        self.assertEqual(self.moved(BULLETS, inverted), ["D1.1"])

    def test_a_moved_sentence_is_reported_and_is_not_a_failure(self):
        moved = GOOD.replace(
            "- rule: The second rule says another thing.\n- rationale: the user asked for it.",
            "- rule: The second rule says another thing.\n"
            "- rationale: it. the user asked for",
        )
        self.assertEqual(self.changed(BULLETS, moved), [])
        self.assertEqual(self.moved(BULLETS, moved), ["D1.2"])

    def test_an_allowed_entry_is_excused(self):
        broken = GOOD.replace("says one thing", "says one other thing")
        self.assertEqual(self.changed(BULLETS, broken, allowed=["D1.1"]), [])

    def test_the_rationale_marker_is_not_a_difference(self):
        """The old form writes `Rationale: ` in the sentence, the new form as
        the field label, and the comparison must not read that as a change.
        The field keeping the marker compares equal as well."""
        kept = GOOD.replace("- rationale: the user", "- rationale: Rationale: the user")
        self.assertEqual(self.changed(BULLETS, kept), [])
        dropped = BULLETS.replace(" Rationale: the user asked for it.", "")
        self.assertEqual([name for name, _ in self.changed(dropped, GOOD)], ["D1.2"])

    def test_a_word_that_only_moved_between_two_entries_is_a_failure(self):
        broken = GOOD.replace("- rule: The first rule says one thing.",
                              "- rule: The first rule says one.")
        broken = broken.replace("- rule: The second rule says another thing.",
                                "- rule: The second rule says another thing thing.")
        self.assertEqual(sorted(name for name, _ in self.changed(BULLETS, broken)),
                         ["D1.1", "D1.2"])


class Main(unittest.TestCase):
    """The command line, over files in a temporary directory."""

    def run_tool(self, argv):
        out = io.StringIO()
        with redirect_stdout(out):
            status = check_decisions.main(argv)
        return status, out.getvalue()

    def setUp(self):
        import tempfile

        self.dir = tempfile.mkdtemp()

    def test_a_good_log_exits_zero(self):
        status, out = self.run_tool([write(self.dir, "good.md", GOOD)])
        self.assertEqual(status, 0)
        self.assertEqual(out, "")

    def test_a_bad_log_exits_one(self):
        text = GOOD.replace("- rule: The first rule says one thing.\n", "")
        status, out = self.run_tool([write(self.dir, "bad.md", text)])
        self.assertEqual(status, 1)
        self.assertIn("has no rule field", out)

    def test_the_expected_count_is_checked(self):
        status, out = self.run_tool([write(self.dir, "good.md", GOOD), "--expect", "4"])
        self.assertEqual(status, 1)
        self.assertIn("3 entries, expected 4", out)

    def test_a_comparison_that_holds_exits_zero(self):
        status, out = self.run_tool([
            write(self.dir, "new.md", GOOD),
            "--against", write(self.dir, "old.md", BULLETS),
        ])
        self.assertEqual(status, 0)
        self.assertIn("entries compared: 3", out)
        self.assertIn("entries whose text changed: 0", out)

    def test_a_comparison_that_fails_exits_one(self):
        broken = GOOD.replace("says one thing", "says one other thing")
        status, out = self.run_tool([
            write(self.dir, "new.md", broken),
            "--against", write(self.dir, "old.md", BULLETS),
        ])
        self.assertEqual(status, 1)
        self.assertIn("D1.1: the text changed", out)

    def test_an_undeclared_move_exits_one(self):
        moved = GOOD.replace(
            "- rule: The first rule says one thing.",
            "- rule: The first says one rule thing.")
        status, out = self.run_tool([
            write(self.dir, "new.md", moved),
            "--against", write(self.dir, "old.md", BULLETS),
        ])
        self.assertEqual(status, 1)
        self.assertIn("D1.1: the words moved and no --allow-move names it", out)
        self.assertIn("undeclared: 1", out)

    def test_a_declared_move_exits_zero_and_is_printed(self):
        moved = GOOD.replace(
            "- rule: The first rule says one thing.",
            "- rule: The first says one rule thing.")
        status, out = self.run_tool([
            write(self.dir, "new.md", moved),
            "--against", write(self.dir, "old.md", BULLETS),
            "--allow-move", "D1.1",
        ])
        self.assertEqual(status, 0)
        self.assertIn("declared move D1.1", out)
        self.assertIn("undeclared: 0", out)

    def test_a_flag_that_names_no_entry_exits_one(self):
        for flag in ("--allow", "--allow-move"):
            with self.subTest(flag=flag):
                status, out = self.run_tool([
                    write(self.dir, "new.md", GOOD),
                    "--against", write(self.dir, "old.md", BULLETS),
                    flag, "D9.9",
                ])
                self.assertEqual(status, 1)
                self.assertIn("is not in both revisions", out)

    def test_an_allow_that_would_excuse_a_deleted_entry_exits_one(self):
        without = GOOD.replace("""### D1.1 The first rule
- owner: `one.md`.
- rule: The first rule says one thing.

""", "")
        status, out = self.run_tool([
            write(self.dir, "new.md", without),
            "--against", write(self.dir, "old.md", BULLETS),
            "--allow", "D1.1",
        ])
        self.assertEqual(status, 1)
        self.assertIn("--allow D1.1: the entry is not in both revisions", out)

    def test_an_allowed_entry_leaves_the_comparison_green(self):
        broken = GOOD.replace("says one thing", "says one other thing")
        status, out = self.run_tool([
            write(self.dir, "new.md", broken),
            "--against", write(self.dir, "old.md", BULLETS),
            "--allow", "D1.1",
        ])
        self.assertEqual(status, 0)
        self.assertIn("entries compared: 2", out)
        self.assertIn("excused D1.1: lost", out)


class RealLog(unittest.TestCase):
    """The decision log in the repository answers for its own shape."""

    def setUp(self):
        self.text = DECISIONS_PATH.read_text(encoding="utf-8")
        self.entries, _ = check_decisions.read_fielded(self.text)

    def test_the_lint_is_clean(self):
        self.assertEqual(check_decisions.lint(str(DECISIONS_PATH), self.text), [])

    def test_it_holds_a_hundred_and_forty_four_entries(self):
        self.assertEqual(len(self.entries), 144)

    def test_every_entry_carries_an_owner_and_a_rule(self):
        without = [name for name, fields in self.entries.items()
                   if not fields.get("owner", "").strip() or not fields.get("rule", "").strip()]
        self.assertEqual(without, [])

    def test_no_dated_note_stands_outside_a_history_field(self):
        """Every dated note stands in a history field and none in a rule.

        D3.14 carried `amended 2026-09-10 from the T-011 review` inside a
        sentence of its own rule until T-085, which removed the sentence: the
        rule it described, a span cast that adds mutability, no longer exists,
        and a history note whose subject is gone says nothing. So the list is
        empty, and a note written into a rule fails this test.

        The pattern is case-insensitive and takes any whitespace between the
        word and the date, because a rule wraps at 100 columns and such a note
        is often lowercase; a stricter pattern misses both and passes for the
        wrong reason.
        """
        dated = re.compile(r"(?:amended|note|decided)\s+20\d\d-\d\d-\d\d", re.IGNORECASE)
        loose = sorted(name for name, fields in self.entries.items()
                       if dated.search(fields.get("rule", "")) or dated.search(
                           fields.get("rationale", "")))
        self.assertEqual(loose, [])

    def test_the_guard_above_sees_a_note_the_wrap_splits_from_its_date(self):
        """The 100-column wrap may put the word and the date on two lines."""
        dated = re.compile(r"(?:amended|note|decided)\s+20\d\d-\d\d-\d\d", re.IGNORECASE)
        self.assertIsNotNone(dated.search("... unspellable. Amended\n  2026-09-10: spans"))
        self.assertIsNotNone(dated.search("(amended 2026-09-10 from the T-011 review"))

    def test_the_sections_of_the_log_are_the_sections_of_the_index(self):
        listed = re.findall(r"^- (D\d+) ", self.text, re.MULTILINE)
        headed = re.findall(r"^## (D\d+) ", self.text, re.MULTILINE)
        self.assertEqual(listed, headed)


if __name__ == "__main__":
    unittest.main()
