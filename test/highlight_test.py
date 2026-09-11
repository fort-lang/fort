#!/usr/bin/env python3
"""Unit tests of the TextMate grammar in editors/vscode/syntaxes/fort.tmLanguage.json.

Two things are checked. First, drift: the keyword and reserved lists of D2.4
and the operator list of D2.10 are read out of notes/decisions.md and compared
with the sets the grammar names, so an amendment to the decision log that the
grammar does not follow fails the build. The grammar writes keywords as one
word group, `\\b(?:a|b)\\b`, and operators as one alternation of literals,
`(?:\\+|-)`; a rule whose scope is in a keyword or operator family but which
matches neither shape is an error, so a rule cannot hide from the comparison.

Second, scopes: a small line-oriented TextMate engine (patterns, repository,
include, match, begin/end, captures) tokenizes the fixtures in test/highlight,
every marker declaration of the D5.3 and D17.2 tables, every language test
under test/lang/run and every module of the standard library, and the tests
assert the scopes the grammar hands out.

Run with `python3 -m unittest highlight_test` from this directory. Standard
library only; Python 3.12.
"""

import dataclasses
import json
import re
import unittest
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
ROOT = TEST_DIR.parent
GRAMMAR_PATH = ROOT / "editors" / "vscode" / "syntaxes" / "fort.tmLanguage.json"
DECISIONS_PATH = ROOT / "notes" / "decisions.md"
FIXTURE_DIR = TEST_DIR / "highlight"
LANG_RUN_DIR = ROOT / "test" / "lang" / "run"
STD_DIR = ROOT / "std"
FORT_SRC_DIR = ROOT / "src" / "fort"

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


def decision_bullet(text, tag):
    """Return the text of the `- **Dn.m**` bullet named by tag."""
    start = text.index(f"- **{tag}**")
    following = re.compile(r"^- \*\*D", re.MULTILINE).search(text, start + 1)
    return text[start : following.start() if following else len(text)]


def decision_lexicon(text):
    """Read the D2.4 keyword and reserved lists and the D2.10 operator list."""
    spans = re.findall(r"`([^`]*)`", decision_bullet(text, "D2.4"), re.DOTALL)
    operators = re.search(r"`([^`]*)`", decision_bullet(text, "D2.10"), re.DOTALL)
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


class Engine:
    """A line-oriented TextMate engine: enough of it to tokenize fort."""

    def __init__(self, grammar):
        self.grammar = grammar
        self.repository = grammar["repository"]
        self.root_scope = grammar["scopeName"]
        self.cache = {}

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

    def tokenize(self, text):
        """Return the tokens of a whole source text, sorted by position."""
        tokens = []
        stack = [((self.root_scope,), self.grammar["patterns"], None, None)]
        for lineno, line in enumerate(text.split("\n"), start=1):
            pos = 0
            while pos <= len(line):
                scopes, patterns, end, owner = stack[-1]
                found = self.leftmost(line, pos, patterns, end)
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
                    stack.append((inner, rule.get("patterns", []), rule["end"], rule))
                else:
                    emit(tokens, lineno, match, rule.get("name"), rule.get("captures"), scopes)
                pos = match.end() if match.end() > match.start() else match.start() + 1
        tokens.sort(key=lambda t: (t.line, t.start, t.start - t.end))
        return tokens

    def leftmost(self, line, pos, patterns, end):
        """The match that starts first; the end pattern and then rule order win ties."""
        found = None
        if end is not None:
            match = self.compiled(end).search(line, pos)
            if match is not None:
                found = ("end", match, None)
        for rule in self.rules(patterns):
            pattern = rule.get("match") or rule["begin"]
            match = self.compiled(pattern).search(line, pos)
            if match is not None and (found is None or match.start() < found[1].start()):
                found = ("begin" if "begin" in rule else "match", match, rule)
        return found

    def gap(self, tokens, lineno, line, start, end, scopes):
        """Text no rule matched carries the scopes of the enclosing context."""
        if end > start:
            tokens.append(Token(lineno, start, end, line[start:end], scopes))


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
    for line in decision_bullet(text, tag).split("\n"):
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
        self.assertNotIn("", self.decisions.operators)


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


class CorpusTest(unittest.TestCase):
    """Real fort is covered by the grammar and holds no lexical error."""

    def setUp(self):
        self.engine = Engine(load_grammar())

    def sources(self, directory):
        return sorted(directory.rglob("*.ft"))

    def test_the_lexical_tests_spell_correctly(self):
        paths = self.sources(LANG_RUN_DIR / "lexical")
        self.assertGreaterEqual(len(paths), 4)
        self.check(paths)

    def test_every_language_test_spells_correctly(self):
        self.check(self.sources(LANG_RUN_DIR))

    def test_every_standard_library_module_spells_correctly(self):
        """std/*.ft is real fort the grammar must cover too (T-076)."""
        paths = self.sources(STD_DIR)
        self.assertGreaterEqual(len(paths), 8)
        self.check(paths)

    def test_every_compiler_source_in_fort_spells_correctly(self):
        """src/fort/*.ft, which Phase B fills.

        The directory does not exist before the first ported module lands, and
        rglob over a missing directory yields nothing without error, so the
        test would pass over zero files and say so to nobody. It skips out
        loud until the directory exists and asserts a file once it does.
        """
        if not FORT_SRC_DIR.is_dir():
            self.skipTest("src/fort does not exist yet (Phase B creates it)")
        paths = self.sources(FORT_SRC_DIR)
        self.assertGreaterEqual(len(paths), 1, "src/fort exists but holds no .ft")
        self.check(paths)

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
