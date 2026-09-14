#!/usr/bin/env python3
"""Lint the knowledge rules of notes/style.md and AGENTS.md (T-103).

A convention with a lint is a rule. A convention without one is a wish: the
count rule of AGENTS.md was written down three times and three tickets still
claimed a universal they had not measured, and T-108 shipped 15 false sentences
and 37 misplaced `///` marks past a check that reported zero.

This tool holds four rules, one for each convention the ticket names. Each is a
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
  * EXCLUDED below names the code it does not read at all.
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_decisions  # noqa: E402

# ---- the corpus -------------------------------------------------------------

# The sources notes/style.md 1.1 governs: `src/**`, `std/*.ft` and the C and
# fort files of `test/` that are code.
SOURCE_GLOBS = (
    "src/**/*.c",
    "src/**/*.h",
    "src/**/*.ft",
    "std/*.ft",
    "test/*.c",
    "test/*.h",
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
