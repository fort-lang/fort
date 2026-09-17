#!/usr/bin/env python3
"""Unit tests of the TextMate grammar in editors/vscode/syntaxes/fort.tmLanguage.json.

Two things are checked. First, drift: the keyword and reserved lists of D2.4
and the operator list of D2.10 are read out of the rule field of each entry
of spec/decisions.md, through tools/check_decisions.py, and compared
with the sets the grammar names, so an amendment to the decision log that the
grammar does not follow fails the build. The grammar writes keywords as one
word group, `\\b(?:a|b)\\b`, and operators as one alternation of literals,
`(?:\\+|-)`; a rule whose scope is in a keyword or operator family but which
matches neither shape is an error, so a rule cannot hide from the comparison.

Second, scopes: a small line-oriented TextMate engine (patterns, repository,
include, match, begin/end, captures) tokenizes the fixtures in test/highlight,
every marker declaration of the D5.3 and D17.2 tables, and every fort source
the project itself writes -- the language tests under test/lang/run, the whole
programs beside them, the standard library, the self-hosted compiler under
src/fort, its own tests under test/fort, the fixtures of tools/fort_lint.py and
the program test/tty_test.py drives on a pseudo terminal
-- and the tests assert the
scopes the grammar hands out. CORPUS_DIRS is that list and CORPUS_FILES its
size, and a partition test holds every other `.ft` in the repository against
EXCLUDED_DIRS, so a new directory of fort cannot be missed in silence.

Run with `python3 -m unittest highlight_test` from this directory. Standard
library only; Python 3.12.
"""

import dataclasses
import inspect
import json
import re
import sys
import unittest
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
ROOT = TEST_DIR.parent
sys.path.insert(0, str(ROOT / "tools"))

import check_decisions  # noqa: E402

GRAMMAR_PATH = ROOT / "editors" / "vscode" / "syntaxes" / "fort.tmLanguage.json"
DECISIONS_PATH = ROOT / "spec" / "decisions.md"
FIXTURE_DIR = TEST_DIR / "highlight"
LANG_RUN_DIR = ROOT / "test" / "lang" / "run"
LANG_PROGRAMS_DIR = ROOT / "test" / "lang" / "programs"
LANG_FAIL_DIR = ROOT / "test" / "lang" / "fail"
STD_DIR = ROOT / "std"
FORT_SRC_DIR = ROOT / "src" / "fort"
LSP_SRC_DIR = ROOT / "src" / "lsp"
FORT_TESTS_DIR = ROOT / "test" / "fort"
FORT_LINT_DIR = ROOT / "test" / "fort_lint"
TTY_DIR = ROOT / "test" / "tty"
DARWIN_TEST_DIR = ROOT / "test" / "darwin"
EDITOR_FIXTURE_DIR = ROOT / "editors" / "vscode" / "test" / "fixtures"

# Every directory of fort the grammar is held over. `src/fort` is the only one
# that may not exist yet, which the test that walks it says out loud.
CORPUS_DIRS = (
    LANG_RUN_DIR,
    LANG_PROGRAMS_DIR,
    STD_DIR,
    FORT_SRC_DIR,
    LSP_SRC_DIR,
    FORT_TESTS_DIR,
    FORT_LINT_DIR,
    TTY_DIR,
    DARWIN_TEST_DIR,
)
# The number of files those directories hold. It is an equality and not a floor
# because a floor cannot see a directory that stopped being walked: a ticket
# that adds or removes a `.ft` under CORPUS_DIRS reads the new number off the
# failure and writes it here, as it does for CORPUS_FILES in
# test/parser_recovery_test.c and FT_FILES in tools/diff_tokens.sh.
CORPUS_FILES = 699
# The least number of `.ft` each of those directories holds. A directory grows,
# so its own test asserts a floor and CORPUS_FILES asserts the exact total. A
# floor of 1 says only that the directory exists, so each one here is near the
# count of the day: 432, 22, 12, 23, 7, 184, 3 and 2 on 2026-09-14, `std` being
# one file smaller since T-132 deleted std/rt_float.ft. The
# table has one entry for each directory of CORPUS_DIRS, and
# test_every_corpus_directory_is_checked_by_a_test holds the two against each
# other, so a directory that no test checks is a red test (T-104).
CORPUS_MINIMUMS = {
    LANG_RUN_DIR: 200,
    LANG_PROGRAMS_DIR: 20,
    STD_DIR: 8,
    FORT_SRC_DIR: 20,
    LSP_SRC_DIR: 5,
    FORT_TESTS_DIR: 80,
    FORT_LINT_DIR: 3,
    TTY_DIR: 2,
    DARWIN_TEST_DIR: 2,
}
# The `.ft` of the repository that are deliberately outside the corpus, each
# because it is meant to hold a lexical error: test/lang/fail is the corpus of
# programs the compiler must reject, test/highlight/scopes.ft carries the
# errors the fixture test enumerates, and editors/vscode/test/fixtures holds
# lexical.ft, whose bad literal is the diagnostic the extension displays.
EXCLUDED_DIRS = (LANG_FAIL_DIR, FIXTURE_DIR, EDITOR_FIXTURE_DIR)
# Directories of the worktree that hold no source of the project: the build
# tree copies std/*.ft next to the runtime, and a dot directory is git's or a
# cache.
IGNORED_DIRS = ("build",)

