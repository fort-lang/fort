#!/usr/bin/env python3
"""Check the shape of spec/decisions.md, and hold two revisions of it together.

The decision log is the normative source, so the risk of an edit to it is a
changed meaning that no build and no test can see. This tool answers two
questions.

With one file it lints the shape (T-100): every entry is `### Dn.m Title` with
an `owner` field, a `rule` field, and an optional `rationale` and `history`
field in that order. It prints one line for each problem and exits 1.

With `--against OLD` it reads the older revision too, in this form or in the
`- **Dn.m**` bullet form that came before it, and compares the text of each
entry. The comparison is the acceptance criterion of T-100: the rule sentences
move between the fields unchanged. An entry whose words changed is a failure.
An entry whose words only moved is a failure too, unless the caller names it
with `--allow-move Dn.m`: a permutation keeps the words of an entry and can
still invert its rule, so "widening extends, narrowing truncates" and
"narrowing extends, widening truncates" are one multiset and two rules. Each
move a ticket makes on purpose is one flag, which is also the record of it.
`--allow Dn.m` excuses one entry from both reports, for an amendment the ticket
carries on purpose; the tool prints the difference it excused, so an excused
entry is still read and never silently dropped. Both flags fail when they name
an entry that is not in both revisions.

The one text the comparison drops is the bare marker `Rationale: `, which the
old form wrote in the sentence and the new form writes as the field label.
"""

import argparse
import re
import sys

ENTRY_HEAD = re.compile(r"^### (D\d+\.\d+) (\S.*)$")
BULLET = re.compile(r"^- \*\*(D\d+\.\d+)\*\* (.*)$")
FIELD = re.compile(r"^- ([a-z]+): ?(.*)$")
SECTION = re.compile(r"^## (D\d+) ")
RATIONALE_MARKER = "Rationale: "
FIELDS = ("owner", "rule", "rationale", "history")
REQUIRED = ("owner", "rule")
WIDTH = 100


def read_fielded(text):
    """Return {id: {field: text}} and {id: line} for the fielded form."""
    entries, lines_at = {}, {}
    current, field = None, None
    for number, line in enumerate(text.split("\n"), start=1):
        head = ENTRY_HEAD.match(line)
        if head:
            current, field = head.group(1), None
            entries[current] = {}
            lines_at[current] = number
            continue
        if line.startswith("#"):
            current, field = None, None
            continue
        if current is None:
            continue
        match = FIELD.match(line)
        if match:
            field = match.group(1)
            # A field written twice keeps both texts: the lint reports the
            # duplicate and the comparison must still see every word.
            entries[current].setdefault(field, []).append(match.group(2))
            continue
        if field is not None:
            entries[current][field].append(line)
    return {name: {f: "\n".join(v).rstrip() for f, v in fields.items()}
            for name, fields in entries.items()}, lines_at


def read_bullets(text):
    """Return {id: text} for the `- **Dn.m**` form that came before T-100."""
    entries, current = {}, None
    for line in text.split("\n"):
        match = BULLET.match(line)
        if match:
            current = match.group(1)
            entries[current] = [match.group(2)]
            continue
        if line.startswith("#") or (line.strip() and not line.startswith("  ")):
            current = None
            continue
        if current is not None:
            entries[current].append(line)
    return {name: "\n".join(body).rstrip() for name, body in entries.items()}


def read_any(text):
    """Return {id: whole text of the entry}, in either form."""
    if not re.search(r"^### D\d", text, re.MULTILINE):
        return read_bullets(text)
    fielded, _ = read_fielded(text)
    return {name: " ".join(fields.get(f, "") for f in FIELDS if f != "owner")
            for name, fields in fielded.items()}


def rule_of(text, tag):
    """The rule field of one entry, for a test that reads a rule out of the log."""
    entries, _ = read_fielded(text)
    if tag not in entries:
        raise KeyError(f"{tag} is not in the decision log")
    return entries[tag]["rule"]


def words(text):
    """The words of an entry, with the scaffolding of both forms removed."""
    text = " ".join(text.split()).replace(RATIONALE_MARKER, " ")
    return text.split()


