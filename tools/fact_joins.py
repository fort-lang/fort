#!/usr/bin/env python3
"""Hold each fact of the heap proof state to the join of its type and to its provenance
(D17.16, D17.18).

`src/fort/ownership_heap.ft` declares the structs of STRUCTS. Each of their fields has exactly
one of two declarations:

- a fact type of `src/fort/ownership_facts.ft`, a key of FACT_JOINS, written bare or as
  `ownership_facts.<type>`. Its struct must have a join function in JOIN_FUNCTIONS, and that
  function must assign the field from the join of the type at least once:
  `<name>.<field> = [ownership_facts.]<join>(...)`.
- one tag `// join: <kind>` at the end of the field line, with a kind of TAGS.

No test reads a provenance before a rule prunes on it, so this tool holds the provenance:

- A join function joins. Each assignment of a whole typed field there calls the join of its
  type, each read of a whole typed field is an argument of that join, and no statement assigns
  the provenance `how`. A write to another part of a fact, such as `out.root.value = ...`, keeps
  the provenance of the join; the report counts it.
- A `may` field has a join assignment with `||` or `|` and no `&&` or `&`; a `must` field has one
  with `&&` or `&`. A `same`, `owner` or `none` field has no assignment with one of these four
  operators. The join function of its struct holds the assignment.
- A join function reads a whole record of RECORD_JOINS, or a whole record parameter of
  RECORD_PARAMETERS, only as an argument of its join, or on a line with the tag
  `// join: copy`: one side gives the record.
- A graft function (GRAFT_FUNCTIONS) reads a whole typed field of a callee only as an argument of
  the join of its type, of its ASSUMES or IMPORTS function, or of its PRUNES predicate: the
  caller imports or assumes each callee fact, and a predicate copies none. Each assignment of
  GRAFT_ASSUMES stays, so a callee fact cannot pass without one.
- `provenance.established` and a call of `null_value()` occur only in the functions of
  ESTABLISHED_SITES and NULL_VALUE_SITES, each exactly as often as the table says. A new
  established fact is a change of this table, so review sees it.
- `may_prune` and `may_prune_truth` occur only in the functions of PRUNING_SITES, each exactly as
  often as the table says. Each site that removes a path reads one of them (D17.18), so a site
  that drops its predicate, or a new site, is a change of this table.
- A known scalar is a fact too. `forget_scalars` occurs only in the functions of FORGET_SITES,
  and each callee effect, a call of `apply` or `unknown_call`, only in those of EFFECT_SITES, each
  exactly as often as the table says. In each function of EFFECT_SITES the forget comes before
  the first callee effect, so each callee effect follows the one forget.

The report has one row for each field. The tool fails for each of these problems:

- a field with no fact type and no tag;
- a tag with a kind that TAGS does not name, a second tag, or other text after the kind;
- a field with a fact type and a tag;
- a field with a fact type in a struct that has no join function;
- a typed field that its join function does not assign from the join of its type;
- an assignment of a whole typed field in a join function that does not call that join;
- a read of a whole typed field in a join function outside the join of its type;
- an assignment of the provenance `how` in a join function;
- a `may` or `must` field with no join assignment, or with an assignment of the other operator;
- a `same`, `owner` or `none` field with a join assignment that uses `||`, `|`, `&&` or `&`;
- a whole record read in a join function outside its join and without the tag `// join: copy`;
- a read of a whole typed field in a graft function outside its join, its assume or import
  function and its predicate;
- a graft function without an assignment of GRAFT_ASSUMES;
- an occurrence of `provenance.established`, `null_value()`, a pruning predicate,
  `forget_scalars`, `apply` or `unknown_call` that its table does not count;
- a function of EFFECT_SITES whose first callee effect comes before its forget;
- a struct of STRUCTS, or a join function, that the file does not declare.

A new fact type adds one row to FACT_JOINS and one join function to `ownership_facts.ft`.

Usage: fact_joins.py [--root DIR] [--source FILE]. The report goes to stdout and each problem goes
to stderr. The exit status is 0 for no problem, 1 for a problem and 2 for a bad argument.
"""

import argparse
from pathlib import Path
import re
import sys

