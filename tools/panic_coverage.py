#!/usr/bin/env python3
"""Hold each panic site of the fort modules against the panic tests beside it.

A site is one `panic(...)` call in `src/fort/*.ft`, `src/lsp/*.ft` or `std/**/*.ft`. A panic ends
the program (D11.4), so each test of a site is a program of its own. The tests of `src/fort` are
`test/fort/*_panic_test.ft`, and the tests of `src/lsp` are `test/lsp/*_panic_test.ft`. The tests
of `std` are the `test/lang` programs that abort. Such a program answers a `std` site only.

A test answers a site when one of its `//! stderr:` texts holds the whole text of the site and
is part of the line `panic: <text>`. The match uses the whole text, so a fragment of a text
answers no site. A site whose argument is not one string literal has no text to match. A
declaration directly above that site names its tests:

    // panic-coverage: tests: fir_verify_v01 gen_fir_v9

The name `<stem>` is the file `<stem>_panic_test.ft` of the corpus of the site. A declaration
also says that no caller can reach a site:

    // panic-coverage: unreachable: <reason>

The tool counts a declared site as answered. `test/panic_gaps.txt` lists the sites that have no
test today, one `<path> "<text>"` on each line. The tool fails for each of these problems:

- a site with no test, no declaration and no line in the list;
- a test of `test/fort` or `test/lsp` that answers no site, or more than one;
- two sites with one text, because one test cannot tell them apart;
- a declaration that stands above no site, or that names a test that does not exist;
- a declaration that stands above a line with more than one site;
- a declaration with a keyword other than `unreachable` or `tests`, or with no value;
- two `unreachable` declarations on one site, or `unreachable` and `tests` on one site;
- a `tests` declaration on a site with a literal text, or on a site in `std`;
- a site declared unreachable that a test answers;
- a line of the list that names no site, a tested site or a declared site;
- a line of the list that names a site a second time, or that is not `<path> "<text>"`.

The tool does not refuse a new line in the list. Review holds that the list only gets shorter,
as it does for `test/lang/xfail.txt`.

Usage: panic_coverage.py [--root DIR] [--gaps FILE]. The report goes to stdout and each problem
goes to stderr. The exit status is 0 for no problem, 1 for a problem and 2 for a bad argument.
"""

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys

# Each source root, the glob of its modules and the corpus of its panic tests.
SOURCE_ROOTS = (
    ("src/fort", "*.ft", "test/fort"),
    ("src/lsp", "*.ft", "test/lsp"),
    ("std", "**/*.ft", None),
)
# The corpora whose `*_panic_test.ft` files must each answer exactly one site.
PANIC_CORPORA = ("test/fort", "test/lsp")
PANIC_TEST_SUFFIX = "_panic_test.ft"
# The language corpus. Its programs also panic in their own code, so it has no orphan test.
LANG_CORPUS = "test/lang"
STD_ROOT = "std"
GAPS_FILE = "test/panic_gaps.txt"
MARK = "// panic-coverage:"
PANIC_PREFIX = "panic: "

CALL_RE = re.compile(r"(?<![\w.])panic\s*\(")
FN_BEFORE_RE = re.compile(r"\bfn\s+$")
DIRECTIVE_RE = re.compile(r"^//! ([a-z][a-z-]*)(?::(.*))?$")
HEADER_MARKERS = ("//!", "//<", "//|")
GAP_RE = re.compile(r'^(\S+)\s+"((?:[^"\\]|\\.)*)"\s*(?:#.*)?$')


@dataclass
class Site:
    """One panic call and what answers it."""

    path: str
    line: int
    corpus: str
    text: str = None  # the decoded literal, or None for any other argument
    unreachable: str = None  # the reason of an unreachable declaration
    named: list = field(default_factory=list)  # the stems of a tests declaration
    tests: list = field(default_factory=list)  # the paths of the tests that answer it
    listed: bool = False

    def where(self):
        return "%s:%d" % (self.path, self.line)


@dataclass
class Test:
    """One panic test and the texts of its stderr directives."""

    path: str
    corpus: str
    texts: list
    sites: list = field(default_factory=list)


