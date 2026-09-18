#!/usr/bin/env python3
"""Lint knowledge files for structure, stale claims, and broken paths.

The budget rule limits AGENTS.md. The decisions rule requires owner and rule fields.
The history rule rejects undated present-tense reports about other rules.
The paths rule requires existing document paths and rejects line-number citations.

Each rule returns `path:line: message` problems and one count line. Use `--rule NAME` to select
one rule. Use `--root DIR` to check another tree.

What this lint does not see:

  * The `paths` rule reads a reference to a `.md` file under a directory of the
    repository. A path to a source file, a glob and a bare document name are
    outside it, as is a section number that names no section.
  * The `history` rule reads a subject that names a rule, and a bare decision
    tag only at the head of a sentence, because inside one a tag is as often a
    relative clause. It reads a closed list of verbs, one dated note excuses
    the whole field, and it says nothing about the `rule` field or the
    `rationale` field. HISTORY_LIMITS below states each gap with the entry
    that stands in it, and it is counted by
    `test_every_limit_names_a_gap_and_says_what_stands_in_it`, so no number
    for a gap stands in this prose.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_decisions  # noqa: E402

# The documents the `paths` rule reads.
DOC_GLOBS = (
    "AGENTS.md",
    "notes/**/*.md",
    "spec/**/*.md",
    "editors/**/*.md",
)

AGENTS_FILE = "AGENTS.md"
DECISIONS_FILE = "spec/decisions.md"

# The line budget of AGENTS.md uses the measured `wc -l AGENTS.md` value.
# The limit has no headroom. A new AGENTS.md line fails this rule.
# BUDGET_TARGET is display-only. Lower the limit when text moves out.
# Raise the limit only when approved text moves in.
BUDGET_LIMIT = 252
BUDGET_TARGET = 150

# ---- the rules --------------------------------------------------------------

def rule_budget(root):
    """AGENTS.md is at most BUDGET_LIMIT lines."""
    path = root / AGENTS_FILE
    text = path.read_text(encoding="utf-8")
    count = len(text.split("\n")) - (1 if text.endswith("\n") else 0)
    problems = []
    if count > BUDGET_LIMIT:
        problems.append(
            f"{AGENTS_FILE}:{BUDGET_LIMIT + 1}: {count} lines, over the budget of "
            f"{BUDGET_LIMIT}; the routing table of AGENTS.md says where a fact goes")
    return problems, f"budget: {AGENTS_FILE} is {count} lines of {BUDGET_LIMIT} " \
                     f"(target {BUDGET_TARGET})"


def rule_decisions(root):
    """Every entry of spec/decisions.md has an owner field and a rule field."""
    path = root / DECISIONS_FILE
    text = path.read_text(encoding="utf-8")
    entries, lines_at = check_decisions.read_fielded(text)
    problems = []
    for name in sorted(entries, key=check_decisions.key_of):
        for field in check_decisions.REQUIRED:
            if not entries[name].get(field, "").strip():
                problems.append(
                    f"{DECISIONS_FILE}:{lines_at[name]}: {name} has no {field} field")
    return problems, f"decisions: {len(entries)} entries, each with " \
                     f"{' and '.join(check_decisions.REQUIRED)}"


# ---- tense of a history note -----------------------------------------------

# An amendment date does not date a present-tense claim inside its note.
# Such a claim becomes stale when the reported rule changes.
# The reported verb controls the result. Quoted old text does not make a current claim.

# A bare decision tag is a subject at the head of a sentence and nowhere else.
# A note places a reported entry tag at the sentence head.
# Inside a sentence, a tag often belongs to a relative clause and reports nothing.
# The lookbehind reads "no character stands
# before this one", and it is the head of the *sentence* because
# history_problems matches one sentence at a time.
HISTORY_HEAD_TAG = r"(?<![\s\S])D\d+(?:\.\d+)?"

# The subject of a report: a phrase that names a rule of the log, or a bare tag
# at the head of a sentence.
HISTORY_SUBJECT = (r"(?:th(?:is|at|e) (?:decision|rule|rationale)|its rule"
                   r"|D\d+(?:\.\d+)?'s (?:rule|note|clause|sentence)"
                   r"|the (?:note|sentence|clause|entry) (?:above|below|here)"
                   r"|" + HISTORY_HEAD_TAG + r")")

# An adverb may stand between the subject and the verb. "still" is the one that
# can make a note stale because it claims that the statement did not move. "now"
# is the opposite and the house style of an amendment, and HISTORY_ANCHOR
# excuses the sentence it stands in.
HISTORY_ADVERB = (r"(?:\s+(?:still|now|also|already|only|therefore|again|then"
                  r"|no longer))?")

# Up to HISTORY_RUN words may stand between the subject and the verb, with an
# optional comma after the subject. The bound prevents a match from crossing
# a relative clause to a later verb.
HISTORY_RUN = 3
HISTORY_BETWEEN = HISTORY_ADVERB + r"(?:,?(?:\s+\S+){0," + str(HISTORY_RUN) + r"})?\s+"

# The verbs of saying, in the present tense. `read` and `said` are absent
# because they are past-tense forms and a clause built on one reports nothing
# that can go stale; that is a property of the reported clause and not of the
# note around it, which the paragraph above states. `is`, `has` and `stands` are
# absent too, because they report a property and not a statement, and every note
# holds one.
HISTORY_VERBS = ("says|states|reads|asks|requires|names|decides|lists|holds|carries"
                 "|cites|records|forbids|contradicts|means|describes|covers"
                 "|specifies|mentions|calls|allows|admits|bars")
HISTORY_NEGATED = (r"(?:does not|cannot|can not)\s+"
                   r"(?:ask|say|state|read|require|name|decide|list|hold|carry|cite"
                   r"|record|forbid|contradict|mean|describe|cover|specify|mention"
                   r"|call|allow|admit|bar)")
HISTORY_REPORT = re.compile(
    r"\b(" + HISTORY_SUBJECT + HISTORY_BETWEEN + r"(?:" + HISTORY_VERBS + r"|"
    + HISTORY_NEGATED + r"))\b", re.IGNORECASE)

# A claim the sentence dates itself: `now` says the amendment made it true, and
# a date or one of the two dating phrases pins it to a day.
HISTORY_ANCHOR = re.compile(r"\bnow\b|20\d\d-\d\d-\d\d|as it stood"
                            r"|from th(?:is|at) date")

# A dated repair adds a note instead of changing the old words.
# One dated repair excuses each report in its field.
HISTORY_DATE = re.compile(r"20\d\d-\d\d-\d\d")
HISTORY_DATING = re.compile(r"as it stood|from th(?:is|at) date")

# A sentence ends at a full stop, a semicolon or a colon, all three of which the
# log uses to join clauses. The rule reads one sentence at a time, so a date in
# one sentence does not excuse the next.
SENTENCE_BREAK = re.compile(r"(?<=[.;:])\s")

# Each row names one known gap and the text that demonstrates it.
# HistoryLimitTest verifies the row count and measured limits.
HISTORY_LIMITS = (
    ("a bare decision tag is a subject only at the head of a sentence",
     "the gap that refused a bare tag everywhere closed on 2026-09-13 (T-122), "
     "which dated D17.4's `D3.10 decides function types and function pointers` "
     "and then widened the subject. Over the log at 8a1eb48 the widened rule "
     "reports 6 sentences where the old one reported 4, the 2 it adds being "
     "that note of D17.4 and D20.5's `D19.5 requires the emitted text to be a "
     "function of the program`, which stood inside the motivating case. What "
     "is left is a tag inside a sentence: 1 sentence of the 49 fields stands "
     "in that gap, D9.1's `every test file D14.4 names NNN_name.ft`, and it is "
     "a relative clause, so reading it would be a false positive. The position "
     "is a proxy for the reading and the two questions are not the same. A "
     "report one subordinator from the head is missed as well: `the citation "
     "was wrong, because D3.10 decides function types and function pointers` "
     "reports D3.10 and is not read, while the same clause at the head of a "
     "sentence is"),
    ("a note that quotes nothing is invisible",
     "T-039's note said the stage1 comparison is one `which this decision does "
     "not ask for` and pointed at `the sentence above`. The rule catches the "
     "first half. No rule can catch the second, because nothing in the text "
     "says which sentence is meant (T-109's review)"),
    ("one dated note excuses the whole field, and not positionally",
     "the escape is the repair notes/style.md 4 prescribes, and it excuses "
     "every report of the field wherever it stands. 3 fields of the 49 carry a "
     "dated note and it excuses 9 reports, 2 of which stand after it. Those 2 "
     "are T-109's own amendment describing the rule it wrote, so excusing them "
     "is right, but it is right by accident of a coarse escape: a stale report "
     "appended below the dated note rides out the same way"),
    ("the rule field and the rationale field are not read",
     "a rule states a requirement in the present tense by design. The same "
     "signature over the 144 rule fields reports 2 sentences in 1 entry, both "
     "D19.5's rule describing its own scope and both correct, and over the 16 "
     "rationale fields it reports 0. Reading either would still not close the "
     "class: the third instance of the defect was T-109's `nothing compares "
     "two runs of one compiler`, written into D19.5's rule, and it is out of "
     "reach twice over, once for the field and once for the subject `nothing`, "
     "whose cost the limit below measures"),
    ("a subject or a verb outside the two lists is not a report",
     "both lists are closed, so `the rule bans X` reads as prose. A word added "
     "to either must be measured over the log again: reading `it`, `nothing`, "
     "`the text` and `the log` as subjects and `is`, `has`, `stands` and "
     "`compares` as verbs takes the sweep from 9 sentences in 3 fields to 31 "
     "in 9. The sentences it adds are of both kinds, `It is the observation "
     "point` (D14.1) being prose and `it states` (D17.4) a report whose "
     "subject is a pronoun, and no count of the two stands here, because which "
     "is which is a reading and no test holds a reading"),
    ("a sentence that dates itself is not read",
     "`Amended YYYY-MM-DD (T-nnn)` dates the sentence it stands in and `now` "
     "says the amendment made the rule read this way, so a report written "
     "inside such a sentence is excused and `the rule now says` is outside the "
     "rule even when the amendment changed no word. A stale report joined by a "
     "comma to a clause that holds `now` rides out on it. 7 sentences of the "
     "49 fields stand in this gap"),
    ("a run of at most HISTORY_RUN words joins a subject to a verb",
     "beyond that bound a report is not read: `the rule of the other entry, "
     "which nobody has touched since, requires two runs` is green at 3 words. "
     "The bound stops a long subject bridging a relative clause on to a later "
     "verb, and it stops nothing when the subject is one token, because the "
     "whole run is then free to reach another subject's verb: `D9.1 aside, the "
     "ticket names the file wrongly` is reported as `D9.1 aside, the ticket "
     "names`, whose subject is `the ticket`. T-122 opened that case by reading "
     "a bare tag as a subject, a tag being the only one-token subject either "
     "list holds. 0 of the 9 sentences the sweep without the escape reports "
     "stand in this gap"),
)


HISTORY_LABEL = re.compile(r"^- history: ?(.*)$")


def history_fields(text):
    """Every history field of the decision log, as (tag, body, line of each character).

    The body has no line wrapping. Character positions retain their source lines.
    """
    found = []
    tag, pieces = None, None
    for number, line in enumerate(text.split("\n"), start=1):
        head = check_decisions.ENTRY_HEAD.match(line)
        if head:
            tag, pieces = head.group(1), None
            continue
        if line.startswith("#"):
            tag, pieces = None, None
            continue
        if tag is None:
            continue
        label = HISTORY_LABEL.match(line)
        if label:
            pieces = [(number, label.group(1))]
            found.append((tag, pieces))
            continue
        if pieces is None:
            continue
        if check_decisions.FIELD.match(line):
            pieces = None
            continue
        pieces.append((number, line))
    return [(tag,) + joined_field(pieces) for tag, pieces in found]


def joined_field(pieces):
    """One field's words as one string, with the source line of each character."""
    body, at = [], []
    for number, piece in pieces:
        for word in piece.split():
            if body:
                body.append(" ")
                at.append(number)
            body.append(word)
            at.extend([number] * len(word))
    return "".join(body), at