# A keyword rule writes `\b(?:a|b)\b` or `\b(a|b)`; an operator or punctuation
# rule writes one alternation of literals, `(?:\+|-)`.
WORD_GROUP = re.compile(r"\\b\((?:\?:)?([A-Za-z_][A-Za-z0-9_|]*)\)")
LITERAL_GROUP = re.compile(r"^\((?:\?:)?(.*)\)$", re.DOTALL)
# The scopes whose rules must name their tokens in one of those two shapes.
NAMED_FAMILIES = (
    "keyword.",
    "storage.",
    "constant.language",
    "punctuation.",
    "invalid.illegal.reserved",
)


@dataclasses.dataclass(frozen=True)
class Lexicon:
    """The keywords, the reserved words and the operators of one source."""

    keywords: frozenset
    reserved: frozenset
    operators: frozenset


@dataclasses.dataclass(frozen=True)
class Token:
    """One span of one line and the scope stack the grammar gives it."""

    line: int
    start: int
    end: int
    text: str
    scopes: tuple


def decision_lexicon(text):
    """Read the D2.4 keyword and reserved lists and the D2.10 operator list."""
    spans = re.findall(r"`([^`]*)`", check_decisions.rule_of(text, "D2.4"), re.DOTALL)
    operators = re.search(r"`([^`]*)`", check_decisions.rule_of(text, "D2.10"), re.DOTALL)
    return Lexicon(
        keywords=frozenset(spans[0].split()),
        reserved=frozenset(spans[1].split()),
        operators=frozenset(operators.group(1).split()),
    )


def split_alternatives(pattern):
    """Split a regex alternation on the `|`s that are not backslash-escaped."""
    parts, current, escaped = [], [], False
    for ch in pattern:
        if escaped:
            current.append(ch)
            escaped = False
        elif ch == "\\":
            escaped = True
        elif ch == "|":
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
    parts.append("".join(current))
    return parts


def rule_tokens(rule):
    """Return the words and the operators one repository rule names."""
    pattern = rule.get("match") or rule.get("begin") or ""
    words = set()
    for group in WORD_GROUP.findall(pattern):
        words.update(group.split("|"))
    operators = set()
    literals = LITERAL_GROUP.match(pattern)
    if literals and not words:
        operators = set(split_alternatives(literals.group(1)))
    return words, operators


def grammar_lexicon(grammar):
    """Read the keywords, reserved words and operators the grammar names."""
    keywords, reserved, operators = set(), set(), set()
    for rule in grammar["repository"].values():
        words, ops = rule_tokens(rule)
        if "invalid.illegal.reserved" in rule.get("name", ""):
            reserved |= words
        else:
            keywords |= words
        operators |= ops
    return Lexicon(
        keywords=frozenset(keywords),
        reserved=frozenset(reserved),
        operators=frozenset(operators),
    )


def grammar_patterns(grammar):
    """Every `match`, `begin` and `end` pattern the grammar spells."""
    found = []
    stack = [grammar]
    while stack:
        node = stack.pop()
        if isinstance(node, dict):
            found += [node[key] for key in ("match", "begin", "end") if key in node]
            stack += list(node.values())
        elif isinstance(node, list):
            stack += node
    return found


def uncanonical_rules(grammar):
    """Return the rules whose scope promises tokens but whose shape names none."""
    offenders = []
    for key, rule in grammar["repository"].items():
        name = rule.get("name", "")
        if not name.startswith(NAMED_FAMILIES):
            continue
        words, operators = rule_tokens(rule)
        if not words and not operators:
            offenders.append(key)
    return sorted(offenders)


# The pattern list of a rule that declares none. The scanner of a rule list is
# cached on the identity of the list, so one object must answer for all of them.
NO_PATTERNS = ()


# A backreference, a named backreference and a conditional group count their
# group from the start of the pattern they stand in. The alternation of Scanner
# puts a group in front of each rule, so a rule that spells one of the three
# would count a group of another rule. The grammar spells none of them, and
# test_every_rule_of_the_grammar_can_be_combined holds that over all 57
# patterns (T-104).
RENUMBERING = re.compile(r"\\[1-9]|\(\?P=|\(\?\(")