class Problems:
    """The problems of one run, in the order the tool finds them."""

    def __init__(self):
        self.items = []

    def add(self, where, message):
        self.items.append("%s: %s" % (where, message))


def code_of(line):
    """The line without its `//` comment. A string or char literal keeps its bytes."""
    masked = masked_literals(line)
    cut = masked.find("//")
    return line if cut < 0 else line[:cut]


def masked_literals(line):
    """The line with each byte inside a string or char literal replaced by a space."""
    out = list(line)
    quote = None
    i = 0
    while i < len(line):
        c = line[i]
        if quote is not None:
            if c == quote:
                quote = None
            else:
                out[i] = " "
                if c == "\\" and i + 1 < len(line):
                    out[i + 1] = " "
                    i += 1
        elif c in "\"'":
            quote = c
        i += 1
    return "".join(out)


def read_literal(code, start):
    """The decoded literal that starts at the quote `code[start]`, and the index after it.

    Returns (None, start) for a literal that the line does not close. Only `\\"` and `\\\\`
    are decoded; any other escape keeps its two bytes, so its site matches no test text.
    """
    out = []
    i = start + 1
    while i < len(code):
        c = code[i]
        if c == "\\" and i + 1 < len(code):
            nxt = code[i + 1]
            out.append(nxt if nxt in "\"\\" else c + nxt)
            i += 2
            continue
        if c == '"':
            return "".join(out), i + 1
        out.append(c)
        i += 1
    return None, start


def call_texts(code):
    """For each panic call on a code line: its decoded literal, or None for another argument."""
    found = []
    masked = masked_literals(code)
    for m in CALL_RE.finditer(masked):
        if FN_BEFORE_RE.search(masked[: m.start()]):
            continue  # `fn panic(` declares the runtime entry and calls nothing
        rest = m.end()
        while rest < len(code) and code[rest] in " \t":
            rest += 1
        text = None
        if rest < len(code) and code[rest] == '"':
            literal, after = read_literal(code, rest)
            tail = code[after:].lstrip() if literal is not None else ""
            if tail.startswith(")"):
                text = literal
        found.append(text)
    return found


def parse_declaration(body, where, problems):
    """(kind, value) of the text after the mark, or None for a malformed declaration."""
    kind, colon, value = body.partition(":")
    kind = kind.strip()
    value = value.strip()
    if not colon or kind not in ("unreachable", "tests"):
        problems.add(where, "a declaration is 'unreachable: <reason>' or 'tests: <stem> ...'")
        return None
    if not value:
        problems.add(where, "the '%s' declaration is empty" % kind)
        return None
    if kind == "tests":
        return kind, value.split()
    return kind, value


def scan_module(root, rel, corpus, problems):
    """The sites of one module. A declaration is the run of comment lines above its site."""
    lines = (root / rel).read_text(encoding="utf-8").split("\n")
    sites = []
    used_marks = set()
    for index, line in enumerate(lines):
        texts = call_texts(code_of(line))
        if not texts:
            continue
        declarations = []
        above = index - 1
        while above >= 0 and lines[above].lstrip().startswith("//"):
            stripped = lines[above].strip()
            if stripped.startswith(MARK):
                used_marks.add(above)
                where = "%s:%d" % (rel, above + 1)
                parsed = parse_declaration(stripped[len(MARK):], where, problems)
                if parsed is not None:
                    declarations.append((where, parsed))
            above -= 1
        line_sites = [Site(rel, index + 1, corpus, text) for text in texts]
        sites.extend(line_sites)
        if not declarations:
            continue
        if len(line_sites) > 1:
            problems.add(declarations[0][0], "the declaration stands above more than one site")
            continue
        apply_declarations(line_sites[0], declarations, problems)
    for index, line in enumerate(lines):
        if line.strip().startswith(MARK) and index not in used_marks:
            problems.add("%s:%d" % (rel, index + 1), "the declaration stands above no panic site")
    return sites


