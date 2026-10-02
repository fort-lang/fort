#!/usr/bin/env python3
"""Test the fort TextMate grammar without a VS Code process.

The tests compare grammar tokens with the language lists. A small TextMate engine also checks
fixtures, declaration tables, and maintained fort sources.
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
import decisions  # noqa: E402

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
LSP_TESTS_DIR = ROOT / "test" / "lsp"
TTY_DIR = ROOT / "test" / "tty"
OWNERSHIP_APPROVED_DIR = ROOT / "test" / "ownership" / "approved"
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
    LSP_TESTS_DIR,
    TTY_DIR,
    OWNERSHIP_APPROVED_DIR,
)
# The number of files those directories hold. A floor cannot detect a directory
# that the scan stopped using. When a `.ft` file changes this count, use the
# number from the failure. bootstrap0/test/parser_recovery_test.c uses the same method.
CORPUS_FILES = 861
# The least number of `.ft` each of those directories holds. A directory grows,
# so its own test asserts a floor and CORPUS_FILES asserts the exact total.
# A floor of 1 only proves that the directory exists.
# Each floor stays near its measured count. The
# table has one entry for each directory of CORPUS_DIRS, and
# test_every_corpus_directory_is_checked_by_a_test holds the two against each
# other. Thus, an unchecked directory fails the test.
CORPUS_MINIMUMS = {
    LANG_RUN_DIR: 200,
    LANG_PROGRAMS_DIR: 20,
    STD_DIR: 8,
    FORT_SRC_DIR: 20,
    LSP_SRC_DIR: 5,
    FORT_TESTS_DIR: 80,
    LSP_TESTS_DIR: 20,
    TTY_DIR: 2,
    OWNERSHIP_APPROVED_DIR: 10,
}
# The `.ft` of the repository that are deliberately outside the corpus, each
# because it is meant to hold a lexical error.
# test/lang/fail holds rejected programs.
# test/highlight/scopes.ft holds fixture errors.
# editors/vscode/test/fixtures/lexical.ft supplies the extension diagnostic.
EXCLUDED_DIRS = (LANG_FAIL_DIR, FIXTURE_DIR, EDITOR_FIXTURE_DIR)
# Directories of the worktree that hold no source of the project. The build
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
    """Read keyword, reserved-word, and operator lists from the decision log."""
    spans = re.findall(r"`([^`]*)`", decisions.rule_of(text, "D2.4"), re.DOTALL)
    operators = re.search(r"`([^`]*)`", decisions.rule_of(text, "D2.10"), re.DOTALL)
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
# puts a group in front of each rule. So a rule that spells one of the three
# would count a group of another rule. The grammar spells none of them, and
# test_every_rule_of_the_grammar_can_be_combined holds that over all 57
# patterns.
RENUMBERING = re.compile(r"\\[1-9]|\(\?P=|\(\?\(")


def refuse_renumbering(pattern):
    """Refuse a rule pattern whose meaning changes inside the alternation."""
    if RENUMBERING.search(pattern):
        raise ValueError(f"{pattern} counts a group, so it cannot be combined")


class Scanner:
    """One search over a line for a whole rule list.

    The engine searched for each rule separately at each position. Each search
    scans to the line end, so one line cost `positions x rules x length`.
    One alternation answers the same question in one scan. Each rule has its
    own group. The regex engine finds the leftmost matching position. At that
    position, it takes the first alternative in written order. This engine
    already used that order: the end pattern first, then rule order. Thus, the
    end pattern is the first alternative.

    `lastindex` names the group that closed last. A rule's groups close inside
    its wrapper group. Thus, the last group is the wrapper. `by_group` turns
    it into the winning entry.

    The scanner applies the winner alone at the position the alternation found.
    `emit` reads captures by the winning rule's group numbers. Each rule pattern
    must have the same meaning inside the alternation and alone. A backreference,
    named backreference, or conditional group counts from its pattern start.
    `refuse_renumbering` rejects them because the alternation could select the
    wrong rule. The grammar uses none of them across 57 patterns.
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

    It is the oracle of ScannerTest and the full-engine comparison.
    The leftmost match wins. The end pattern and then rule order break a tie.
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

        The key is the pattern-list identity. The grammar keeps each list for
        as long as this engine keeps the grammar. No list is freed, so no id is
        reused. Each context builds its list once, not once for each position.
        This makes the alternation inexpensive.
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
    for line in decisions.rule_of(text, tag).split("\n"):
        match = re.match(r"\s*\|\s*`([^`]+)`\s*\|", line)
        if match:
            rows.append(match.group(1))
    return rows