SOURCE = "src/fort/ownership_heap.ft"
# The structs whose fields the rule holds, in report order.
STRUCTS = ("atom", "value", "state", "summary", "interface_entry")
# Each fact type of ownership_facts.ft and the function that joins two of its facts.
FACT_JOINS = {
    "nullness": "join_nullness",
    "status_fact": "join_status_fact",
    "may_set": "join_may_set",
    "must_set": "join_must_set",
}
# The function that gives a fact of a type the provenance `assumed`.
ASSUMES = {
    "nullness": "assume",
    "status_fact": "assume_status",
}
# The function that gives a callee fact of a type its provenance in the caller: `imported` for a
# fact that the callee established or imported, `assumed` otherwise.
IMPORTS = {
    "nullness": "import_nullness",
}
# The predicate that reads a fact of a type to decide whether it removes a path. It copies no fact.
PRUNES = {
    "nullness": "may_prune",
}
# The function that joins two records of a struct. A struct that no join function names has no
# typed field.
JOIN_FUNCTIONS = {
    "atom": "join_atom",
    "value": "join_values",
    "state": "join",
}
# A container of records whose struct has typed fields, and the join of two of its records.
RECORD_JOINS = {
    "atoms": "join_atom",
}
# The record parameters of a join function, which hold the facts of one side each.
RECORD_PARAMETERS = {
    "join_atom": ("a", "b"),
    "join_values": ("vx", "vy"),
}
# The functions that put the facts of a callee summary into the state of its caller.
GRAFT_FUNCTIONS = ("apply", "graft_value")
# The assignments that give the facts of a callee their provenance in its caller: a graft
# function, a typed field, and the IMPORTS or ASSUMES function that each graft assigns it from.
# A root fact is imported; a status is assumed, because no rule reads a status provenance.
GRAFT_ASSUMES = (
    ("graft_value", "nullness", "import_nullness"),
    ("apply", "root", "import_nullness"),
    ("apply", "status", "assume_status"),
)
# The functions that may write `provenance.established`, and how often. Each one is an operation
# of the body that observes its fact: a constant, a move, an allocation, a null test, a release,
# a storage end, a loss that the body reports, or an unknown effect.
ESTABLISHED_SITES = {
    "null_value": 1,
    "address_fact": 1,
    "operand": 1,
    "allocate": 2,
    "refine": 1,
    "null_descendants": 1,
    "release": 4,
    "write_at": 1,
    "escape": 1,
    "havoc_value": 1,
    "unknown_call": 1,
    "check_overwrite": 1,
    "end_slot": 1,
    "require_one": 2,
    "finish_return": 1,
}
# The functions that may call `null_value()`, and how often.
NULL_VALUE_SITES = {
    "operand": 2,
    "place_leaves": 1,
    "allocate": 1,
    "release_place": 1,
    "end_slot": 1,
    "finish_return": 1,
}
# The functions that may call `may_prune` or `may_prune_truth`, and how often: each site that
# removes a path (D17.18). null_test: an empty value, an address and the atom of a view. refine:
# a known truth, and one root result for the null and the nonnull edge. apply: the null exit
# root of the callee and the nonnull caller root of the exit contradiction. A callee without a
# return removes a caller path only because its own paths ended at these sites, so that site
# reads no fact.
PRUNING_SITES = {
    "null_test": 3,
    "refine": 2,
    "apply": 2,
}
# The functions that may call `forget_scalars`, and how often: a call forgets each scalar local
# that a callee can write before any callee effect.
FORGET_SITES = {
    "call": 1,
}
# The functions that may apply a callee effect, a call of `apply` or `unknown_call`, and how often.
# Each of them comes after the forget of FORGET_SITES in the same function.
EFFECT_SITES = {
    "call": 4,
}
# same: an identity that a join keeps only when both sides agree. owner: a container or an index
# whose records the join of the struct pairs. none: no path fact, or a fact that the join drops.
# may and must: a flag that holds on some path or on each path and has no fact type yet.
TAGS = ("same", "owner", "none", "may", "must")
COPY_TAG = "// join: copy"

STRUCT_START = re.compile(r"^struct (\w+) \{\s*$")
FIELD = re.compile(r"^\s+(?P<type>[^\s=;][^=;]*?)\s+(?P<name>[a-z_][a-z_0-9]*);\s*"
                   r"(?://(?P<comment>.*))?$")
TAG = re.compile(r"^\s*join:\s*(?P<rest>.*?)\s*$")
FUNCTION_START = re.compile(r"^fn (\w+)\(")
FACT_TYPE = re.compile(r"^(?:ownership_facts\.)?(\w+)$")
# The names before a `.` that are no record: an enum or a module.
NO_RECORD = ("atom_status", "root_kind", "provenance", "ownership_facts")