def lint(path, text):
    """Report every problem with the shape of one fielded file."""
    problems = []
    entries, lines_at = read_fielded(text)
    seen = []
    section = None
    current, field, written = None, None, set()
    for number, line in enumerate(text.split("\n"), start=1):
        if ENTRY_HEAD.match(line) or line.startswith("#"):
            current, field, written = ENTRY_HEAD.match(line), None, set()
            current = current.group(1) if current else None
        elif current is not None:
            match = FIELD.match(line)
            if match:
                field = match.group(1)
                if field in written:
                    problems.append(
                        f"{path}:{number}: {current} writes the {field} field twice")
                written.add(field)
            elif field is None and line.strip():
                problems.append(
                    f"{path}:{number}: {current} has text that belongs to no field")
    section = None
    for number, line in enumerate(text.split("\n"), start=1):
        if len(line) > WIDTH:
            problems.append(f"{path}:{number}: line is {len(line)} columns, over {WIDTH}")
        match = SECTION.match(line)
        if match:
            section = match.group(1)
        head = ENTRY_HEAD.match(line)
        if head:
            seen.append((head.group(1), number))
            if section is None or not head.group(1).startswith(section + "."):
                problems.append(f"{path}:{number}: {head.group(1)} is not in section {section}")
    for name, number in seen:
        if seen.count((name, number)) > 1 or [n for n, _ in seen].count(name) > 1:
            problems.append(f"{path}:{number}: {name} appears twice")
    for name, fields in entries.items():
        number = lines_at[name]
        for required in REQUIRED:
            if not fields.get(required, "").strip():
                problems.append(f"{path}:{number}: {name} has no {required} field")
        unknown = [f for f in fields if f not in FIELDS]
        for field in unknown:
            problems.append(f"{path}:{number}: {name} has an unknown field '{field}'")
        order = [f for f in fields if f in FIELDS]
        if order != sorted(order, key=FIELDS.index):
            problems.append(f"{path}:{number}: {name} writes its fields out of order")
    return problems


def compare(old_text, new_text, allowed):
    """Report entries whose words changed, and entries whose words only moved."""
    old, new = read_any(old_text), read_any(new_text)
    changed, moved = [], []
    for name in sorted(set(old) | set(new), key=key_of):
        if name in allowed:
            continue
        if name not in old or name not in new:
            changed.append((name, "the entry is in one revision only"))
            continue
        before, after = words(old[name]), words(new[name])
        if sorted(before) != sorted(after):
            changed.append((name, first_difference(before, after)))
        elif before != after:
            moved.append(name)
    return changed, moved


def difference_of(old_text, new_text, name):
    """The word difference of one entry, for a name the caller excused."""
    old, new = read_any(old_text), read_any(new_text)
    if name not in old or name not in new:
        return "the entry is in one revision only"
    before, after = words(old[name]), words(new[name])
    if before == after:
        return "no change"
    if sorted(before) == sorted(after):
        return "the words only moved"
    return first_difference(before, after)


def names_of(text):
    return set(read_any(text))


def first_difference(before, after):
    """Name the first word the two revisions disagree on."""
    counts = {}
    for word in before:
        counts[word] = counts.get(word, 0) + 1
    for word in after:
        counts[word] = counts.get(word, 0) - 1
    lost = sorted(w for w, n in counts.items() if n > 0)
    gained = sorted(w for w, n in counts.items() if n < 0)
    return f"lost {lost[:6]}, gained {gained[:6]}"


def read_file(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def key_of(name):
    major, minor = name[1:].split(".")
    return int(major), int(minor)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", help="the decision log to check")
    parser.add_argument("--against", help="an older revision of the same file")
    parser.add_argument("--allow", action="append", default=[],
                        help="an entry whose text this change edits on purpose (repeatable)")
    parser.add_argument("--allow-move", action="append", default=[],
                        help="an entry whose words this change moves between fields (repeatable)")
    parser.add_argument("--expect", type=int, default=0,
                        help="the number of entries the file must hold")
    args = parser.parse_args(argv)

    text = read_file(args.file)
    problems = lint(args.file, text)
    entries, _ = read_fielded(text)
    if args.expect and len(entries) != args.expect:
        problems.append(f"{args.file}: {len(entries)} entries, expected {args.expect}")
    for problem in problems:
        print(problem)

    failed = bool(problems)
    if args.against:
        old_text = read_file(args.against)
        both = names_of(old_text) & names_of(text)
        for flag, given in (("--allow", args.allow), ("--allow-move", args.allow_move)):
            for name in given:
                if name not in both:
                    print(f"{args.file}: {flag} {name}: the entry is not in both revisions")
                    failed = True
        changed, moved = compare(old_text, text, set(args.allow))
        for name in args.allow:
            print(f"excused {name}: {difference_of(old_text, text, name)}")
        for name, why in changed:
            print(f"{args.file}: {name}: the text changed: {why}")
        undeclared = [name for name in moved if name not in args.allow_move]
        for name in moved:
            if name in args.allow_move:
                print(f"declared move {name}: the words only moved between fields")
        for name in undeclared:
            print(f"{args.file}: {name}: the words moved and no --allow-move names it")
        print(f"entries compared: {len(read_any(old_text)) - len(args.allow)}")
        print(f"entries whose text changed: {len(changed)}")
        print(f"entries whose words moved: {len(moved)}, undeclared: {len(undeclared)}")
        failed = failed or bool(changed) or bool(undeclared)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