def refuse_renumbering(pattern):
    """Refuse a rule pattern whose meaning changes inside the alternation."""
    if RENUMBERING.search(pattern):
        raise ValueError(f"{pattern} counts a group, so it cannot be combined")


class Scanner:
    """One search over a line for a whole rule list.

    The engine searched for each rule of the list separately at each position,
    and each search scans to the end of the line, so one line cost
    `positions x rules x length`. One alternation of the same rules, each
    inside a group of its own, answers the same question in one scan: the
    regex engine finds the leftmost position at which an alternative matches,
    and at that position it takes the first alternative in the order they are
    written. That is the rule this engine already had -- the end pattern
    first, then rule order -- so the end pattern is the first alternative
    (T-104).

    `lastindex` names the group that closed last. The groups of a rule close
    inside the group that wraps the rule, so that group is the wrapper, and
    `by_group` turns it into the entry that won.

    The winner is then applied on its own at the position the alternation
    found, because `emit` reads the captures of a rule by the group numbers of
    that rule. The precondition of the whole class is that a rule pattern
    means the same thing inside the alternation as it does alone. A
    backreference, a named backreference and a conditional group all count
    from the start of the pattern they stand in, so `refuse_renumbering`
    refuses them rather than let the alternation select the wrong rule. The
    grammar spells none of the three today, over 57 patterns.
    """

    def __init__(self, engine, rules, end):
        self.entries = []  # (kind, rule, the compiled pattern of the rule)
        self.by_group = {}  # the index of the group that wraps an entry -> it
        alternatives = [] if end is None else [("end", None, end)]
        for rule in rules:
            pattern = rule.get("match") or rule["begin"]
            alternatives.append(("begin" if "begin" in rule else "match", rule, pattern))
        parts = []
        index = 1
        for kind, rule, pattern in alternatives:
            refuse_renumbering(pattern)
            compiled = engine.compiled(pattern)
            self.entries.append((kind, rule, compiled))
            self.by_group[index] = self.entries[-1]
            parts.append(f"({pattern})")
            index += 1 + compiled.groups
        self.combined = engine.compiled("|".join(parts)) if parts else None

    def search(self, line, pos):
        """The match that starts first; the end pattern and then rule order win ties."""
        if self.combined is None:
            return None
        found = self.combined.search(line, pos)
        if found is None:
            return None
        kind, rule, compiled = self.by_group[found.lastindex]
        match = compiled.match(line, found.start())
        if match is None:
            raise ValueError(f"{compiled.pattern} matched inside the alternation and not alone")
        return kind, match, rule


class SlowScanner:
    """One search for each rule of the list, which is what Scanner replaces.

    It is the oracle of ScannerTest and of
    test_the_engine_answers_what_one_search_for_each_rule_answers. It states
    the rule the alternation must keep -- the leftmost match wins, the end
    pattern and then rule order win a tie -- in the shape that needs no
    reasoning about a combined pattern (T-104).
    """

    def __init__(self, engine, rules, end):
        self.engine = engine
        self.ruleset = rules
        self.end = end

    def search(self, line, pos):
        found = None
        if self.end is not None:
            match = self.engine.compiled(self.end).search(line, pos)
            if match is not None:
                found = ("end", match, None)
        for rule in self.ruleset:
            pattern = rule.get("match") or rule["begin"]
            match = self.engine.compiled(pattern).search(line, pos)
            if match is not None and (found is None or match.start() < found[1].start()):
                found = ("begin" if "begin" in rule else "match", match, rule)
        return found