class Field:
    """One field of a struct: its declaration and what the join functions do with it."""

    def __init__(self, struct, name, type_text, line):
        self.struct = struct
        self.name = name
        self.type_text = type_text
        self.line = line
        self.tag = None
        self.fact = None
        self.joins = 0
        self.partial = 0
        self.operators = 0

    def where(self):
        return "%s:%d" % (SOURCE, self.line)


def parse_structs(lines, problems):
    """The fields of each struct of STRUCTS, in declaration order."""
    structs = {}
    current = None
    for number, line in enumerate(lines, 1):
        start = STRUCT_START.match(line)
        if start is not None:
            current = start.group(1) if start.group(1) in STRUCTS else None
            if current is not None:
                structs[current] = []
            continue
        if current is None:
            continue
        if line.startswith("}"):
            current = None
            continue
        if line.strip() == "" or line.strip().startswith("//"):
            continue
        found = FIELD.match(line)
        if found is None:
            problems.append("%s:%d: struct %s: a field line that the tool cannot read: %s"
                            % (SOURCE, number, current, line.strip()))
            continue
        field = Field(current, found.group("name"), found.group("type"), number)
        structs[current].append(field)
        comment = found.group("comment")
        fact = FACT_TYPE.match(field.type_text)
        if fact is not None and fact.group(1) in FACT_JOINS:
            field.fact = fact.group(1)
        if comment is None or "join:" not in comment:
            if field.fact is None:
                problems.append("%s: %s.%s has no fact type and no `// join:` tag"
                                % (field.where(), current, field.name))
            continue
        tag = TAG.match(comment)
        rest = tag.group("rest") if tag is not None else ""
        if tag is None or rest not in TAGS:
            problems.append("%s: %s.%s has the tag `//%s`; a tag is `// join: <kind>` with one "
                            "kind of %s" % (field.where(), current, field.name, comment,
                                            "|".join(TAGS)))
            continue
        field.tag = rest
        if field.fact is not None:
            problems.append("%s: %s.%s has the fact type %s and the tag %s; give it one of them"
                            % (field.where(), current, field.name, field.fact, field.tag))
    for name in STRUCTS:
        if name not in structs:
            problems.append("%s: the file declares no struct %s" % (SOURCE, name))
    return structs


def code_of(line):
    """The line without a trailing `//` comment. A `//` in a string literal stays."""
    quoted = False
    at = 0
    while at < len(line):
        char = line[at]
        if char == "\\" and quoted:
            at += 2
            continue
        if char == '"':
            quoted = not quoted
        elif not quoted and line.startswith("//", at):
            return line[:at]
        at += 1
    return line


def without_strings(code):
    """The code with the text of each string literal blanked, so no name in a string counts."""
    return re.sub(r'"(?:[^"\\]|\\.)*"', lambda m: '"' + " " * (len(m.group(0)) - 2) + '"', code)


class Body:
    """One top-level function: its first line, its source lines and its code without comments.
    `code` joins the lines with a newline, so an offset in it gives a line of the function."""

    def __init__(self, name, first):
        self.name = name
        self.first = first
        self.lines = []

    def finish(self):
        self.code = "\n".join(code_of(line) for line in self.lines)
        self.starts = []
        offset = 0
        for line in self.lines:
            self.starts.append(offset)
            offset += len(code_of(line)) + 1

    def line_at(self, offset):
        """The index of the line of the function that holds `offset`."""
        index = 0
        while index + 1 < len(self.starts) and self.starts[index + 1] <= offset:
            index += 1
        return index

    def number_at(self, offset):
        return self.first + self.line_at(offset)

    def spans(self, names):
        """The argument span (open, close) of each call of a function in `names`."""
        out = []
        pattern = r"\b(?:ownership_facts\.)?(?:%s)\(" % "|".join(re.escape(n) for n in names)
        for found in re.finditer(pattern, self.code):
            depth = 0
            at = found.end() - 1
            while at < len(self.code):
                if self.code[at] == "(":
                    depth += 1
                elif self.code[at] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                at += 1
            out.append((found.end() - 1, at))
        return out


def function_bodies(lines):
    """Each top-level function, from its `fn` line to its closing brace."""
    bodies = {}
    current = None
    for number, line in enumerate(lines, 1):
        start = FUNCTION_START.match(line)
        if start is not None:
            current = Body(start.group(1), number)
            bodies[current.name] = current
        if current is not None:
            current.lines.append(line)
            if line.startswith("}"):
                current.finish()
                current = None
    return bodies


