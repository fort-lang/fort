#!/usr/bin/env python3
"""Read spec/decisions.md: the fielded form of one entry, for a test that reads a rule."""

import re

ENTRY_HEAD = re.compile(r"^### (D\d+\.\d+) (\S.*)$")
FIELD = re.compile(r"^- ([a-z]+): ?(.*)$")
FIELDS = ("owner", "rule", "rationale", "history")
REQUIRED = ("owner", "rule")


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


def rule_of(text, tag):
    """The rule field of one entry, for a test that reads a rule out of the log."""
    entries, _ = read_fielded(text)
    if tag not in entries:
        raise KeyError(f"{tag} is not in the decision log")
    return entries[tag]["rule"]
