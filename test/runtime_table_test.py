#!/usr/bin/env python3
"""Hold each runtime entry that the emitters call to its definition in std/rt.ft.

The emitters write a call of a runtime entry from a row of the table `rt_sig_of` of
src/fort/runtime_sig.ft: the row gives the name, the result form, the `noreturn` state and the
parameter forms. The definition takes its text from the fort function of std/rt.ft. LLVM accepts
a call whose argument types are not the types of the definition, and the behavior of that call is
undefined (census row E48 of notes/compiler.md 9). Rule V7 of the verifier holds only the reported
operands of a check entry.

This test reads 4 texts and needs no compiler. The entries that the emitters can call are each
`rt_entry.rt_<x>` of src/fort/fir_llvm.ft and src/fort/gen.ft, less `rt_none`; the test finds
them itself, as the census command `grep -o 'rt_entry\\.rt_[a-z_0-9]*'` does. For each entry it
compares 4 parts of the row with the definition `fn <x>(...)` of std/rt.ft:
- the name: the row names `std.rt.<x>`, and std/rt.ft defines `<x>` once at the start of a line;
- each parameter form, in order, the 3 location parameters (file, line, column) included, and the
  count of the parameters;
- the result form;
- the calling convention: the definition is a fort `fn` with a body, not an `extern fn` and with
  no `...`, so its convention is the default one, which the call also has (the emitter writes no
  convention keyword, census row E18). The `noreturn` state of the row is the `noreturn` result
  of the definition, because it selects the attribute group of the definition.

The forms of a definition follow `rt_signature_of_type` of src/fort/runtime_sig.ft: a pointer, a
function type, a span, an array and a `string` are `ptr`; an aggregate result is a leading `ptr`
and a `void` result; `bool` is `ir_bool`, `u8` and `char` are `ir_u8`, `u32` is `ir_i32` and `u64`
is `ir_i64`. A change of that function changes FORMS or form_of here. The test fails on a type
that it cannot read, such as a named struct or enum, and on an entry with no row or no definition.
"""

import argparse
import os
import re
import sys
import tempfile
import unittest

EMITTERS = ("src/fort/fir_llvm.ft", "src/fort/gen.ft")
TABLE = "src/fort/runtime_sig.ft"
RUNTIME = "std/rt.ft"
MODULE = "std.rt."

# The IR form of each primitive type, as ir_of_prim of src/fort/runtime_sig.ft gives it.
FORMS = {
    "void": "ir_void", "bool": "ir_bool", "char": "ir_u8", "u8": "ir_u8", "i8": "ir_i8",
    "u16": "ir_u16", "i16": "ir_i16", "u32": "ir_i32", "i32": "ir_i32", "u64": "ir_i64",
    "i64": "ir_i64", "f32": "ir_f32", "f64": "ir_f64",
}
AGGREGATE = "aggregate"

ENTRY = re.compile(r"rt_entry\.(rt_[a-z_0-9]*)")
ROW = re.compile(r"case\s+rt_entry\.(rt_\w+)\s*:\s*return\s+row(\d?)\s*\(([^;]*)\)\s*;")
DEFINITION = re.compile(r"^(extern\s+)?fn\s+(\w+)\s*\(", re.MULTILINE)
# A string literal, a char literal or a `//` comment, so that a comment can be cut out.
LEXEME = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*')

OPTIONS = None


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as handle:
        return handle.read()


def code(text):
    """The text with each `//` comment cut out; a literal stays, so `"//"` is no comment."""
    return LEXEME.sub(lambda m: "" if m.group(0).startswith("//") else m.group(0), text)


def callable_entries(root):
    """The entries that the emitters name, less `rt_none`, sorted."""
    names = set()
    for rel in EMITTERS:
        names.update(ENTRY.findall(read(root, rel)))
    names.discard("rt_none")
    return sorted(names)