def inside(offset, spans):
    return any(open_at < offset < close_at for open_at, close_at in spans)


def field_uses(code, name):
    """Each use of a whole field `name` of a record in `code`: (offset, is_write, rhs offset).
    A use followed by `.` is a part of the field and no use of the whole field."""
    out = []
    pattern = r"(?<=[\w\]\)])(?P<at>\.|->)%s\b" % re.escape(name)
    for found in re.finditer(pattern, code):
        before = re.search(r"(\w+)\s*$", code[:found.start()])
        if before is not None and before.group(1) in NO_RECORD:
            continue
        after = code[found.end():]
        if re.match(r"\s*\.(?!\.)", after):
            continue
        write = re.match(r"\s*=(?!=)", after)
        out.append((found.start(), write is not None,
                    found.end() + write.end() if write is not None else None))
    return out


def statement_from(code, offset):
    """The text from `offset` to the end of its statement."""
    end = code.find(";", offset)
    return code[offset:] if end < 0 else code[offset:end]


def check_joins(structs, bodies, problems):
    """Hold each typed field to its join in the join functions."""
    typed = [f for fields in structs.values() for f in fields if f.fact is not None]
    for name in sorted(set(JOIN_FUNCTIONS.values())):
        if name not in bodies:
            problems.append("%s: the file declares no join function %s" % (SOURCE, name))
    for field in typed:
        if field.struct not in JOIN_FUNCTIONS:
            problems.append("%s: %s.%s has the fact type %s, but no join function joins a %s"
                            % (field.where(), field.struct, field.name, field.fact,
                               field.struct))
    for function in sorted(set(JOIN_FUNCTIONS.values())):
        body = bodies.get(function)
        if body is None:
            continue
        for found in re.finditer(r"(?:\.|->)how\s*=(?!=)", body.code):
            problems.append("%s:%d: %s assigns a provenance by hand; the join of a fact "
                            "type gives it" % (SOURCE, body.number_at(found.start()), function))
        for field in typed:
            join = FACT_JOINS[field.fact]
            spans = body.spans([join])
            for offset, write, rhs in field_uses(body.code, field.name):
                number = body.number_at(offset)
                if write:
                    calls = re.match(r"\s*(?:ownership_facts\.)?%s\(" % join, body.code[rhs:])
                    if calls is None:
                        problems.append("%s:%d: %s assigns %s.%s without %s"
                                        % (SOURCE, number, function, field.struct, field.name,
                                           join))
                    elif JOIN_FUNCTIONS.get(field.struct) == function:
                        field.joins += 1
                elif not inside(offset, spans):
                    problems.append("%s:%d: %s reads %s.%s outside %s, so one side gives "
                                    "its fact" % (SOURCE, number, function, field.struct,
                                                  field.name, join))
            if JOIN_FUNCTIONS.get(field.struct) == function:
                field.partial += len(re.findall(r"(?<=[\w\]\)])(?:\.|->)%s\.\w+\s*=(?!=)"
                                                % field.name, body.code))
    for field in typed:
        function = JOIN_FUNCTIONS.get(field.struct)
        if function in bodies and field.joins == 0:
            problems.append("%s: %s.%s has the fact type %s, but %s assigns it from no %s"
                            % (field.where(), field.struct, field.name, field.fact, function,
                               FACT_JOINS[field.fact]))


def check_operators(structs, bodies, problems):
    """Hold each tagged field to the operator of its join assignment."""
    for fields in structs.values():
        for field in fields:
            if field.tag is None or field.struct not in JOIN_FUNCTIONS:
                continue
            function = JOIN_FUNCTIONS[field.struct]
            body = bodies.get(function)
            if body is None:
                continue
            for offset, write, rhs in field_uses(body.code, field.name):
                if not write:
                    continue
                text = statement_from(body.code, rhs)
                union = "||" in text or re.search(r"[^|]\|[^|]", text) is not None
                meet = "&&" in text or re.search(r"\s&\s", text) is not None
                if field.tag == "may":
                    good = union and not meet
                elif field.tag == "must":
                    good = meet
                else:
                    good = not union and not meet
                field.operators += 1
                if not good:
                    problems.append("%s:%d: %s.%s has the tag %s, but %s joins it as `%s`"
                                    % (SOURCE, body.number_at(offset), field.struct,
                                       field.name, field.tag, function,
                                       " ".join(text.split())))
            if field.operators == 0 and field.tag in ("may", "must"):
                problems.append("%s: %s.%s has the tag %s, but %s assigns it nowhere"
                                % (field.where(), field.struct, field.name, field.tag,
                                   function))