class Engine:
    """A line-oriented TextMate engine: enough of it to tokenize fort."""

    def __init__(self, grammar):
        self.grammar = grammar
        self.repository = grammar["repository"]
        self.root_scope = grammar["scopeName"]
        self.cache = {}
        self.scanners = {}

    def rules(self, patterns):
        """Resolve the `include`s of a pattern list into repository rules."""
        return [
            self.repository[item["include"].lstrip("#")] if "include" in item else item
            for item in patterns
        ]

    def compiled(self, pattern):
        if pattern not in self.cache:
            self.cache[pattern] = re.compile(pattern)
        return self.cache[pattern]

    def scanner(self, patterns, end):
        """The scanner of one rule list and one end pattern, built once.

        The key is the identity of the pattern list, because the grammar holds
        every list for as long as this engine holds the grammar: no list is
        freed and no id is reused. A list is built once for each context and
        not once for each position, which is what makes the alternation cheap.
        """
        key = (id(patterns), end)
        if key not in self.scanners:
            self.scanners[key] = Scanner(self, self.rules(patterns), end)
        return self.scanners[key]

    def tokenize(self, text):
        """Return the tokens of a whole source text, sorted by position."""
        tokens = []
        stack = [((self.root_scope,), self.scanner(self.grammar["patterns"], None), None)]
        for lineno, line in enumerate(text.split("\n"), start=1):
            pos = 0
            while pos <= len(line):
                scopes, scanner, owner = stack[-1]
                found = scanner.search(line, pos)
                if found is None:
                    self.gap(tokens, lineno, line, pos, len(line), scopes)
                    break
                kind, match, rule = found
                self.gap(tokens, lineno, line, pos, match.start(), scopes)
                if kind == "end":
                    emit(tokens, lineno, match, None, owner.get("endCaptures"), scopes)
                    stack.pop()
                elif kind == "begin":
                    name = rule.get("name")
                    inner = scopes + ((name,) if name else ())
                    emit(tokens, lineno, match, None, rule.get("beginCaptures"), inner)
                    patterns = rule.get("patterns") or NO_PATTERNS
                    stack.append((inner, self.scanner(patterns, rule["end"]), rule))
                else:
                    emit(tokens, lineno, match, rule.get("name"), rule.get("captures"), scopes)
                pos = match.end() if match.end() > match.start() else match.start() + 1
        tokens.sort(key=lambda t: (t.line, t.start, t.start - t.end))
        return tokens

    def gap(self, tokens, lineno, line, start, end, scopes):
        """Text no rule matched carries the scopes of the enclosing context."""
        if end > start:
            tokens.append(Token(lineno, start, end, line[start:end], scopes))


class SlowEngine(Engine):
    """The engine with SlowScanner in place of Scanner: the oracle."""

    def scanner(self, patterns, end):
        key = (id(patterns), end)
        if key not in self.scanners:
            self.scanners[key] = SlowScanner(self, self.rules(patterns), end)
        return self.scanners[key]


def emit(tokens, lineno, match, name, captures, scopes):
    """Append the token of a whole match and the tokens of its captures."""
    base = scopes + ((name,) if name else ())
    if name is not None:
        tokens.append(Token(lineno, match.start(), match.end(), match.group(0), base))
    for key, capture in (captures or {}).items():
        index = int(key)
        if index > match.re.groups or match.group(index) is None:
            continue
        start, end = match.span(index)
        scope = capture.get("name")
        tokens.append(
            Token(lineno, start, end, match.group(index), base + ((scope,) if scope else ()))
        )


def by_line(tokens):
    """Group tokens by their line number."""
    lines = {}
    for token in tokens:
        lines.setdefault(token.line, []).append(token)
    return lines


def scoped(token):
    """True when a token carries a scope of its own, not just the root scope."""
    return len(token.scopes) > 1


def uncovered(text, tokens):
    """Return the (line, column, character) triples no scoped token covers."""
    covered = {}
    for token in tokens:
        if scoped(token):
            covered.setdefault(token.line, set()).update(range(token.start, token.end))
    missing = []
    for lineno, line in enumerate(text.split("\n"), start=1):
        for column, ch in enumerate(line):
            if not ch.isspace() and column not in covered.get(lineno, set()):
                missing.append((lineno, column, ch))
    return missing


def invalid_tokens(tokens):
    """Return the tokens the grammar paints as a lexical error."""
    return [t for t in tokens if any(s.startswith("invalid.") for s in t.scopes)]


def marker_declarations(text, tag):
    """Return the declarations in the first column of a decision's marker table."""
    rows = []
    for line in check_decisions.rule_of(text, tag).split("\n"):
        match = re.match(r"\s*\|\s*`([^`]+)`\s*\|", line)
        if match:
            rows.append(match.group(1))
    return rows


def load_grammar():
    return json.loads(GRAMMAR_PATH.read_text(encoding="utf-8"))


class LexiconTest(unittest.TestCase):
    """The grammar names exactly the tokens D2.4 and D2.10 list."""

    def setUp(self):
        self.grammar = load_grammar()
        self.decisions = decision_lexicon(DECISIONS_PATH.read_text(encoding="utf-8"))
        self.named = grammar_lexicon(self.grammar)

    def test_keywords_are_the_d24_keywords(self):
        self.assertEqual(self.named.keywords, self.decisions.keywords)

    def test_reserved_words_are_the_d24_reserved_words(self):
        self.assertEqual(self.named.reserved, self.decisions.reserved)

    def test_operators_are_the_d210_operators(self):
        self.assertEqual(self.named.operators, self.decisions.operators)

    def test_every_keyword_or_operator_rule_is_canonical(self):
        self.assertEqual(uncanonical_rules(self.grammar), [])

    def test_decisions_parse_as_expected(self):
        self.assertIn("noreturn", self.decisions.keywords)
        self.assertIn("yield", self.decisions.reserved)
        self.assertIn("+%=", self.decisions.operators)
        self.assertIn("@", self.decisions.operators)
        self.assertIn("...", self.decisions.operators)
        self.assertNotIn("", self.decisions.operators)

    def test_c_extern_tail_has_one_variadic_token(self):
        tokens = Engine(self.grammar).tokenize("extern fn c_var(i32 n, ...) i32;")
        tail = [token for token in tokens if token.text == "..."]
        self.assertEqual(len(tail), 1)
        self.assertIn("keyword.operator.variadic.fort", tail[0].scopes)