def apply_declarations(site, declarations, problems):
    """Record the declarations of one site and check that they agree with it."""
    kinds = [parsed[0] for _, parsed in declarations]
    where = declarations[0][0]
    if "unreachable" in kinds and (kinds.count("unreachable") > 1 or "tests" in kinds):
        problems.add(where, "a site takes one unreachable declaration and nothing else")
        return
    if "unreachable" in kinds:
        site.unreachable = declarations[0][1][1]
        return
    if site.text is not None:
        problems.add(where, "the site has a literal text, so its tests answer it by that text")
        return
    if site.corpus is None:
        problems.add(where, "the tests of %s are no panic tests, so no declaration can name them"
                     % site.path.split("/")[0])
        return
    for _, (_, stems) in reversed(declarations):
        site.named.extend(stems)


def directives(path):
    """(name, value) of each directive in the header of a test file."""
    out = []
    for line in path.read_text(encoding="utf-8").split("\n"):
        if line[:3] not in HEADER_MARKERS:
            break
        m = DIRECTIVE_RE.match(line)
        if m:
            out.append((m.group(1), (m.group(2) or "").strip()))
    return out


def stderr_texts(found):
    """The texts of the `stderr` and `stderr-<os>` directives."""
    return [v for n, v in found if (n == "stderr" or n.startswith("stderr-")) and v]


def aborts(found):
    """Whether a header says the program ends with SIGABRT, on some OS."""
    for name, value in found:
        if name == "abort" or name.startswith("abort-"):
            return True
        if (name == "signal" or name.startswith("signal-")) and value == "ABRT":
            return True
    return False


def read_tests(root):
    """The panic tests of the two module corpora and the aborting programs of the language."""
    tests = []
    for corpus in PANIC_CORPORA:
        for path in sorted((root / corpus).glob("*" + PANIC_TEST_SUFFIX)):
            tests.append(Test(path.relative_to(root).as_posix(), corpus,
                              stderr_texts(directives(path))))
    for path in sorted((root / LANG_CORPUS).rglob("*.ft")):
        found = directives(path)
        if aborts(found):
            tests.append(Test(path.relative_to(root).as_posix(), LANG_CORPUS,
                              stderr_texts(found)))
    return tests


def answers(expected, text):
    """Whether a stderr text answers the site text: it holds all of it and adds no other word."""
    return text in expected and expected in PANIC_PREFIX + text


def read_gaps(path, problems):
    """The (module path, text) pairs of the list of untested sites, with the line of each."""
    gaps = {}
    if not path.is_file():
        return gaps
    for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        where = "%s:%d" % (GAPS_FILE, number)
        m = GAP_RE.match(stripped)
        if not m:
            problems.add(where, 'a line of the list is <path> "<text>"')
            continue
        literal, _ = read_literal('"' + m.group(2) + '"', 0)
        key = (m.group(1), literal)
        if key in gaps:
            problems.add(where, "the list names this site twice")
            continue
        gaps[key] = where
    return gaps


