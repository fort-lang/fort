#!/usr/bin/env python3
"""Lint the knowledge rules of notes/style.md and AGENTS.md (T-103).

A convention with a lint is a rule. A convention without one is a wish: the
count rule of AGENTS.md was written down three times and three tickets still
claimed a universal they had not measured, and T-108 shipped 15 false sentences
and 37 misplaced `///` marks past a check that reported zero.

This tool holds five rules, one for each convention the ticket names. Each is a
function that returns a list of `path:line: message` problems and a one-line
count, and `--rule NAME` runs one of them alone.

  budget    AGENTS.md is at most BUDGET_LIMIT lines (its own target is 150,
            AGENTS.md, "Self-Updating Context", last paragraph).
  citation  the citation shape of notes/style.md 1.4 over the sources 1.1
            governs, the punctuation a cut tag leaves, the `///` of 1.3 placed
            by position, and a decision tag that names an entry or a section of
            spec/decisions.md.
  decisions every entry of spec/decisions.md has an owner and a rule field
            (notes/style.md 4), read through tools/check_decisions.py so that
            one parser answers for both tools.
  history   no history note of spec/decisions.md reports a rule of the log in
            the present tense with no date on the claim (notes/style.md 4,
            T-109, T-121).
  paths     a document is cited by a path that exists and by its section, never
            by a line number (notes/style.md 4, T-105).

The `citation` rule carries the four wrong answers T-108 found in T-101's lint
and fixed: `U+D800` is not a citation of a decision D800, a `//` inside a string
literal is not a comment, a bare `// T-091` is not a citation, and a list may
not repeat a tag that a range of the same list already names.

`--root DIR` points the whole tool at another tree, which is how
test/knowledge_lint_test.py seeds a broken tree and watches each rule go red.

What this lint does not see, stated rather than left in a glob:

  * It asks which mark a comment takes and never that a comment exists, so the
    `///` rule is one-sided over most of the corpus: it can only refuse a mark
    in a `.c` and in a fort test program. The count line of `citation` prints
    both numbers, so a reader knows how much of a green report is an assertion.
  * A fort program that carries no harness directive reads as a module. The two
    are test/tty/print_then_wait.ft and test/tty/rt_print_then_wait.ft. The lint
    would then ask `///` of a comment standing on a top-level declaration of
    one of them; neither holds such a comment today.
  * A table of two rows inside a comment is reported as a tag cut out of prose,
    because a column is a column only when three lines running align at it. The
    rule reads that way round on purpose: two lines cut at one column are the
    residue a sweep leaves, T-101 made 11 of that shape, and a false positive
    is noise somebody silences while a false negative is a defect that ships.
    A third row, or a re-wrap, answers it. 0 such tables stand in the tree.
  * The `paths` rule reads a reference to a `.md` file under a directory of the
    repository. A path to a source file, a glob and a bare document name are
    outside it, as is a section number that names no section.
  * A ticket number is not checked against anything, because `.tickets/` is
    outside the repository. A decision tag is: it must name an entry or a
    section of spec/decisions.md.
  * The `history` rule reads a subject that names a rule, and a bare decision
    tag only at the head of a sentence, because inside one a tag is as often a
    relative clause. It reads a closed list of verbs, one dated note excuses
    the whole field, and it says nothing about the `rule` field or the
    `rationale` field. HISTORY_LIMITS below states each gap with the entry
    that stands in it, and it is counted by
    `test_every_limit_names_a_gap_and_says_what_stands_in_it`, so no number
    for a gap stands in this prose.
  * EXCLUDED below names the code it does not read at all.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_decisions  # noqa: E402

# ---- the corpus -------------------------------------------------------------

# The sources notes/style.md 1.1 governs: `src/**`, `std/**/*.ft` and the C and
# fort files of `test/` that are code.
SOURCE_GLOBS = (
    "src/**/*.c",
    "src/**/*.h",
    "src/**/*.ft",
    "std/*.ft",
    "std/mac/*.ft",
    "test/*.c",
    "test/*.h",
    "test/abi/*.c",
    "test/fort/**/*.ft",
    "test/fort_lint/*.ft",
    "test/highlight/*.ft",
    "test/lang/ffi/*.c",
    "test/tty/*.ft",
)

# The code this lint does not read, and why. A lint with a silent hole is worse
# than no lint, because the next reader trusts it: the globs above left 13 files
# out for no stated reason until a review counted them and the 48 problems they
# held (T-103, round 2). `test_every_code_file_is_read_or_excluded` fails on a
# file that is neither read nor under a prefix named here, and on a prefix here
# that names no file, so neither list rots in silence.
EXCLUDED = (
    ("test/lang/",
     "the language corpus of spec/toolchain.md 7: each file is a fixture of the "
     "harness, many are deliberately malformed, and the comments are directives. "
     "test/lang/ffi is read, because its C files are ordinary code the harness "
     "compiles and links"),
    ("editors/vscode/test/fixtures/",
     "the fixtures of the VS Code extension, whose conventions are in "
     "editors/README.md. The line and column of each diagnostic stands in the "
     "`*-document.json` beside the fixture, so a comment added above a "
     "declaration moves a number a test compares"),
)

# The documents the `paths` rule reads.
DOC_GLOBS = (
    "AGENTS.md",
    "notes/**/*.md",
    "spec/**/*.md",
    "editors/**/*.md",
)

AGENTS_FILE = "AGENTS.md"
DECISIONS_FILE = "spec/decisions.md"

# The line budget of AGENTS.md in force: the measured value, `wc -l AGENTS.md`.
# The limit carries no headroom on purpose, so a line added to AGENTS.md fails
# this rule and the ticket that wants it says what it moved out, which is the
# behaviour a budget exists to cause (T-103, round 2: a limit 10 lines above the
# file blocks a new section and nothing smaller). AGENTS.md states 150 as its
# target and says that number stands as a target and not as a rule until the
# user rules between the two readings ("Self-Updating Context", last paragraph),
# so the target stands here beside the limit and no rule reads it. Lower the
# limit when a ticket moves text out; raise it only with what moved in.
BUDGET_LIMIT = 252
BUDGET_TARGET = 150

# ---- the comment scanner ----------------------------------------------------

# The five harness directive markers of notes/style.md 1.1. A comment that opens
# with one of them is a directive and no rule here reads it.
DIRECTIVES = ("!", "|", "<", "@", "^")

# A section banner is navigation and not a contract (notes/style.md 1.3), so it
# keeps `//` even where it stands against a declaration.
BANNER = re.compile(r"^//+ -{4,}.*-{4,}$")

# A brace whose declaration text names a record and holds no parenthesis and no
# `=` opens a record body: a struct field takes `///` and a statement does not.
RECORD_HEAD = re.compile(r"\b(struct|union|enum)\b[^()=]*$")


class Comment:
    """One comment of one file, with the position that decides its mark."""

    def __init__(self, path, line, marker, body):
        self.path = path
        self.line = line
        self.marker = marker  # "//" or "///"
        self.body = body  # the text after the marker, one line
        self.column = 0  # where the `//` stands on the line
        self.trailing = False  # code stands before it on the line
        self.context = "file"  # "header", "file", "record" or "body"
        self.attached = False  # a declaration stands on the next line
        self.next_code = ""  # that line, stripped
        self.banner = False  # the block opens with a section banner


def split_comment(line):
    """Return (code before the comment, index of the `//`) or (line, None).

    The scan tracks a string and a character literal, because `test/*.c` holds
    fort programs as C strings and `test/fort/*_test.ft` holds `file:///a.ft`
    (T-108, the second wrong answer of T-101's lint).
    """
    quote = ""
    i = 0
    while i < len(line):
        ch = line[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = ""
        elif ch in ('"', "'"):
            quote = ch
        elif ch == "/" and line.startswith("//", i):
            return line[:i], i
        i += 1
    return line, None


def update_braces(stack, pending, code):
    """Push and pop the bracket stack over one line of code; return the new pending text.

    `pending` is the declaration text since the last `;`, `{` or `}`, which says
    what a brace opens: a record body, where a field takes `///`, or a block,
    where a statement takes `//`.

    A parenthesis is pushed too, so that a comment standing inside a signature
    that wraps over two lines is inside something and not at file scope. It is
    the position of a parameter, not of a declaration, and 1.3 gives it `//`.
    """
    quote = ""
    i = 0
    while i < len(code):
        ch = code[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = ""
            i += 1
            continue
        if ch in ('"', "'"):
            quote = ch
        elif ch == "{":
            kind = "record" if RECORD_HEAD.search(pending.strip()) else "block"
            stack.append(kind)
            pending = ""
        elif ch == "(":
            stack.append("paren")
            # the parenthesis stays in `pending`, because RECORD_HEAD reads it:
            # `void f(struct s* p) {` opens a block and `struct s {` a record
            pending += ch
        elif ch == ")":
            if stack:
                stack.pop()
            pending += ch
        elif ch == "}":
            if stack:
                stack.pop()
            pending = ""
        elif ch == ";":
            pending = ""
        else:
            pending += ch
        i += 1
    return pending


def context_of(stack):
    """Where a comment stands: outside every bracket, inside a record, or inside
    a block or a parameter list."""
    if not stack:
        return "file"
    return "record" if all(kind == "record" for kind in stack) else "body"


def comments_of(path, text):
    """Every comment of one file, in source order, with its context.

    A directive is dropped here, so no rule below has to know about it.
    """
    lines = text.split("\n")
    found = []
    stack = []
    pending = ""
    for number, line in enumerate(lines, start=1):
        code, index = split_comment(line)
        pending = update_braces(stack, pending, code)
        if index is None:
            continue
        rest = line[index:]
        marker = "///" if rest.startswith("///") else "//"
        body = rest[len(marker):]
        if body[:1] in DIRECTIVES and marker == "//":
            continue
        comment = Comment(path, number, marker, body)
        comment.column = index
        comment.trailing = code.strip() != ""
        comment.context = context_of(stack)
        found.append(comment)
    mark_blocks(found, lines)
    return found


def mark_blocks(found, lines):
    """Give each whole-line comment its block's header, banner and attachment.

    A block is a run of whole-line comments on consecutive lines at one column.
    The block that opens at line 1 is the header block of notes/style.md 1.2. A
    block attaches
    to the first line after it when that line holds code; a blank line between
    the two means the comment stands alone, which is what a section banner and a
    free note do.

    A block that carries on a trailing comment at the same column is part of
    that trailing comment and not a comment of its own: the second line of
    `AST_TYPE, // a: base type;` wraps the first and marks no declaration.
    """
    whole = [c for c in found if not c.trailing]
    i = 0
    while i < len(whole):
        j = i
        while (j + 1 < len(whole) and whole[j + 1].line == whole[j].line + 1
               and whole[j + 1].column == whole[j].column):
            j += 1
        block = whole[i:j + 1]
        if continues_trailing(block, lines):
            for comment in block:
                comment.trailing = True
            i = j + 1
            continue
        after = block[-1].line  # the 1-based line after the block is lines[after]
        next_code = lines[after].strip() if after < len(lines) else ""
        attached = next_code != "" and split_comment(lines[after])[0].strip() != ""
        header = block[0].line == 1
        banner = bool(BANNER.match(block[0].marker + block[0].body))
        for comment in block:
            comment.attached = attached
            comment.next_code = next_code
            comment.banner = banner
            if header:
                comment.context = "header"
        i = j + 1


def continues_trailing(block, lines):
    """Whether this block wraps the trailing comment of the line above it."""
    above = block[0].line - 2
    if above < 0:
        return False
    code, index = split_comment(lines[above])
    if index is None or code.strip() == "":
        return False
    return all(comment.column == index for comment in block)


# ---- the `///` of notes/style.md 1.3 ----------------------------------------

# A fort source that carries a harness directive is a test program: the 173
# programs of test/fort all open on `//! run` or `//! check`, and the 11 modules
# of test/fort/support carry none (T-103, Notes / Design).
FORT_DIRECTIVE = re.compile(r"^//[!|<@^]", re.MULTILINE)

# An import is not a declaration another module reads, so a comment above one is
# a note and keeps `//`. Every other top-level line of a fort module declares
# something, because D9.6 exports everything at module level.
FORT_NOT_DECLARATION = re.compile(r"^import\b")


def kind_of(path, text):
    """"c_header", "c_source", "fort_module" or "fort_program"."""
    if path.suffix == ".h":
        return "c_header"
    if path.suffix == ".c":
        return "c_source"
    return "fort_program" if FORT_DIRECTIVE.search(text) else "fort_module"


def expected_marker(kind, comment):
    """The mark notes/style.md 1.3 asks for at this position.

    A `.c` and a fort test program keep `//`: neither is a module interface. A
    trailing comment keeps `//` as well, which T-103 ruled and 1.3 now states,
    because a contract does not fit at the end of a line of code.
    """
    if comment.trailing or comment.context == "header" or comment.banner:
        return "//"
    if kind in ("c_source", "fort_program"):
        return "//"
    if not comment.attached or comment.context == "body":
        return "//"
    if kind == "c_header" and comment.next_code.startswith("#"):
        return "//"  # a preprocessor line is not a declaration
    if kind == "fort_module" and FORT_NOT_DECLARATION.match(comment.next_code):
        return "//"
    return "///"


def marker_problems(path, kind, comments):
    """Every comment whose mark disagrees with its position."""
    problems = []
    for comment in comments:
        want = expected_marker(kind, comment)
        if comment.marker == want:
            continue
        where = {
            "header": "a header block",
            "file": "a declaration" if want == "///" else "no declaration",
            "record": "a field",
            "body": "a function body",
        }[comment.context]
        if comment.trailing:
            where = "the end of a line of code"
        elif comment.banner:
            where = "a section banner"
        elif kind in ("c_source", "fort_program") and want == "//":
            where = "a .c file" if kind == "c_source" else "a test program"
        problems.append(
            f"{path}:{comment.line}: {comment.marker} on {where}; "
            f"notes/style.md 1.3 asks for {want}")
    return problems


# ---- the citation shape of notes/style.md 1.4 -------------------------------

# A tag never follows a word character or a `+`, so `U+D800` is a code point and
# not a citation of a decision D800 (T-108, the first wrong answer).
TAG = re.compile(r"(?<![\w+])(?:D\d+(?:\.\d+)?|T-\d{3})\b")
SECTION = r"D\d+"
POINT = r"D\d+\.\d+"
RANGE = r"(?:" + SECTION + r" to " + SECTION + r"|" + POINT + r" to " + POINT + r")"
ITEM = r"(?:" + RANGE + r"|D\d+(?:\.\d+)?|T-\d{3})"
CITE = re.compile(r"^(?: |)" + ITEM + r"(?:, " + ITEM + r")*(?::\ .{0,59}[^.])?$")
TICKET = re.compile(r"(?<![\w+])T-\d{3}\b")
DECISION_TAG = re.compile(r"(?<![\w+])(D\d+(?:\.\d+)?)\b")
RANGE_PAIR = re.compile(r"(?<![\w+])(D\d+(?:\.\d+)?) to (D\d+(?:\.\d+)?)")
CODE_SPAN = re.compile(r"`[^`]*`")

# The punctuation a cut tag leaves, which holds no tag and which the shape above
# therefore cannot see. T-101 made 11 of these in 9 files; its review fed the
# first version of the list ten cuts and nine passed.
MANGLED = (
    (re.compile(r"\(\s*[:,;]"), "a parenthesis that opens on a separator"),
    (re.compile(r"[;,]\s*\)"), "a parenthesis that closes on a separator"),
    (re.compile(r"(?<![\w)])\(\s*\)"), "an empty parenthesis"),
    (re.compile(r"(?<![\w>])\[\s*\]"), "an empty bracket"),
    (re.compile(r"\(\s*[\w.,-]+\s+\)"), "a one-word parenthesis that ends in a space"),
    (re.compile(r"\w\s+[.,;](?=\s|$)"), "a separator with nothing before it"),
    (re.compile(r"\s's\b"), "a possessive whose owner is gone"),
    (re.compile(r"\b(?:on|of|to|per|by|in|at|as|and|or)\s*\)"),
     "a connective left hanging"),
    (re.compile(r"\b(?:and|or|see)\.[a-z]"), "a space that went with the tag"),
)


def number_of(tag):
    parts = tag[1:].split(".")
    return int(parts[0]), int(parts[1]) if len(parts) > 1 else 0


def items_of(body):
    """The items of a citation, before the clause."""
    head = body.split(": ", 1)[0].strip()
    return [item.strip() for item in head.split(",")]


def citation_faults(body):
    """Why this comment body is not a citation of the shape of 1.4, or []."""
    faults = []
    # a malformed range is named before the shape, because the shape refuses it
    # too and says less about it
    for low, high in RANGE_PAIR.findall(body):
        if len(low.split(".")) != len(high.split(".")):
            faults.append(f"the range {low} to {high} mixes a section and a decision")
        elif number_of(low) >= number_of(high):
            faults.append(f"the range {low} to {high} does not ascend")
    if faults:
        return faults
    if not CITE.match(body.rstrip()):
        return ["outside the shape of notes/style.md 1.4"]
    if TICKET.search(body) and ": " not in body:
        # a ticket number points at a file outside the repository, so the clause
        # is what the reader gets
        faults.append("a ticket number with no clause")
    covered = set()
    for item in items_of(body):
        pair = RANGE_PAIR.match(item)
        if not pair:
            continue
        low, high = number_of(pair.group(1)), number_of(pair.group(2))
        for other in items_of(body):
            if RANGE_PAIR.match(other) or not other.startswith("D"):
                continue
            if low <= number_of(other) <= high:
                covered.add(other)
    for item in sorted(covered):
        faults.append(f"{item} is named twice: the range of the same list covers it")
    return faults


# A run of spaces inside a sentence is what a tag cut out of running prose
# leaves. It is also how a table inside a comment is aligned, so the run is a
# column rather than a cut when the same column is aligned in TABLE_ROWS lines
# running: `kind   what it holds` over `k_int  a number` over `k_str  the bytes`
# is a table, and `the rule  applies here` is a cut. Two lines are not enough,
# because a sweep that cuts one parenthesised tag out of two wrapped lines of
# one paragraph leaves two runs at one column, and T-101 made 11 of that shape
# in 9 files (T-103, rounds 2 and 3).
GAP = re.compile(r"\S(  +)\S")
TABLE_ROWS = 3


def masked(body):
    """The prose of a comment, with every code span replaced by filler."""
    return CODE_SPAN.sub(lambda m: "x" * len(m.group(0)), body)


def gap_columns(body):
    """The column each run of two or more spaces of this comment body ends at."""
    return {match.end(1) for match in GAP.finditer(masked(body))}


def aligned_run(comments, index, column):
    """How many comment lines running, through this one, align at `column`."""
    run = 1
    for step in (-1, 1):
        other = index + step
        while 0 <= other < len(comments):
            neighbour = comments[other]
            if abs(neighbour.line - comments[other - step].line) != 1:
                break
            if neighbour.column != comments[index].column:
                break
            if column not in gap_columns(neighbour.body):
                break
            run += 1
            other += step
    return run


def cut_gap(comments, index):
    """Whether the run of spaces in this comment is a cut and not a column."""
    for column in gap_columns(comments[index].body):
        if aligned_run(comments, index, column) < TABLE_ROWS:
            return True
    return False


def citation_problems(path, comments, known=None):
    """The shape, the punctuation and the split citations of one file.

    Returns (problems, tagged), where tagged counts the comment lines that hold
    a tag at all. `known` is the set of decision tags spec/decisions.md names; a
    tag outside it is reported, because 1.4 sends the reader who wants the rule
    to the log at that tag and a tag that names no entry sends them nowhere. A
    caller that passes None asks for no such check, which is what a unit test
    over one comment does.
    """
    problems = []
    tagged = 0
    cites = {}
    comments = list(comments)
    for index, comment in enumerate(comments):
        body = comment.body
        # an indented example line and a code span wrapped across two lines are
        # not prose, so the shapes below say nothing about them
        example = body.startswith("  ") or body.count("`") % 2 != 0
        if not example:
            prose = masked(body)
            if cut_gap(comments, index):
                problems.append(
                    f"{path}:{comment.line}: two spaces where a tag was cut out: "
                    f"{comment.marker}{body}")
            for pattern, why in MANGLED:
                if pattern.search(prose):
                    problems.append(
                        f"{path}:{comment.line}: {why}: {comment.marker}{body}")
        if not TAG.search(masked(body)):
            continue
        tagged += 1
        if known is not None:
            for tag in sorted(set(DECISION_TAG.findall(masked(body)))):
                if tag not in known:
                    problems.append(
                        f"{path}:{comment.line}: {tag} is in no entry and no section of "
                        f"{DECISIONS_FILE}: {comment.marker}{body}")
        faults = citation_faults(body)
        for fault in faults:
            problems.append(f"{path}:{comment.line}: {fault}: {comment.marker}{body}")
        if not faults and not comment.trailing:
            cites[comment.line] = (body, comment.column)
    for line, (body, column) in sorted(cites.items()):
        other, other_column = cites.get(line + 1, ("", -1))
        # two citation lines stand together only when each carries its own
        # clause and the two say different things; a trailing citation on the
        # declaration below is not the second line of this one
        if other_column == column and not (": " in body and ": " in other):
            problems.append(
                f"{path}:{line}: a citation split over two lines: //{body} | //{other}")
    return problems, tagged


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


# A `## Dn` heading of spec/decisions.md is a section and a legitimate citation,
# beside the `### Dn.m` entries: `D15` (not in v1) and `D16` (hazards) are cited
# as sections and hold no entry of their own (T-103, round 3).
DECISION_SECTION = re.compile(r"^## (D\d+) ", re.MULTILINE)


def known_tags(root):
    """Every decision tag spec/decisions.md names, as entries and as sections."""
    path = root / DECISIONS_FILE
    if not path.exists():
        return set()
    text = path.read_text(encoding="utf-8")
    entries, _ = check_decisions.read_fielded(text)
    return set(entries) | set(DECISION_SECTION.findall(text))


def rule_citation(root):
    """The comment rules of notes/style.md 1.3 and 1.4 over the sources 1.1 governs.

    The count line says how much of the corpus the `///` rule can speak about.
    The rule is one-sided over most of it: in a `.c` and in a fort test program
    it can only refuse a mark and never ask for one, so a reader of a green
    report should know at how many positions the rule required something
    (T-103, round 2).
    """
    problems = []
    tagged = 0
    total = 0
    required = 0
    files = 0
    known = known_tags(root)
    for path in collect(root, SOURCE_GLOBS):
        text = path.read_text(encoding="utf-8", errors="replace")
        name = path.relative_to(root)
        comments = comments_of(name, text)
        kind = kind_of(path, text)
        files += 1
        total += len(comments)
        required += sum(1 for c in comments if expected_marker(kind, c) == "///")
        found, count = citation_problems(name, comments, known)
        tagged += count
        problems.extend(found)
        problems.extend(marker_problems(name, kind, comments))
    return problems, f"citation: {files} files, {total} comments, {tagged} tagged; " \
                     f"/// required at {required} and refused at {total - required}"


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


# ---- the tense of a history note (notes/style.md 4) -------------------------

# What a history note may report and what it may not. A note is dated by its own
# `Amended YYYY-MM-DD` marker; the statement it reports is not. So a note that
# says what another statement *says*, in the present tense, makes a claim with no
# date on it, and the claim goes false the next time that statement moves. Three
# entries went wrong this way: D20.5's note said D19.5's rule "still says" a
# sentence that T-109 then replaced; D19.5's note called the stage1-against-stage2
# comparison one "which this decision does not ask for", which T-109's rewrite
# made the rule ask for; and T-109's own round-2 fix wrote `nothing compares two
# runs of one compiler`, a present-tense claim about the repository, into
# D19.5's rule field, which no rule here reads. What silences the rule is the tense of the
# *reported* verb, not the tense of the note around it: "the rule read X" is a
# report in the past and no verb of HISTORY_VERBS stands in it, while "until
# then the rule read: this decision requires two runs" is still red, because
# SENTENCE_BREAK cuts at the colon and the clause after it is present-tense. A
# note that reports an old rule quotes it, and unquoted() then hides the words
# (notes/style.md 4, T-109, T-121 round 2).

# A bare decision tag is a subject at the head of a sentence and nowhere else.
# At the head is where a note that reports another entry puts it: D17.4 carried
# `D3.10 decides function types and function pointers` and D20.5 at 8a1eb48
# carried `D19.5 requires the emitted text to be a function of the program`,
# and both stand there. Inside a sentence a tag is as often a relative clause
# that reports nothing, as in D9.1's `every test file D14.4 names NNN_name.ft`,
# which names a file and not a rule. The lookbehind reads "no character stands
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
# made D20.5 wrong, because it claims the statement has not moved since; "now"
# is the opposite and the house style of an amendment, and HISTORY_ANCHOR
# excuses the sentence it stands in.
HISTORY_ADVERB = (r"(?:\s+(?:still|now|also|already|only|therefore|again|then"
                  r"|no longer))?")

# Up to HISTORY_RUN words may stand between the subject and the verb, with an
# optional comma against the subject, so that `the rule of D19.5 says` and `this
# decision, after T-109, says` are reports and not prose. Without it a
# paraphrase of D20.5's real defect, built only from words both closed lists
# hold, went green (T-121, round 2). The bound is what stops the run bridging a
# relative clause on to a later verb: at 3 words `the rule of the other entry,
# which nobody has touched since, requires two runs` stays green. Widening the
# run from 0 to 3 words costs 0 problems over the tree and adds 1 sentence to
# the sweep that drops the dated-note escape, `the rule itself requires` in
# D19.5's own T-109 note.
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

# The repair notes/style.md 4 prescribes, which is to append a dated note rather
# than to edit the words of the old one: a sentence that carries a date and says
# what the note describes. T-109 wrote one into D19.5 and one into D20.5. A field
# that holds one excuses every report in it.
HISTORY_DATE = re.compile(r"20\d\d-\d\d-\d\d")
HISTORY_DATING = re.compile(r"as it stood|from th(?:is|at) date")

# A sentence ends at a full stop, a semicolon or a colon, all three of which the
# log uses to join clauses. The rule reads one sentence at a time, so a date in
# one sentence does not excuse the next.
SENTENCE_BREAK = re.compile(r"(?<=[.;:])\s")

# What this rule cannot see. Each line names the gap and the entry that stands in
# it, so a reader of a green report knows what the green covers. Every number was
# measured over the 49 history fields of the 144 entries on 2026-09-13, and each
# is pinned by a test of HistoryLimitTest in test/knowledge_lint_test.py rather
# than left in this prose, so a gap that closes fails a test.
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

    The body is the field with its line wrapping removed, because the sentence
    that repairs D19.5 wraps between "describe the log as" and "it stood on
    2026-09-12" and a line-at-a-time reader sees neither half (T-121).
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

    A note that quotes the sentence it replaces does the thing notes/style.md 4
    asks for, so the words inside the quotation marks are the old statement and
    not this note's claim about it.
    """
    return QUOTED.sub(lambda m: "x" * len(m.group(0)), text)


def field_is_dated(body):
    """Whether this history field carries the dated note notes/style.md 4 prescribes."""
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

    A tree with no decision log holds no history note, which is what `known_tags`
    answers for a missing log too, so `--root` over a partial tree reports
    nothing rather than raising (T-121, round 2).
    """
    path = root / DECISIONS_FILE
    if not path.exists():
        return [], "history: 0 history fields, 0 with a dated note"
    text = path.read_text(encoding="utf-8")
    problems, fields, dated = history_problems(text)
    return problems, f"history: {fields} history fields, {dated} with a dated note"


# A reference to a document of the repository: a path with a directory, which is
# what notes/style.md 4 asks a citation to carry after T-099 moved three sections
# out of AGENTS.md.
DOC_PATH = re.compile(r"(?<![\w/.-])((?:notes|spec|editors|tools|test|src|std)/"
                      r"[A-Za-z0-9_./-]*[A-Za-z0-9_-]\.md)(:\d+)?")

# The one place a line number may follow a document path: the sentence of
# notes/style.md 4 that refuses the shape has to quote it. The lint reports an
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
    for path in collect(root, DOC_GLOBS) + collect(root, SOURCE_GLOBS):
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
    "citation": rule_citation,
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