class DriftTest(unittest.TestCase):
    """A keyword or an operator added on one side and not the other is caught."""

    def setUp(self):
        self.grammar = load_grammar()
        self.named = grammar_lexicon(self.grammar)
        self.fixture = decision_lexicon(
            (FIXTURE_DIR / "drift_decisions.md").read_text(encoding="utf-8")
        )

    def test_keyword_added_to_the_decisions_is_missing_from_the_grammar(self):
        self.assertIn("effect", self.fixture.keywords)
        self.assertNotEqual(self.named.keywords, self.fixture.keywords)
        self.assertEqual(self.fixture.keywords - self.named.keywords, {"effect"})

    def test_operator_added_to_the_decisions_is_missing_from_the_grammar(self):
        self.assertIn("<->", self.fixture.operators)
        self.assertNotEqual(self.named.operators, self.fixture.operators)
        self.assertEqual(self.fixture.operators - self.named.operators, {"<->"})

    def test_keyword_dropped_from_the_grammar_is_caught(self):
        decisions = decision_lexicon(DECISIONS_PATH.read_text(encoding="utf-8"))
        rule = self.grammar["repository"]["keyword-control"]
        rule["match"] = rule["match"].replace("|while", "")
        self.assertEqual(decisions.keywords - grammar_lexicon(self.grammar).keywords, {"while"})

    def test_operator_dropped_from_the_grammar_is_caught(self):
        decisions = decision_lexicon(DECISIONS_PATH.read_text(encoding="utf-8"))
        rule = self.grammar["repository"]["operator-span"]
        rule["match"] = "(?:\u00a7)"
        self.assertEqual(decisions.operators - grammar_lexicon(self.grammar).operators, {"@"})


class ScopeFixtureTest(unittest.TestCase):
    """The `//^` lines of the fixture name the scope of each token above them."""

    def setUp(self):
        self.engine = Engine(load_grammar())

    def test_fixture_scopes(self):
        path = FIXTURE_DIR / "scopes.ft"
        text = path.read_text(encoding="utf-8")
        lines = by_line(self.engine.tokenize(text))
        target = 0
        checked = 0
        for lineno, line in enumerate(text.split("\n"), start=1):
            if not line.strip().startswith("//^"):
                if line.strip():
                    target = lineno
                continue
            fields = line.strip()[3:].split()
            self.assertEqual(len(fields) % 2, 0, f"{path.name}:{lineno}: odd field count")
            for want_text, want_scope in zip(fields[0::2], fields[1::2]):
                self.assertTrue(
                    self.find(lines.get(target, []), want_text, want_scope),
                    f"{path.name}:{target}: no {want_text!r} with scope {want_scope}",
                )
                checked += 1
        self.assertGreater(checked, 100)

    def find(self, tokens, want_text, want_scope):
        """True when some token of the line has that text and that scope."""
        return any(t.text == want_text and want_scope in t.scopes for t in tokens)

    def test_fixture_has_only_the_deliberate_errors(self):
        text = (FIXTURE_DIR / "scopes.ft").read_text(encoding="utf-8")
        tokens = invalid_tokens(self.engine.tokenize(text))
        self.assertEqual(
            sorted({t.text for t in tokens}),
            ["'\\q'", "/*", "09", "\\q", "oops;", "yield"],
        )


class MarkerTableTest(unittest.TestCase):
    """Every declaration of the D5.3 and D17.2 tables spells correctly."""

    def setUp(self):
        self.engine = Engine(load_grammar())
        self.decisions = DECISIONS_PATH.read_text(encoding="utf-8")

    def declarations(self):
        rows = marker_declarations(self.decisions, "D5.3")
        rows += marker_declarations(self.decisions, "D17.2")
        return rows

    def test_the_tables_were_found(self):
        rows = self.declarations()
        self.assertIn("node mut* mut p", rows)
        self.assertIn("u8 mut@ own buf", rows)
        self.assertGreater(len(rows), 20)

    def test_every_declaration_spells_correctly(self):
        for declaration in self.declarations():
            with self.subTest(declaration=declaration):
                tokens = self.engine.tokenize(declaration)
                self.assertEqual(invalid_tokens(tokens), [])
                self.assertEqual(uncovered(declaration, tokens), [])
                self.check_markers(declaration, tokens)

    def check_markers(self, declaration, tokens):
        """mut and own are modifiers, the base is a type and the name a variable."""
        for token in tokens:
            if token.text == "mut":
                self.assertIn("storage.modifier.mut.fort", token.scopes)
            if token.text == "own":
                self.assertIn("storage.modifier.own.fort", token.scopes)
        base = re.match(r"[A-Za-z_][A-Za-z0-9_]*", declaration).group(0)
        self.assertTrue(
            any(
                t.text == base
                and (
                    "storage.type.primitive.fort" in t.scopes or "entity.name.type.fort" in t.scopes
                )
                for t in tokens
            ),
            f"{declaration}: {base} is not a type",
        )
        name = declaration.split()[-1]
        self.assertTrue(
            any(t.text == name and "variable.other.fort" in t.scopes for t in tokens),
            f"{declaration}: {name} is not a variable",
        )