def check(root, gaps_path):
    """Scan the repository and return (sites, tests, problems)."""
    problems = Problems()
    sites = []
    for source, pattern, corpus in SOURCE_ROOTS:
        for path in sorted((root / source).glob(pattern)):
            sites.extend(scan_module(root, path.relative_to(root).as_posix(), corpus, problems))
    by_text = {}
    for site in sites:
        if site.text is None:
            continue
        if site.text in by_text:
            problems.add(site.where(), 'the text "%s" is also the text of %s'
                         % (site.text, by_text[site.text].where()))
            continue
        by_text[site.text] = site

    tests = read_tests(root)
    by_path = {t.path: t for t in tests}
    for site in sites:
        for stem in site.named:
            path = "%s/%s%s" % (site.corpus, stem, PANIC_TEST_SUFFIX)
            if path not in by_path:
                problems.add(site.where(), "the declaration names %s, which does not exist" % path)
                continue
            by_path[path].sites.append(site)
    for test in tests:
        for text, site in by_text.items():
            if test.corpus == LANG_CORPUS and not site.path.startswith(STD_ROOT + "/"):
                continue  # a language program tests the language and std, not the compiler
            if any(answers(expected, text) for expected in test.texts):
                test.sites.append(site)
        if test.corpus == LANG_CORPUS:
            test.sites = test.sites if len(test.sites) == 1 else []
        elif not test.sites:
            problems.add(test.path, "the test answers no panic site")
        elif len(test.sites) > 1:
            problems.add(test.path, "the test answers %d sites: %s"
                         % (len(test.sites), ", ".join(s.where() for s in test.sites)))
        for site in test.sites:
            site.tests.append(test.path)

    gaps = read_gaps(gaps_path, problems)
    for site in sites:
        if site.unreachable is not None and site.tests:
            problems.add(site.where(), "the site is declared unreachable, but %s answers it"
                         % site.tests[0])
        key = (site.path, site.text)
        if site.text is not None and key in gaps:
            where = gaps.pop(key)
            if site.tests:
                problems.add(where, "%s answers this site, so remove the line" % site.tests[0])
            elif site.unreachable is not None:
                problems.add(where, "the site is declared unreachable, so remove the line")
            else:
                site.listed = True
            continue
        if site.tests or site.unreachable is not None or site.listed:
            continue
        if site.text is None:
            problems.add(site.where(), "the argument is not one string literal, so the site "
                         "needs a declaration")
        else:
            problems.add(site.where(), 'the site "%s" has no test, no declaration and no line '
                         "in %s" % (site.text, GAPS_FILE))
    for (path, text), where in gaps.items():
        problems.add(where, 'the list names %s "%s", which is no site' % (path, text))
    return sites, tests, problems


def report(sites, tests, out):
    """Write the count of each module and each site with no test."""
    modules = {}
    for site in sites:
        modules.setdefault(site.path, []).append(site)
    totals = [0, 0, 0, 0]
    for path, own in modules.items():
        tested = sum(1 for s in own if s.tests)
        unreachable = sum(1 for s in own if s.unreachable is not None and not s.tests)
        untested = len(own) - tested - unreachable
        for i, n in enumerate((len(own), tested, unreachable, untested)):
            totals[i] += n
        out.write("%s: %d sites, %d tested, %d unreachable, %d untested\n"
                  % (path, len(own), tested, unreachable, untested))
        for s in own:
            if not s.tests and s.unreachable is None:
                text = '"%s"' % s.text if s.text is not None else "(no literal text)"
                state = "listed" if s.listed else "NOT LISTED"
                out.write("  untested %s %s (%s)\n" % (s.where(), text, state))
    for corpus in PANIC_CORPORA:
        own = [t for t in tests if t.corpus == corpus]
        orphans = [t for t in own if len(t.sites) != 1]
        out.write("%s: %d panic tests, %d with no single site\n" % (corpus, len(own), len(orphans)))
        for t in orphans:
            out.write("  no single site %s\n" % t.path)
    lang = [t for t in tests if t.corpus == LANG_CORPUS and t.sites]
    out.write("%s: %d aborting programs answer a site\n" % (LANG_CORPUS, len(lang)))
    out.write("total: %d sites, %d tested, %d unreachable, %d untested\n" % tuple(totals))


def main(argv=None, out=sys.stdout, err=sys.stderr):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="repository root (default: the parent of tools/)")
    parser.add_argument("--gaps", type=Path, default=None,
                        help="the list of untested sites (default: <root>/%s)" % GAPS_FILE)
    args = parser.parse_args(argv)
    if not (args.root / "src/fort").is_dir():
        err.write("panic_coverage: %s holds no src/fort\n" % args.root)
        return 2
    gaps_path = args.gaps if args.gaps is not None else args.root / GAPS_FILE
    sites, tests, problems = check(args.root, gaps_path)
    report(sites, tests, out)
    out.flush()
    for item in problems.items:
        err.write("panic_coverage: error: %s\n" % item)
    if problems.items:
        err.write("panic_coverage: %d problems\n" % len(problems.items))
        return 1
    out.write("panic_coverage: no problems\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