def sentences_of(body):
    """Each sentence of a field, as (offset in the body, text)."""
    found = []
    start = 0
    for match in SENTENCE_BREAK.finditer(body):
        found.append((start, body[start:match.start()]))
        start = match.end()
    found.append((start, body[start:]))
    return found


QUOTED = re.compile(r"\"[^\"]*\"|`[^`]*`")


def unquoted(text):
    """The text with every quotation and code span replaced by filler of the same length.

    Quoted text is an old statement, not the current note's claim.
    """
    return QUOTED.sub(lambda m: "x" * len(m.group(0)), text)


def field_is_dated(body):
    """Return whether this history field contains a dated repair."""
    for _, sentence in sentences_of(body):
        text = unquoted(sentence)
        if HISTORY_DATE.search(text) and HISTORY_DATING.search(text):
            return True
    return False


def history_problems(text, path=DECISIONS_FILE):
    """Every history note that reports a rule of the log in the present tense.

    Returns (problems, fields, dated): the count of fields read and the count of
    them that carry a dated note, so a green report says how much of it is the
    escape rather than the rule.
    """
    problems, fields, dated = [], 0, 0
    for tag, body, at in history_fields(text):
        fields += 1
        if field_is_dated(body):
            dated += 1
            continue
        for start, sentence in sentences_of(body):
            clean = unquoted(sentence)
            if HISTORY_ANCHOR.search(clean):
                continue
            for match in HISTORY_REPORT.finditer(clean):
                line = at[start + match.start()]
                problems.append(
                    f"{path}:{line}: {tag} reports a rule of the log in the present "
                    f"tense: \"{match.group(1)}\"; write the past tense or append a "
                    f"dated note (notes/style.md 4)")
    return problems, fields, dated