def table_rows(root):
    """Each row of `rt_sig_of`: entry -> (name, result, noreturn, [params]) or a problem text."""
    rows = {}
    for entry, arity, args in ROW.findall(code(read(root, TABLE))):
        parts = [p.strip() for p in args.split(",")]
        if len(parts) < 3 or not parts[0].startswith('"') or parts[2] not in ("true", "false"):
            rows[entry] = "the row of %s has no name, result and noreturn state" % entry
            continue
        params = [p.removeprefix("ir_form.") for p in parts[3:]]
        if int(arity or "0") != len(params):
            rows[entry] = "the row of %s calls row%s with %d parameters" % (
                entry, arity, len(params))
            continue
        rows[entry] = (parts[0].strip('"'), parts[1].removeprefix("ir_form."),
                       parts[2] == "true", params)
    return rows


def split_top(text):
    """`text` split at each comma outside parentheses, each part stripped; [] for no text."""
    parts, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == "," and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    last = text[start:].strip()
    return parts + [last] if parts or last else parts


def definitions(root):
    """Each `fn` at the start of a line of std/rt.ft: name -> list of (extern, [params], result)."""
    text = code(read(root, RUNTIME))
    found = {}
    for m in DEFINITION.finditer(text):
        depth, i = 1, m.end()
        while i < len(text) and depth > 0:
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
        end = min(k for k in (text.find("{", i), text.find(";", i), len(text)) if k >= 0)
        params = [" ".join(p.split()) for p in split_top(text[m.end():i - 1])]
        result = " ".join(text[i:end].split())
        found.setdefault(m.group(2), []).append((m.group(1) is not None, params, result))
    return found


def form_of(type_text):
    """The IR form of a fort type, AGGREGATE for one that travels by address, or None."""
    t = re.sub(r"\s+", "", re.sub(r"\b(?:mut|own)\b", "", type_text))
    if t.startswith("fn(") or t.endswith("*"):
        return "ir_ptr"
    if t.endswith("@") or t.endswith("]") or t == "string":
        return AGGREGATE
    return FORMS.get(t)


def signature(params, result):
    """(result, noreturn, [params]) of a definition as the emitter calls it, or a problem text."""
    if result == "noreturn":
        out, noreturn, lead = "ir_void", True, []
    else:
        out, noreturn, lead = form_of(result), False, []
        if out == AGGREGATE:
            out, lead = "ir_void", ["ir_ptr"]
        if out is None:
            return "the test cannot read the result type `%s`" % result
    forms = []
    for p in params:
        if p == "...":
            return "the definition takes `...`: a variable tail has the C convention"
        words = p.rsplit(None, 1)
        form = form_of(words[0]) if len(words) == 2 else None
        if form is None:
            return "the test cannot read the parameter `%s`" % p
        forms.append("ir_ptr" if form == AGGREGATE else form)
    return out, noreturn, lead + forms


def compare(root):
    """(the entries compared, the problems): one problem text for each part that differs."""
    entries, rows, defs = callable_entries(root), table_rows(root), definitions(root)
    problems = []
    for entry in entries:
        row = rows.get(entry)
        if row is None:
            problems.append("%s: rt_sig_of has no row" % entry)
            continue
        if isinstance(row, str):
            problems.append("%s: %s" % (entry, row))
            continue
        name, result, noreturn, params = row
        if not name.startswith(MODULE):
            problems.append("%s: the row names %s, not a function of %s" % (entry, name, RUNTIME))
            continue
        bare = name[len(MODULE):]
        found = defs.get(bare, [])
        if len(found) != 1:
            problems.append("%s: %s defines `%s` %d times, not once" % (
                entry, RUNTIME, bare, len(found)))
            continue
        is_extern, def_params, def_result = found[0]
        if is_extern:
            problems.append("%s: `%s` is an extern fn and has the C convention" % (entry, bare))
            continue
        made = signature(def_params, def_result)
        if isinstance(made, str):
            problems.append("%s: %s" % (entry, made))
            continue
        made_result, made_noreturn, made_params = made
        if made_result != result:
            problems.append("%s: the row gives the result %s, `%s` gives %s" % (
                entry, result, bare, made_result))
        if made_noreturn != noreturn:
            problems.append("%s: the row gives noreturn %s, `%s` gives %s" % (
                entry, noreturn, bare, made_noreturn))
        if len(made_params) != len(params):
            problems.append("%s: the row gives %d parameters, `%s` takes %d" % (
                entry, len(params), bare, len(made_params)))
        for k, (want, got) in enumerate(zip(params, made_params)):
            if want != got:
                problems.append("%s: parameter %d: the row gives %s, `%s` gives %s" % (
                    entry, k, want, bare, got))
    return entries, problems