def load_grammar():
    return json.loads(GRAMMAR_PATH.read_text(encoding="utf-8"))


class LexiconTest(unittest.TestCase):
    """Compare grammar tokens with the language token lists."""

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
    """Check each declaration marker in the language tables."""

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
    """Compare Scanner with one search for each rule.

    SlowScanner is that oracle. Each case names a search rule on a grammar of
    two or three patterns. The named rule is the witness. A matching corpus
    identifies only the class.
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
        """The alternation does not shift a rule's capture groups.

        emit reads each capture by its group number. Group 1 of the winner
        remains group 1.
        """
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
        """A rule with groups does not move the next rule.

        An entry's wrapper stands after the groups of the previous entry.
        """
        rules = [{"match": "(a)(b)"}, {"match": "z", "name": "z"}, {"match": "(c)"}]
        _, _, rule = self.scan(rules, None, "z")
        self.assertEqual(rule["name"], "z")

    def test_the_group_that_closed_last_is_the_one_that_wraps_the_rule(self):
        """search reads the winner from lastindex.

        lastindex names the group that closed last. A rule's groups close
        inside its wrapper, so the wrapper closes last.
        """
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

        The sample includes the fixture and each 25th corpus file.
        Thus, it covers rules that the fixture does not spell.
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

    One test checks one CORPUS_DIRS entry. Thus, each file gets one scope check.
    The count test counts paths and does not tokenize them.

    Scanner comparisons sample every 25th corpus file. Other tests use small fixtures.
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
        """Check the corpus of whole programs."""
        self.check_directory(LANG_PROGRAMS_DIR)

    def test_every_standard_library_module_spells_correctly(self):
        """Check standard library sources."""
        self.check_directory(STD_DIR)

    def test_every_compiler_source_in_fort_spells_correctly(self):
        """Check compiler sources and require the configured minimum count."""
        self.check_directory(FORT_SRC_DIR)

    def test_every_language_server_source_spells_correctly(self):
        """Check language server sources."""
        self.check_directory(LSP_SRC_DIR)

    def test_every_module_test_of_the_compiler_spells_correctly(self):
        """test/fort/**/*.ft: the tests of the self-hosted modules and their
        shared fixtures under support/, which rglob reaches."""
        paths = self.check_directory(FORT_TESTS_DIR)
        self.assertTrue(any(p.parent.name == "support" for p in paths), "support/ not walked")

    def test_every_module_test_of_the_language_server_spells_correctly(self):
        """test/lsp/*.ft: the tests of the language server's modules."""
        self.check_directory(LSP_TESTS_DIR)

    def test_every_terminal_program_spells_correctly(self):
        """Check the terminal test programs."""
        self.check_directory(TTY_DIR)

    def test_every_approved_ownership_example_spells_correctly(self):
        """Check the approved examples without selecting ownership analysis."""
        self.check_directory(OWNERSHIP_APPROVED_DIR)

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

        This test records directory checks and tokenizes nothing.
        It runs each test of this class with check_directory recording its
        directory instead of checking it. Thus, it reads what the run does,
        not what the source says. A renamed method, a skipped method, or a
        commented call leaves a directory unrecorded. A sibling that fails for
        its own reason also fails here. This is the cost of running them.
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

        A new `.ft` outside CORPUS_DIRS and EXCLUDED_DIRS fails here. Thus, an
        unknown corpus causes a red test instead of an unseen gap.
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