def rule_history(root):
    """No history note reports a rule of the log in the present tense, undated.

    A tree with no decision log holds no history note. Thus, `--root` over a
    partial tree reports nothing instead of raising an error.
    """
    path = root / DECISIONS_FILE
    if not path.exists():
        return [], "history: 0 history fields, 0 with a dated note"
    text = path.read_text(encoding="utf-8")
    problems, fields, dated = history_problems(text)
    return problems, f"history: {fields} history fields, {dated} with a dated note"


# A document reference contains a repository directory and a Markdown filename.
DOC_PATH = re.compile(r"(?<![\w/.-])((?:notes|spec|editors|tools|test|src|std)/"
                      r"[A-Za-z0-9_./-]*[A-Za-z0-9_-]\.md)(:\d+)?")

# One line number can follow a document path because the rule quotes its rejected form.
# The lint reports an
# exemption that matches nothing, so a rewritten sentence does not leave a hole.
EXEMPT_LINE_NUMBERS = (
    ("notes/style.md", "`notes/style.md:158` is not",
     "the sentence that states the rule quotes the shape it refuses (T-105)"),
)


def rule_paths(root):
    """A document is cited by a path that exists, and never by a line number."""
    problems = []
    used = set()
    cites = 0
    for path in collect(root, DOC_GLOBS):
        name = str(path.relative_to(root))
        for number, line in enumerate(path.read_text(
                encoding="utf-8", errors="replace").split("\n"), start=1):
            for match in DOC_PATH.finditer(line):
                cites += 1
                cited, tail = match.group(1), match.group(2)
                if tail:
                    excused = [e for e in EXEMPT_LINE_NUMBERS
                               if e[0] == name and e[1] in line]
                    used.update(excused)
                    if not excused:
                        problems.append(
                            f"{name}:{number}: {cited}{tail} cites a line number; "
                            f"cite the section (notes/style.md 4)")
                if (root / cited).exists():
                    continue
                other = elsewhere(root, cited)
                if other:
                    problems.append(
                        f"{name}:{number}: {cited} does not exist; "
                        f"the file is {other} (notes/style.md 4)")
                else:
                    problems.append(f"{name}:{number}: {cited} does not exist")
    for exemption in EXEMPT_LINE_NUMBERS:
        if (root / exemption[0]).exists() and exemption not in used:
            problems.append(
                f"tools/knowledge_lint.py: the exemption {exemption[0]} "
                f"'{exemption[1]}' matches nothing; delete it")
    return problems, f"paths: {cites} references to a document of the repository"