class RuntimeTable(unittest.TestCase):
    def test_each_entry_that_the_emitters_call_matches_its_definition(self):
        entries, problems = compare(OPTIONS.root)
        print("runtime-table: %d entries compared: %s" % (len(entries), " ".join(entries)),
              file=sys.stderr)
        self.assertGreater(len(entries), 0, "the emitters name no runtime entry")
        self.assertEqual(problems, [])


# A small tree for the control tests: 2 emitters, a table of 4 rows and a runtime. `rt_b` stands
# only in a comment of an emitter, as the census command counts it.
CONTROL = {
    "src/fort/gen.ft": "call(rt_entry.rt_a);\nx = rt_entry.rt_none;\n// rt_entry.rt_b\n",
    "src/fort/fir_llvm.ft": "call(runtime_sig.rt_entry.rt_c, rt_entry.rt_d);\n",
    "src/fort/runtime_sig.ft": (
        "fn rt_sig_of(rt_entry rt) rt_sig {\n    switch (rt) {\n"
        "    case rt_entry.rt_a:\n"
        "        return row4(\"std.rt.a\", ir_form.ir_void, true, ir_form.ir_i64,\n"
        "                    ir_form.ir_ptr, ir_form.ir_i32, ir_form.ir_i32);\n"
        "    case rt_entry.rt_b:\n        return row(\"std.rt.b\", ir_form.ir_void, false);\n"
        "    case rt_entry.rt_c:\n"
        "        return row3(\"std.rt.c\", ir_form.ir_ptr, false, ir_form.ir_bool, ir_form.ir_u8,\n"
        "                    ir_form.ir_ptr);\n"
        "    case rt_entry.rt_d:\n"
        "        return row2(\"std.rt.d\", ir_form.ir_void, false, ir_form.ir_ptr, ir_form.ir_ptr);"
        "\n    case rt_entry.rt_none:\n        panic(\"no\");\n    }\n}\n"),
    "std/rt.ft": (
        "// fn a(i32 x) void {\n"
        "fn a(u64 n, char* file, u32 line,\n      u32 col) noreturn {\n    s = \"//\";\n}\n"
        "fn b() void {\n}\n"
        "fn c(bool flag, char c, fn (u8*, u64) void f) void mut* own {\n}\n"
        "fn d(u8 mut@ bytes) string@ {\n}\n"),
}