def check_records(bodies, problems):
    """Hold each whole record that a join function reads to its join or to a copy tag."""
    for function in sorted(set(JOIN_FUNCTIONS.values())):
        body = bodies.get(function)
        if body is None:
            continue
        for container, join in RECORD_JOINS.items():
            spans = body.spans([join])
            for found in re.finditer(r"(?<=[\w\]\)])(?:\.|->)%s\[" % container, body.code):
                depth = 0
                at = found.end() - 1
                while at < len(body.code):
                    if body.code[at] == "[":
                        depth += 1
                    elif body.code[at] == "]":
                        depth -= 1
                        if depth == 0:
                            break
                    at += 1
                after = body.code[at + 1:]
                if re.match(r"\s*(?:\.(?!\.)|\[|=(?!=))", after):
                    continue
                if inside(found.start(), spans):
                    continue
                line = body.lines[body.line_at(found.start())]
                if line.rstrip().endswith(COPY_TAG):
                    continue
                problems.append("%s:%d: %s reads a whole record of `%s` outside %s and "
                                "without `%s`" % (SOURCE, body.number_at(found.start()),
                                                  function, container, join, COPY_TAG))
    for function, names in RECORD_PARAMETERS.items():
        body = bodies.get(function)
        if body is None:
            continue
        spans = body.spans([function])
        for name in names:
            pattern = r"(?<![\w.>])%s\b(?!\s*(?:\.(?!\.)|->|=(?!=)))" % re.escape(name)
            for found in re.finditer(pattern, body.code):
                if body.line_at(found.start()) == 0 or inside(found.start(), spans):
                    continue
                line = body.lines[body.line_at(found.start())]
                if line.rstrip().endswith(COPY_TAG):
                    continue
                problems.append("%s:%d: %s reads the whole record `%s` without `%s`"
                                % (SOURCE, body.number_at(found.start()), function, name,
                                   COPY_TAG))


def check_grafts(structs, bodies, problems):
    """Hold each typed fact that a graft function reads to its join or to its assume."""
    typed = [f for fields in structs.values() for f in fields if f.fact is not None]
    for function in GRAFT_FUNCTIONS:
        body = bodies.get(function)
        if body is None:
            problems.append("%s: the file declares no graft function %s" % (SOURCE, function))
            continue
        for field in typed:
            names = [FACT_JOINS[field.fact]]
            for table in (ASSUMES, IMPORTS, PRUNES):
                if field.fact in table:
                    names.append(table[field.fact])
            spans = body.spans(names)
            for offset, write, _ in field_uses(body.code, field.name):
                if not write and not inside(offset, spans):
                    problems.append("%s:%d: %s reads %s.%s outside %s; the caller imports or "
                                    "assumes each callee fact"
                                    % (SOURCE, body.number_at(offset), function, field.struct,
                                       field.name, " or ".join(names)))


def check_graft_assumes(bodies, problems):
    """Hold each assignment of GRAFT_ASSUMES in its graft function."""
    for function, field, assume in GRAFT_ASSUMES:
        body = bodies.get(function)
        if body is None:
            continue
        pattern = r"(?<=[\w\]\)])(?:\.|->)%s\s*=\s*(?:ownership_facts\.)?%s\(" % (field, assume)
        if re.search(pattern, body.code) is None:
            problems.append("%s:%d: %s assigns no %s from %s, so a callee fact can pass as it "
                            "is" % (SOURCE, body.first, function, field, assume))


def count_sites(bodies, lines, pattern, table, what, problems):
    """Hold the occurrences of `pattern` to the count of each function in `table`."""
    found = {}
    inside_body = set()
    for body in bodies.values():
        inside_body.update(range(body.first, body.first + len(body.lines)))
        code = without_strings("\n".join(code_of(line) for line in body.lines[1:]))
        count = len(re.findall(pattern, code))
        if count:
            found[body.name] = count
    for number, line in enumerate(lines, 1):
        if number not in inside_body and re.search(pattern, without_strings(code_of(line))):
            problems.append("%s:%d: %s outside a function" % (SOURCE, number, what))
    for name in sorted(set(found) | set(table)):
        if found.get(name, 0) != table.get(name, 0):
            problems.append("%s: %s has %d %s, and the table allows %d"
                            % (SOURCE, name, found.get(name, 0), what, table.get(name, 0)))
    return sum(found.values())