DOC_DIRS = ("notes", "spec", "editors", "tools", "test", "src", "std")


def elsewhere(root, cited):
    """The same file name under another documentation directory, or ""."""
    for directory in DOC_DIRS:
        candidate = Path(directory) / Path(cited).name
        if str(candidate) != cited and (root / candidate).exists():
            return str(candidate)
    return ""


RULES = {
    "budget": rule_budget,
    "decisions": rule_decisions,
    "history": rule_history,
    "paths": rule_paths,
}


def collect(root, globs):
    """The sorted files under root that match any of the globs."""
    files = set()
    for pattern in globs:
        files.update(p for p in root.glob(pattern) if p.is_file())
    return sorted(files)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", maxsplit=1)[0])
    parser.add_argument("--root", type=Path, default=Path.cwd(),
                        help="the tree to read (default: the current directory)")
    parser.add_argument("--rule", action="append", default=[], choices=sorted(RULES),
                        help="run one rule alone (repeatable)")
    parser.add_argument("--quiet", action="store_true",
                        help="print the problems and no count")
    args = parser.parse_args(argv)

    failed = 0
    for name in args.rule or sorted(RULES):
        problems, count = RULES[name](args.root)
        for problem in problems:
            print(problem)
        if not args.quiet:
            print(count)
        failed += len(problems)
    if failed:
        print(f"{failed} knowledge-rule problem(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