class ScannerTest(unittest.TestCase):
    """Scanner answers what one search for each rule answered (T-104).

    SlowScanner is that oracle. The cases below name each rule of the search
    on a grammar of two or three patterns, because a case that names the rule
    is the witness and a corpus that agrees is only the class.
    """

    def setUp(self):
        self.engine = Engine(load_grammar())

    def scan(self, rules, end, line, pos=0):
        fast = Scanner(self.engine, rules, end).search(line, pos)
        slow = SlowScanner(self.engine, rules, end).search(line, pos)
        if fast is None:
            self.assertIsNone(slow)
            return None
        self.assertEqual((fast[0], fast[1].span(), fast[2]), (slow[0], slow[1].span(), slow[2]))
        return fast

    def test_the_match_that_starts_first_wins(self):
        rules = [{"match": "b", "name": "b"}, {"match": "a", "name": "a"}]
        kind, match, rule = self.scan(rules, None, "xxab")
        self.assertEqual((kind, match.start(), rule["name"]), ("match", 2, "a"))

    def test_rule_order_wins_a_tie(self):
        rules = [{"match": "a+", "name": "first"}, {"match": "a", "name": "second"}]
        _, match, rule = self.scan(rules, None, "aa")
        self.assertEqual((rule["name"], match.group(0)), ("first", "aa"))

    def test_the_end_pattern_wins_a_tie_with_a_rule(self):
        kind, match, rule = self.scan([{"match": "x", "name": "x"}], "x", "ax")
        self.assertEqual((kind, rule, match.start()), ("end", None, 1))

    def test_a_rule_beats_an_end_pattern_that_starts_later(self):
        kind, match, rule = self.scan([{"match": "x", "name": "x"}], "y", "xy")
        self.assertEqual((kind, rule["name"], match.start()), ("match", "x", 0))

    def test_a_begin_rule_is_reported_as_a_begin(self):
        rules = [{"begin": '"', "end": '"', "name": "string"}]
        kind, match, rule = self.scan(rules, None, 'a"b')
        self.assertEqual((kind, match.start(), rule["name"]), ("begin", 1, "string"))

    def test_the_captures_keep_the_numbers_of_their_own_rule(self):
        """emit reads a capture by the group number of the rule, so the
        alternation may not shift it: group 1 of the winner is group 1."""
        rules = [{"match": "z"}, {"match": r"(a)(b)"}, {"match": r"(c)"}]
        _, match, _ = self.scan(rules, None, "qab")
        self.assertEqual((match.group(1), match.group(2), match.re.groups), ("a", "b", 2))

    def test_a_search_starts_at_the_position_it_is_given(self):
        _, match, _ = self.scan([{"match": "a", "name": "a"}], None, "aXa", 1)
        self.assertEqual(match.start(), 2)

    def test_a_rule_list_that_matches_nothing_answers_nothing(self):
        self.assertIsNone(self.scan([{"match": "q"}], None, "abc"))

    def test_an_empty_rule_list_answers_nothing(self):
        self.assertIsNone(self.scan([], None, "abc"))

    def test_a_rule_with_groups_does_not_shift_the_rule_after_it(self):
        """The wrapper of an entry stands after the groups of the entry before
        it, so a rule with groups may not move the rule that follows it."""
        rules = [{"match": "(a)(b)"}, {"match": "z", "name": "z"}, {"match": "(c)"}]
        _, _, rule = self.scan(rules, None, "z")
        self.assertEqual(rule["name"], "z")

    def test_the_group_that_closed_last_is_the_one_that_wraps_the_rule(self):
        """search reads the winner off lastindex, which names the group that
        closed last: the groups of a rule close inside the group that wraps
        it, so the wrapper is the one that closes last."""
        scanner = Scanner(self.engine, [{"match": "z"}, {"match": r"(a)(b)"}], None)
        self.assertEqual(sorted(scanner.by_group), [1, 2])
        self.assertEqual(scanner.combined.search("ab").lastindex, 2)

    def test_a_rule_that_counts_a_group_is_refused(self):
        """A backreference, a named backreference and a conditional group
        count from the start of their own pattern, and the alternation moves
        that start. Scanner refuses all three rather than pick the wrong
        rule."""
        for pattern in (r"(a)\1", r"(?P<x>a)(?P=x)", r"(a)?(?(1)b|c)"):
            with self.subTest(pattern=pattern):
                with self.assertRaisesRegex(ValueError, "counts a group"):
                    Scanner(self.engine, [{"match": pattern}], None)

    def test_every_rule_of_the_grammar_can_be_combined(self):
        """The precondition above, over every pattern of the grammar."""
        patterns = grammar_patterns(self.engine.grammar)
        self.assertEqual(len(patterns), 59)
        for pattern in patterns:
            with self.subTest(pattern=pattern):
                refuse_renumbering(pattern)

    def test_a_pattern_that_matches_only_inside_the_alternation_is_named(self):
        """search applies the winner alone at the position the alternation
        found. That match cannot fail while the precondition holds, so the
        error names the pattern rather than let tokenize meet a None."""
        scanner = Scanner(self.engine, [{"match": "ab"}], None)
        kind, rule, _ = scanner.entries[0]
        scanner.by_group[1] = (kind, rule, self.engine.compiled("zz"))
        with self.assertRaisesRegex(ValueError, "zz matched inside the alternation"):
            scanner.search("ab", 0)

    def test_the_scanner_of_one_rule_list_is_built_once(self):
        """The alternation is compiled for each context and not for each
        position, which is what makes it cheaper than the search it replaces."""
        patterns = self.engine.grammar["patterns"]
        first = self.engine.scanner(patterns, None)
        self.assertIs(self.engine.scanner(patterns, None), first)
        self.assertIsNot(self.engine.scanner(patterns, '"'), first)

    def test_the_engine_answers_what_one_search_for_each_rule_answers(self):
        """The whole engine over real fort, against the oracle.

        The sample is the fixture and every 25th file of the corpus walk, so a
        rule the fixture does not spell is still met. T-104 held the two
        engines against all 685 files by hand and their token dumps were
        byte-identical; this test keeps a sample of that in the gate.
        """
        walked = sorted({path for d in CORPUS_DIRS for path in d.rglob("*.ft")})
        paths = [FIXTURE_DIR / "scopes.ft"] + walked[::25]
        self.assertGreaterEqual(len(paths), 20)
        slow = SlowEngine(load_grammar())
        for path in paths:
            with self.subTest(path=str(path)):
                text = path.read_text(encoding="utf-8")
                self.assertEqual(self.engine.tokenize(text), slow.tokenize(text))