def check_forget_order(bodies, effects, problems):
    """Hold that each function of `effects` forgets its scalars before its first callee effect."""
    for name in sorted(effects):
        body = bodies.get(name)
        if body is None:
            continue
        code = without_strings("\n".join(code_of(line) for line in body.lines[1:]))
        forget = re.search(r"\bforget_scalars\(", code)
        effect = re.search(r"\b(?:apply|unknown_call)\(", code)
        if effect is not None and (forget is None or forget.start() > effect.start()):
            problems.append("%s:%d: %s applies a callee effect before forget_scalars, so a callee "
                            "can change a known scalar" % (SOURCE, body.first, name))


def report(structs, out):
    """Write one row for each field, then the totals."""
    count = typed = 0
    for name in STRUCTS:
        for field in structs.get(name, []):
            count += 1
            if field.fact is not None:
                typed += 1
                function = JOIN_FUNCTIONS.get(field.struct, "(no join function)")
                text = "%s, %s assigns %s %d time(s)" % (field.fact, function,
                                                          FACT_JOINS[field.fact], field.joins)
                if field.partial:
                    text += ", %d partial write(s)" % field.partial
            elif field.tag is not None:
                text = "tag %s" % field.tag
                if field.operators and field.tag in ("may", "must"):
                    text += ", %d join assignment(s)" % field.operators
            else:
                text = "NO JOIN"
            out.write("%s.%s: %s\n" % (field.struct, field.name, text))
    untagged = sum(1 for fields in structs.values() for f in fields
                   if f.fact is None and f.tag is None)
    out.write("fact_joins: %d fields, %d typed, %d tagged, %d untagged\n"
              % (count, typed, count - typed - untagged, untagged))


def check(text, established=None, null_values=None, pruning=None, forget=None, effects=None):
    """The parsed structs, the problems and the site counts of one source text. The tables
    default to ESTABLISHED_SITES, NULL_VALUE_SITES, PRUNING_SITES, FORGET_SITES and
    EFFECT_SITES."""
    lines = text.splitlines()
    problems = []
    structs = parse_structs(lines, problems)
    bodies = function_bodies(lines)
    check_joins(structs, bodies, problems)
    check_operators(structs, bodies, problems)
    check_records(bodies, problems)
    check_grafts(structs, bodies, problems)
    check_graft_assumes(bodies, problems)
    sites = (
        count_sites(bodies, lines, r"\bprovenance\.established\b",
                    ESTABLISHED_SITES if established is None else established,
                    "provenance.established", problems),
        count_sites(bodies, lines, r"\bnull_value\(\)",
                    NULL_VALUE_SITES if null_values is None else null_values,
                    "null_value()", problems),
        count_sites(bodies, lines, r"\bmay_prune(?:_truth)?\(",
                    PRUNING_SITES if pruning is None else pruning,
                    "pruning predicate", problems),
        count_sites(bodies, lines, r"\bforget_scalars\(",
                    FORGET_SITES if forget is None else forget,
                    "forget_scalars", problems),
        count_sites(bodies, lines, r"\b(?:apply|unknown_call)\(",
                    EFFECT_SITES if effects is None else effects,
                    "callee effect", problems),
    )
    check_forget_order(bodies, EFFECT_SITES if effects is None else effects, problems)
    return structs, problems, sites


def run(text, out, err, established=None, null_values=None, pruning=None, forget=None,
        effects=None):
    """Check one source text, write the report and the problems, and give the exit status."""
    structs, problems, sites = check(text, established, null_values, pruning, forget, effects)
    report(structs, out)
    out.write("fact_joins: %d provenance.established, %d null_value(), %d pruning predicates, "
              "%d forget_scalars, %d callee effects in their functions\n" % sites)
    out.flush()
    for item in problems:
        err.write("fact_joins: error: %s\n" % item)
    if problems:
        err.write("fact_joins: %d problems\n" % len(problems))
        return 1
    out.write("fact_joins: no problems\n")
    return 0


def main(argv=None, out=sys.stdout, err=sys.stderr):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="repository root (default: the parent of tools/)")
    parser.add_argument("--source", type=Path, default=None,
                        help="the heap module (default: <root>/%s)" % SOURCE)
    args = parser.parse_args(argv)
    source = args.source if args.source is not None else args.root / SOURCE
    if not source.is_file():
        err.write("fact_joins: %s is no file\n" % source)
        return 2
    return run(source.read_text(), out, err)


if __name__ == "__main__":
    sys.exit(main())