class Control(unittest.TestCase):
    def problems(self, file=None, old=None, new=None):
        with tempfile.TemporaryDirectory() as root:
            for rel, text in CONTROL.items():
                if rel == file:
                    self.assertEqual(text.count(old), 1, old)
                    text = text.replace(old, new)
                os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
                with open(os.path.join(root, rel), "w", encoding="utf-8") as handle:
                    handle.write(text)
            return compare(root)

    def one(self, file, old, new, problem):
        entries, problems = self.problems(file, old, new)
        self.assertEqual(problems, [problem])

    def test_the_control_tree_has_4_entries_and_no_problem(self):
        self.assertEqual(self.problems(), (["rt_a", "rt_b", "rt_c", "rt_d"], []))

    def test_a_new_entry_of_an_emitter_is_compared(self):
        entries, problems = self.problems("src/fort/gen.ft", "rt_none;", "rt_e;")
        self.assertEqual(len(entries), 5)
        self.assertEqual(problems, ["rt_e: rt_sig_of has no row"])

    def test_a_name_with_no_definition_fails(self):
        self.one("src/fort/runtime_sig.ft", '"std.rt.b"', '"std.rt.bb"',
                 "rt_b: std/rt.ft defines `bb` 0 times, not once")

    def test_a_definition_that_a_comment_holds_is_no_definition(self):
        self.one("std/rt.ft", "fn b() void {\n}\n", "// fn b() void {\n",
                 "rt_b: std/rt.ft defines `b` 0 times, not once")

    def test_a_name_outside_the_runtime_fails(self):
        self.one("src/fort/runtime_sig.ft", '"std.rt.b"', '"std.str.b"',
                 "rt_b: the row names std.str.b, not a function of std/rt.ft")

    def test_a_second_definition_fails(self):
        self.one("std/rt.ft", "fn b() void {\n}\n", "fn b() void {\n}\nfn b() void {\n}\n",
                 "rt_b: std/rt.ft defines `b` 2 times, not once")

    def test_a_parameter_type_fails(self):
        self.one("std/rt.ft", "u64 n", "u32 n",
                 "rt_a: parameter 0: the row gives ir_i64, `a` gives ir_i32")

    def test_a_location_type_fails(self):
        self.one("std/rt.ft", "u32 line", "u64 line",
                 "rt_a: parameter 2: the row gives ir_i32, `a` gives ir_i64")

    def test_a_parameter_attribute_fails(self):
        self.one("std/rt.ft", "char c,", "i8 c,",
                 "rt_c: parameter 1: the row gives ir_u8, `c` gives ir_i8")

    def test_a_missing_parameter_fails(self):
        self.one("std/rt.ft", "fn b() void", "fn b(i32 x) void",
                 "rt_b: the row gives 0 parameters, `b` takes 1")

    def test_a_result_type_fails(self):
        self.one("std/rt.ft", "void mut* own {", "u64 {",
                 "rt_c: the row gives the result ir_ptr, `c` gives ir_i64")

    def test_an_aggregate_result_is_a_leading_pointer(self):
        self.one("std/rt.ft", "string@ {", "void {",
                 "rt_d: the row gives 2 parameters, `d` takes 1")

    def test_a_noreturn_state_fails(self):
        self.one("src/fort/runtime_sig.ft", "ir_form.ir_void, true,", "ir_form.ir_void, false,",
                 "rt_a: the row gives noreturn False, `a` gives True")

    def test_an_extern_definition_fails(self):
        self.one("std/rt.ft", "fn b() void {\n}\n", "extern fn b() void;\n",
                 "rt_b: `b` is an extern fn and has the C convention")

    def test_a_variable_tail_fails(self):
        self.one("std/rt.ft", "fn b() void", "fn b(...) void",
                 "rt_b: the definition takes `...`: a variable tail has the C convention")

    def test_a_named_type_fails(self):
        self.one("std/rt.ft", "u8 mut@ bytes", "buffer mut* b, decimal d",
                 "rt_d: the test cannot read the parameter `decimal d`")

    def test_a_named_result_fails(self):
        self.one("std/rt.ft", "string@ {", "decimal {",
                 "rt_d: the test cannot read the result type `decimal`")

    def test_an_entry_with_no_row_fails(self):
        self.one("src/fort/runtime_sig.ft", "case rt_entry.rt_b:", "case rt_entry.rt_x:",
                 "rt_b: rt_sig_of has no row")

    def test_a_row_helper_of_another_arity_fails(self):
        self.one("src/fort/runtime_sig.ft", "row2(", "row3(",
                 "rt_d: the row of rt_d calls row3 with 2 parameters")


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--root", default=os.path.join(os.path.dirname(__file__), ".."),
                        help="the top of the source tree")
    OPTIONS, rest = parser.parse_known_args()
    return 0 if unittest.main(argv=[sys.argv[0]] + rest, exit=False).result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