class CorpusTest(unittest.TestCase):
    """Real fort is covered by the grammar and holds no lexical error.

    One test checks one directory of CORPUS_DIRS, so each of the 687 files is
    tokenized once for its scope check. The test that counts the walk checked
    every file of it a second time until T-104, which took a run of this
    module to 1396 calls of Engine.tokenize over 7575844 bytes; it counts now
    and tokenizes nothing.

    A run makes 775 calls over the corpus: the 687 scope checks, 58 of
    ScannerTest.test_the_engine_answers_what_one_search_for_each_rule_answers,
    which tokenizes the fixture and every 25th file of the walk once with each
    of the two engines, and 30 of the fixture and the marker tables. So 28
    corpus files are tokenized three times, and that is the oracle and not a
    second scope check.
    """

    def setUp(self):
        self.engine = Engine(load_grammar())

    def sources(self, directory):
        return sorted(directory.rglob("*.ft"))

    def check_directory(self, directory):
        """Check every `.ft` of one directory of CORPUS_DIRS, against its floor."""
        paths = self.sources(directory)
        self.assertGreaterEqual(len(paths), CORPUS_MINIMUMS[directory], f"{directory} shrank")
        self.check(paths)
        return paths

    def test_the_language_corpus_holds_the_lexical_tests(self):
        """The lexical tests are the hardest fort the grammar meets, and
        test_every_language_test_spells_correctly checks them with the rest of
        test/lang/run. This test holds their floor and tokenizes nothing."""
        self.assertGreaterEqual(len(self.sources(LANG_RUN_DIR / "lexical")), 4)

    def test_every_language_test_spells_correctly(self):
        self.check_directory(LANG_RUN_DIR)

    def test_every_whole_program_spells_correctly(self):
        """test/lang/programs/*.ft, the corpus of whole programs (T-079)."""
        self.check_directory(LANG_PROGRAMS_DIR)

    def test_every_standard_library_module_spells_correctly(self):
        """std/*.ft is real fort the grammar must cover too (T-076)."""
        self.check_directory(STD_DIR)

    def test_every_compiler_source_in_fort_spells_correctly(self):
        """src/fort/*.ft, the self-hosted compiler.

        The directory did not exist before the first ported module landed, and
        rglob over a missing directory yields nothing without error, so this
        test skipped out loud until Phase B created it. The floor of
        CORPUS_MINIMUMS says the same thing now and needs no branch (T-104).
        """
        self.check_directory(FORT_SRC_DIR)

    def test_every_language_server_source_spells_correctly(self):
        """src/lsp/*.ft, the language server (T-063).

        It stood in CORPUS_DIRS with no test of its own until T-104, so the
        second walk of test_the_corpus_is_the_size_it_says_it_is was the only
        thing that tokenized it.
        """
        self.check_directory(LSP_SRC_DIR)

    def test_every_module_test_of_the_compiler_spells_correctly(self):
        """test/fort/**/*.ft: the tests of the self-hosted modules and their
        shared fixtures under support/, which rglob reaches (T-079)."""
        paths = self.check_directory(FORT_TESTS_DIR)
        self.assertTrue(any(p.parent.name == "support" for p in paths), "support/ not walked")

    def test_every_fort_lint_fixture_spells_correctly(self):
        """test/fort_lint/*.ft is wrong semantically and clean lexically, which
        is the stress this test wants: bad_names.ft violates every rule of D1.4
        and broken.ft fails the checker, yet both must tokenize (T-079)."""
        self.check_directory(FORT_LINT_DIR)

    def test_every_terminal_program_spells_correctly(self):
        """test/tty/*.ft, the two programs test/tty_test.py drives on a pseudo
        terminal. They were in the same position as src/lsp before T-104."""
        self.check_directory(TTY_DIR)

    def test_every_darwin_program_spells_correctly(self):
        """Test the fort programs that the darwin network gate uses."""
        self.check_directory(DARWIN_TEST_DIR)

    def test_the_corpus_is_the_size_it_says_it_is(self):
        """The count of files walked, so a glob that stopped matching is seen.

        The walk holds no duplicate, which is what makes each file reach
        exactly one of the tests above. This test tokenizes nothing.
        """
        walked = [path for directory in CORPUS_DIRS for path in self.sources(directory)]
        self.assertEqual(len(walked), len(set(walked)))
        self.assertEqual(len(walked), CORPUS_FILES)

    def test_every_corpus_directory_is_checked_by_a_test(self):
        """A directory of CORPUS_DIRS that no test checks is caught here.

        The walk of test_the_corpus_is_the_size_it_says_it_is checked every
        file, so it covered a new directory whatever else it did, and it cost
        one tokenization of the whole corpus (T-104). This test takes that
        duty and tokenizes nothing. It runs each test of this class with
        check_directory recording its directory rather than checking it, so it
        reads what the run does and not what the source says: a method renamed
        out of the suite, a method the class skips and a call commented out
        each leave a directory unrecorded. A sibling that fails for its own
        reason fails here as well, which is the price of running them.
        """
        checked = []

        def record(directory):
            checked.append(directory)
            return self.sources(directory)

        for name in unittest.defaultTestLoader.getTestCaseNames(CorpusTest):
            if name == self._testMethodName:
                continue
            case = CorpusTest(name)
            case.setUp()
            case.check_directory = record
            try:
                getattr(case, name)()
            except unittest.SkipTest:
                pass
        self.assertEqual(sorted(checked, key=str), sorted(CORPUS_DIRS, key=str))

    def test_every_fort_source_in_the_repository_is_walked_or_excluded(self):
        """No directory of fort can be missed in silence.

        A new `.ft` outside CORPUS_DIRS and EXCLUDED_DIRS fails here, so a
        corpus this test does not know about is a red test rather than a hole
        nobody sees -- which is what test/fort was for two tickets.
        """
        stray = []
        for path in sorted(ROOT.rglob("*.ft")):
            parts = path.relative_to(ROOT).parts
            if any(part.startswith(".") or part in IGNORED_DIRS for part in parts):
                continue
            if any(path.is_relative_to(d) for d in CORPUS_DIRS + EXCLUDED_DIRS):
                continue
            stray.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(stray, [], "fort in no corpus of highlight_test.py")

    def check(self, paths):
        for path in paths:
            with self.subTest(path=str(path.relative_to(ROOT))):
                text = path.read_text(encoding="utf-8")
                tokens = self.engine.tokenize(text)
                bad = invalid_tokens(tokens)
                self.assertEqual(
                    [(t.line, t.text) for t in bad], [], f"{path}: painted as an error"
                )
                self.assertEqual(uncovered(text, tokens), [], f"{path}: not covered")


if __name__ == "__main__":
    unittest.main()
