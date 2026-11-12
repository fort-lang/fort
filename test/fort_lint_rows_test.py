#!/usr/bin/env python3
"""Hold the rules of tools/fort_lint.py on the audit rows of D9.8.

Each test writes small modules into a temporary tree, runs the lint over them with the compiler
of the build, and compares the D9.8 problems that the lint reports with the ones that the test
expects, line by line. The rules (spec/decisions.md D9.8, "The rows" and "The lint"):
- each `extern fn` of a trusted module has a declaration row: a first line `// C: <the symbol>,
  <the source of its contract>.`, and the five words `gives:`, `writes:`, `keeps:`, `calls:` and
  `frees:`, each once;
- the call rows of a function of a trusted module count each call of an `extern fn`, of a
  boundary function and through a function value in its body, by the callee as the body spells
  it, and the count of the index is the authority over the count of the text;
- a name of a boundary function stands only as the callee of a call or as an argument of a call
  of an `extern fn` or of a function pointer.
Two more tests hold the reach of a boundary function against the compiler, which refuses the
name of a boundary function in a module that is not trusted: one over a module of each kind of
parameter type, and one over each function of the standard library of the build.
"""

import argparse
import ast
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


OPTIONS = None

# A line of the lint: `<file>:<line>:<col>: <message>`.
PROBLEM = re.compile(r"^(?P<file>[^:\s]+):(?P<line>\d+):(?P<col>\d+): (?P<message>.*)$")
# The message of the compiler for a name of a boundary function in a module that is not trusted.
REFUSED = re.compile(r"'(std\.[A-Za-z0-9_.]+|[A-Za-z0-9_]+\.[A-Za-z0-9_]+)' is a boundary function")


def lint(root, files):
    """Run the lint over `files`, paths below `root`. Returns (status, output, problems), where
    problems are (file, line, message) of D9.8."""
    argv = [sys.executable, os.path.join(OPTIONS.source, "tools", "fort_lint.py"), "--fort",
            OPTIONS.fort, "--root", root] + files
    proc = subprocess.run(argv, cwd=root, capture_output=True, text=True)
    output = proc.stdout + proc.stderr
    problems = []
    for line in proc.stdout.splitlines():
        found = PROBLEM.match(line)
        if found is not None:
            problems.append((found.group("file"), int(found.group("line")),
                             found.group("message")))
    return proc.returncode, output, problems


def write(root, name, text):
    path = os.path.join(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    return name


# A declaration row that holds each point once.
ROW = """// C: labs, ISO C11 7.22.6.1, labs(3).
// gives: a long and no pointer. writes: no fort storage. keeps: no pointer.
// calls: no fort function. frees: nothing.
"""


def lines_of(text, needle):
    """The 1-based lines of `text` that hold `needle`."""
    return [i + 1 for i, line in enumerate(text.split("\n")) if needle in line]


def grammar_words():
    """The keywords and the reserved words of spec/grammar.md, as two lists."""
    with open(os.path.join(OPTIONS.source, "spec", "grammar.md"), encoding="utf-8") as handle:
        grammar = handle.read()
    words = []
    for rule in ("keyword", "reserved"):
        body = re.search(r"^%s\s+=(.*?);" % rule, grammar, re.MULTILINE | re.DOTALL).group(1)
        words.append(re.findall(r'"([a-z0-9]+)"', body))
    return words


class Rules(unittest.TestCase):
    def check(self, files, expected):
        """Write `files` (name to text), lint them, and compare the D9.8 problems with
        `expected`, a list of (file, line, part of the message)."""
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            names = [write(root, name, text) for name, text in files.items()]
            status, output, problems = lint(root, names)
            got = sorted((f, line, message) for f, line, message in problems)
            self.assertEqual(len(got), len(expected), output)
            for (f, line, message), (want_file, want_line, part) in zip(
                    got, sorted(expected)):
                self.assertEqual((f, line), (want_file, want_line), output)
                self.assertIn(part, message, output)
            self.assertEqual(status, 1 if expected else 0, output)
            return output

    def check_raw(self, files):
        """Write `files` (name to text), lint them, and return the output; the lint must fail."""
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            names = [write(root, name, text) for name, text in files.items()]
            status, output, _ = lint(root, names)
            self.assertEqual(status, 1, output)
            return output

    def test_a_declaration_and_a_call_with_their_rows_pass(self):
        text = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: no pointer argument.\n"
                "fn absolute(i64 v) i64 {\n    return labs(v);\n}\n")
        output = self.check({"good.ft": text}, [])
        self.assertIn("no violation of D1.4 or D9.8", output)

    def test_a_declaration_with_no_row_is_reported(self):
        text = "import extern;\n\n// Gives |v|.\nextern fn labs(i64 v) i64;\n"
        self.check({"bare.ft": text}, [("bare.ft", 4, "extern 'labs' has no declaration row")])

    def test_a_row_below_the_doc_text_is_the_row(self):
        text = "import extern;\n\n/// Gives |v|.\n" + ROW + "extern fn labs(i64 v) i64;\n"
        self.check({"doc.ft": text}, [])

    def test_a_row_above_doc_text_is_no_row(self):
        text = "import extern;\n\n" + ROW + "/// Gives |v|.\nextern fn labs(i64 v) i64;\n"
        self.check({"above.ft": text}, [("above.ft", 7, "has no declaration row")])

    def test_a_row_that_names_another_symbol_is_reported(self):
        text = "import extern;\n\n" + ROW.replace("C: labs", "C: llabs") + \
            "extern fn labs(i64 v) i64;\n"
        self.check({"other.ft": text},
                   [("other.ft", 3, "the declaration row of extern 'labs' names 'llabs'")])

    def test_a_row_with_no_source_is_reported(self):
        text = "import extern;\n\n" + ROW.replace("labs, ISO C11 7.22.6.1, labs(3).", "labs") + \
            "extern fn labs(i64 v) i64;\n"
        self.check({"nosource.ft": text},
                   [("nosource.ft", 3, "the declaration row of extern 'labs' names no symbol")])

    def test_a_row_without_each_word_once_is_reported(self):
        missing = ROW.replace("keeps: no pointer.", "no pointer.")
        twice = ROW.replace("frees: nothing.", "frees: nothing. gives: no pointer.")
        files = {"missing.ft": "import extern;\n\n" + missing + "extern fn labs(i64 v) i64;\n",
                 "twice.ft": "import extern;\n\n" + twice + "extern fn labs(i64 v) i64;\n"}
        self.check(files, [("missing.ft", 3, "gives 'keeps:' 0 times"),
                           ("twice.ft", 3, "gives 'gives:' 2 times")])

    def test_two_declaration_rows_are_reported(self):
        text = "import extern;\n\n" + ROW + ROW + "extern fn labs(i64 v) i64;\n"
        self.check({"two.ft": text}, [("two.ft", 6, "has 2 declaration rows")])

    def test_a_module_that_is_not_trusted_needs_no_row(self):
        # A module that is not trusted declares no extern (D9.8 T1), and its functions with a
        # `char*` parameter are no boundary functions, so a call of one needs no row and a
        # value of one goes where the module puts it.
        text = ("fn first(char* p) char {\n    return *p;\n}\n\n"
                "fn use(char* p) char {\n    return first(p);\n}\n\n"
                "fn (char*) char KEEP = first;\n")
        self.check({"plain.ft": text}, [])

    def test_a_call_with_no_row_is_reported(self):
        text = ("import extern;\nimport std.str;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "fn absolute(i64 v) i64 {\n    return labs(v);\n}\n\n"
                "fn first(char* p) char {\n    return *p;\n}\n\n"
                "fn both(char* p) u64 {\n"
                "    return cast(first(p), u64) + str.from_cstr(p).len;\n}\n")
        self.check({"calls.ft": text}, [
            ("calls.ft", 9, "fn 'absolute' has 1 call(s) of 'labs' and no call row"),
            ("calls.ft", 17, "fn 'both' has 1 call(s) of 'first' and no call row"),
            ("calls.ft", 17, "fn 'both' has 1 call(s) of 'str.from_cstr' and no call row")])

    def test_the_rows_count_the_calls_of_each_callee(self):
        body = "fn twice(i64 v) i64 {\n    return labs(v) + labs(labs(v));\n}\n"
        good = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: no pointer argument.\n// C call labs x2: as above.\n" + body)
        short = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                 "// C call labs x2: no pointer argument.\n" + body)
        more = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs x4: no pointer argument.\n" + body)
        self.check({"good.ft": good, "short.ft": short, "more.ft": more}, [
            ("short.ft", 8, "the call rows of fn 'twice' count 2 call(s) of 'labs', and its "
             "body holds 3"),
            ("more.ft", 8, "the call rows of fn 'twice' count 4 call(s) of 'labs', and its "
             "body holds 3")])

    def test_a_row_for_no_call_is_reported(self):
        text = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: no pointer argument.\nfn none(i64 v) i64 {\n    return v;\n}\n")
        self.check({"stale.ft": text},
                   [("stale.ft", 8, "count 1 call(s) of 'labs', and its body holds 0")])

    def test_a_row_that_does_not_parse_is_reported(self):
        text = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs x0: no pointer argument.\n// C call labs: no pointer argument.\n"
                "fn absolute(i64 v) i64 {\n    return labs(v);\n}\n")
        self.check({"parse.ft": text},
                   [("parse.ft", 8, "a call row of fn 'absolute' does not read as")])

    def test_the_callee_is_spelled_as_the_body_spells_it(self):
        # `libc.strlen`, not `strlen`: the row of a qualified call names its module path.
        text = ("import extern;\nimport std.libc;\n\n"
                "// C call strlen: the NUL of a string literal.\n"
                "fn size() u64 {\n    return libc.strlen(&NAME[0]);\n}\n\n"
                "string NAME = \"ab\";\n")
        self.check({"spell.ft": text}, [
            ("spell.ft", 5, "fn 'size' has 1 call(s) of 'libc.strlen' and no call row"),
            ("spell.ft", 4, "count 1 call(s) of 'strlen', and its body holds 0")])

    def test_a_call_through_a_function_pointer_counts_by_its_text(self):
        # The lint does not tell a function pointer that C gave from one of fort code, so a row
        # for one counts the calls that the body spells.
        good = ("import extern;\n\n// C call cb x2: a function pointer that C gave.\n"
                "fn run(fn (i64) i64 cb) i64 {\n    return cb(1) + cb(2);\n}\n")
        bad = ("import extern;\n\n// C call cb: a function pointer that C gave.\n"
               "fn run(fn (i64) i64 cb) i64 {\n    return cb(1) + cb(2);\n}\n")
        self.check({"good.ft": good, "bad.ft": bad},
                   [("bad.ft", 3, "count 1 call(s) of 'cb', and its body holds 2")])

    def test_a_boundary_function_as_a_value_is_reported(self):
        text = ("import extern;\nimport std.str;\n\n"
                "fn (char*) string KEEP = str.from_cstr;\n\n"
                "struct holder {\n    fn (char*) string f;\n}\n\n"
                "fn give() fn (char*) string {\n    return str.from_cstr;\n}\n\n"
                "fn keep(fn (char*) string f) void {\n}\n\n"
                "fn store() holder {\n    keep(str.from_cstr);\n"
                "    fn (char*) string local = str.from_cstr;\n"
                "    holder mut h = holder{str.from_cstr};\n"
                "    h.f = str.from_cstr;\n"
                "    return holder{local};\n}\n")
        message = "boundary function 'str.from_cstr' goes here as a value"
        self.check({"values.ft": text}, [
            ("values.ft", 4, message), ("values.ft", 11, message), ("values.ft", 18, message),
            ("values.ft", 19, message), ("values.ft", 20, message), ("values.ft", 21, message),
            ("values.ft", 17, "fn 'store' has 1 call(s) of 'keep' and no call row")])

    def test_a_boundary_function_that_goes_to_c_is_no_value(self):
        text = ("import extern;\nimport std.str.from_cstr;\n\n"
                "fn size(char* p) u64 {\n    return 0;\n}\n\n"
                "// C: take_cb, a C function of the test, which calls cb with a C string.\n"
                "// gives: no pointer. writes: no fort storage. keeps: no pointer.\n"
                "// calls: cb, with a C string. frees: nothing.\n"
                "extern fn take_cb(fn (char*) u64 cb) void;\n\n"
                "// C call take_cb: size is a boundary function, which goes to C.\n"
                "fn to_c() void {\n    take_cb(size);\n}\n\n"
                "// C call cb: a function pointer that C gave.\n"
                "fn through(fn (fn (char*) u64) void cb) void {\n    cb(size);\n}\n")
        self.check({"to_c.ft": text}, [])

    def test_a_struct_field_and_a_function_type_reach_a_byte_pointer(self):
        text = ("import extern;\n\n"
                "struct holder {\n    u64 len;\n    u8* data;\n}\n\n"
                "struct outer {\n    holder inner;\n}\n\n"
                "fn by_field(outer* o) u64 {\n    return o->inner.len;\n}\n\n"
                "fn by_callback(fn () char* cb) void {\n}\n\n"
                "fn by_bytes(u8@ b, string s, char[4]* a) u64 {\n    return b.len + s.len;\n}\n\n"
                "fn use(outer* o, u8@ b) u64 {\n    by_callback(null);\n"
                "    return by_field(o) + by_bytes(b, \"\", null);\n}\n")
        self.check({"reach.ft": text}, [
            ("reach.ft", 23, "fn 'use' has 1 call(s) of 'by_callback' and no call row"),
            ("reach.ft", 23, "fn 'use' has 1 call(s) of 'by_field' and no call row")])

    def test_a_row_with_a_word_twice_in_two_cases_is_reported(self):
        row = ROW.replace("frees: nothing.", "frees: nothing. Gives: a long.")
        text = "import extern;\n\n" + row + "extern fn labs(i64 v) i64;\n"
        self.check({"case.ft": text}, [("case.ft", 3, "gives 'gives:' 2 times")])

    def test_a_call_through_a_function_value_needs_a_row(self):
        # The lint does not tell a function pointer that C gave from one of fort code, and a
        # trusted module that gives a boundary function to fort code gives it to a module that
        # is not trusted, so each call through a function value has a row (D9.8). The calls take
        # a local, a parameter, a global, a constant, a field, an element and a result of a call
        # as their callee.
        files = {"sink.ft": SINK, "gate.ft": gate(rows=False)}
        text = files["gate.ft"]
        message = "has 1 call(s) through the function value '%s' and no call row"
        self.check(files, [
            ("gate.ft", lines_of(text, "fn by_" + name + "(")[0], message % callee)
            for name, callee in VALUE_CALLEES])

    def test_a_call_through_a_function_value_with_its_row_passes(self):
        # With a row, the boundary function `size` goes to a function pointer that C gave as an
        # argument, the way it goes to C, and the lint reports no value.
        self.check({"sink.ft": SINK, "gate.ft": gate(rows=True)}, [])

    def test_the_reviewed_probes_of_a_function_value_are_rejected(self):
        # The shapes of a review: str.from_cstr through a local that holds a function of a
        # module that is not trusted, and through an element of a table of them.
        local = ("import extern;\nimport std.str;\nimport sink2;\n\n"
                 "fn hand_out() void {\n"
                 "    fn (fn (char*) string) void k = sink2.keep;\n"
                 "    k(str.from_cstr);\n}\n")
        table = ("import extern;\nimport std.str;\nimport sink2;\n\n"
                 "fn by_table() void {\n"
                 "    (fn (fn (char*) string) void)[1] table = {sink2.keep};\n"
                 "    table[0](str.from_cstr);\n}\n")
        sink = "fn keep(fn (char*) string f) void {\n}\n"
        self.check({"sink2.ft": sink, "local.ft": local, "table.ft": table}, [
            ("local.ft", 5, "fn 'hand_out' has 1 call(s) through the function value 'k'"),
            ("table.ft", 5, "fn 'by_table' has 1 call(s) through the function value 'table[0]'")])

    def test_a_module_that_is_not_trusted_needs_no_row_for_a_function_value(self):
        text = ("fn apply(fn (char*) char f, char* p) char {\n    return f(p);\n}\n\n"
                "fn first(char* p) char {\n    return *p;\n}\n\n"
                "fn use(char* p) char {\n    return apply(first, p);\n}\n")
        self.check({"plain_values.ft": text}, [])

    def test_a_boundary_function_in_an_operand_of_an_argument_is_a_value(self):
        # `size == null` is an operand, not the whole argument, so the name goes nowhere C
        # takes it; the lint reports it, as D9.8 asks for a whole argument.
        text = ("import extern;\n\nfn size(char* p) u64 {\n    return 0;\n}\n\n"
                "// C: take_flag, a C function of the test.\n"
                "// gives: no pointer. writes: no fort storage. keeps: no pointer.\n"
                "// calls: no fort function. frees: nothing.\n"
                "extern fn take_flag(bool b) void;\n\n"
                "// C call take_flag: no pointer argument.\n"
                "fn test() void {\n    take_flag(size == null);\n}\n")
        self.check({"operand.ft": text}, [
            ("operand.ft", 14, "boundary function 'size' goes here as a value")])

    def test_a_call_counts_after_a_range_an_operator_and_white_space(self):
        # The text of a call can stand after `..` or `>`, and it can hold white space; each of
        # the three calls of libc.strlen counts once.
        text = ("import extern;\nimport std.libc;\n\n"
                "// C call libc.strlen x3: &NAME[0], a string literal through a global with its "
                "NUL.\n"
                "fn sizes() u64 {\n    char[3] buf = {'a', 'b', '\\0'};\n"
                "    u8@ part = cast(buf[0..libc.strlen(&NAME[0])], u8@);\n"
                "    bool longer = 9>libc.strlen(&NAME[0]);\n"
                "    return part.len + libc . strlen (&NAME[0]);\n}\n\n"
                "string NAME = \"ab\";\n")
        self.check({"spaces.ft": text}, [])

    def test_a_callee_with_white_space_counts_each_of_its_calls(self):
        # A review gave str.from_cstr to fort code through `table[i + 1]`, and a second call
        # `table[i+1]` with another argument took the one row. Both calls are one callee.
        sink = ("(fn (char*) string)[1] mut kept = {};\n\n"
                "fn keep(fn (char*) string f) u64 {\n    kept[0] = f;\n    return 0;\n}\n\n"
                "fn empty(char* p) string {\n    return \"\";\n}\n")
        gate_text = ("import extern;\nimport std.str;\nimport sink3;\n\n"
                     "(fn (fn (char*) string) u64)[2] mut table = {sink3.keep, sink3.keep};\n\n"
                     "// C call table[i+1]: a function of fort code; its argument is sink3.empty.\n"
                     "fn hidden(u64 i) u64 {\n    u64 a = table[i+1](sink3.empty);\n"
                     "    return a + table[i + 1](str.from_cstr);\n}\n")
        self.check({"sink3.ft": sink, "hidden.ft": gate_text}, [
            ("hidden.ft", 7, "the call rows of fn 'hidden' count 1 call(s) of 'table[i+1]', and "
             "its body holds 2")])

    def test_a_callee_in_an_expression_or_a_group_needs_its_row(self):
        body = ("fn shift((fn (i64) i64)[4] t, u64 i) i64 {\n    return t[i + 1 - 1](1);\n}\n\n"
                "fn group(fn (i64) i64 k) i64 {\n    return (k)(2);\n}\n\n"
                "fn later(fn (i64) void k) void {\n    defer (k)(3);\n}\n\n"
                "fn grid((fn (i64) i64)[2][2] t) i64 {\n    return t[0][1](4);\n}\n")
        rowed = ("import extern;\n\n// C call t[i+1-1]: a function of fort code.\n"
                 + body.replace("fn group(", "// C call (k): a function of fort code.\nfn group(")
                 .replace("fn later(", "// C call (k): a function of fort code.\nfn later(")
                 .replace("fn grid(", "// C call t[0][1]: a function of fort code.\nfn grid("))
        bare = "import extern;\n\n" + body
        self.check({"rowed.ft": rowed, "bare.ft": bare}, [
            ("bare.ft", 3, "through the function value 't[i+1-1]' and no call row"),
            ("bare.ft", 7, "through the function value '(k)' and no call row"),
            ("bare.ft", 11, "through the function value '(k)' and no call row"),
            ("bare.ft", 15, "through the function value 't[0][1]' and no call row")])

    def test_an_import_alias_is_no_function_and_no_extern(self):
        # An `as` alias of an import declares a name in this module and binds a declaration of
        # another (D20.3). A row above the import line covers no function, and an alias of an
        # extern needs no declaration row.
        helper = "fn twice(i64 x) i64 {\n    return x * 2;\n}\n"
        above = ("import extern;\nimport std.libc;\nimport std.str;\n"
                 "// C call libc.strlen: anything at all.\nimport helper.twice as double;\n\n"
                 "fn first(char* p) u64 {\n    return libc.strlen(p);\n}\n")
        below = ("import extern;\nimport std.libc;\nimport helper.twice as double;\n\n"
                 "// C call libc.strlen: p is a parameter source of the boundary function "
                 "first.\nfn first(char* p) u64 {\n    return libc.strlen(p);\n}\n")
        extern = ("import extern;\nimport std.libc.strlen as sl;\n\n"
                  "// C call sl: p is a parameter source of the boundary function first.\n"
                  "fn first(char* p) u64 {\n    return sl(p);\n}\n")
        self.check({"helper.ft": helper, "gate2.ft": above, "gate3.ft": below,
                    "gate4.ft": extern},
                   [("gate2.ft", 7, "fn 'first' has 1 call(s) of 'libc.strlen' and no call row")])

    def test_a_struct_is_its_declaration_and_not_its_name(self):
        # Two modules declare a struct `s`; only the one of module `a` holds a byte pointer.
        files = {
            "a.ft": "struct s {\n    u8* p;\n}\n",
            "b.ft": "struct s {\n    i64 x;\n}\n",
            "c.ft": ("import extern;\nimport a;\nimport b;\n\n"
                     "fn by_b(b.s* x) void {\n}\n\n"
                     "fn by_a(a.s* x) void {\n}\n\n"
                     "fn use(a.s* x, b.s* y) void {\n    by_b(y);\n    by_a(x);\n}\n"),
        }
        self.check(files, [("c.ft", 11, "fn 'use' has 1 call(s) of 'by_a' and no call row")])

    def test_the_rows_count_the_calls_of_each_branch_of_a_body(self):
        # A `$if` of a body selects one branch on each target, and the index records that
        # branch only. A row counts the calls of the text, so it holds on each target.
        body = ("fn either() i64 {\n"
                "    $if ($cfg(target_os) == \"linux\") {\n        return labs(1);\n"
                "    } else {\n        return labs(2) + labs(3);\n    }\n}\n")
        head = "import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
        files = {"both.ft": head + "// C call labs x3: no pointer argument.\n" + body,
                 "one.ft": head + "// C call labs x1: no pointer argument.\n" + body}
        self.check(files, [("one.ft", 8, "count 1 call(s) of 'labs', and its body holds 3")])

    def test_a_language_test_gets_the_rows_and_no_names(self):
        # A file of test/lang/run or test/lang/programs gets the checks of D9.8 and no check of
        # D1.4: its module name, its struct name and its long line pass, and its extern with no
        # row is reported. The same text in another tree gets each check.
        text = ("import extern;\n\n// " + "x" * 100 + "\nstruct Bad_Name {\n    i64 v;\n}\n\n"
                "extern fn labs(i64 v) i64;\n")
        files = {"test/lang/run/ffi/001_bad.ft": text,
                 "test/lang/programs/Bad.ft": text,
                 "other/001_bad.ft": text}
        self.check(files, [
            ("other/001_bad.ft", 1, "module '001_bad' is not lower_case"),
            ("other/001_bad.ft", 3, "line is 103 columns"),
            ("other/001_bad.ft", 4, "struct 'Bad_Name' is not lower_case"),
            ("other/001_bad.ft", 8, "extern 'labs' has no declaration row"),
            ("test/lang/programs/Bad.ft", 8, "extern 'labs' has no declaration row"),
            ("test/lang/run/ffi/001_bad.ft", 8, "extern 'labs' has no declaration row")])

    def test_a_language_test_keeps_the_errors_of_the_compiler(self):
        # A language test gets no check of D1.4, but a closure that the compiler cannot check
        # is still a problem: the rows of a file with no index cannot be read.
        text = "fn main() i32 {\n    return missing;\n}\n"
        output = self.check_raw({"test/lang/run/x/001_broken.ft": text})
        self.assertIn("test/lang/run/x/001_broken.ft:1:1:", output)
        self.assertIn("missing", output)

    def test_a_language_test_takes_the_cfg_options_of_its_flags(self):
        # The `--cfg` options of `//! flags:` reach the index, so a `$if` on a key of the test
        # compiles. A module of a directory test takes the options of the main.ft beside it.
        main = ("//! run\n//! flags: --release --cfg mode=good\n"
                "import extern;\nimport dep;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: no pointer argument.\n"
                "fn main() i32 {\n    $if ($cfg(mode) == \"good\") {\n"
                "        return cast(labs(cast(dep.KEY.len, i64)), i32);\n    }\n}\n")
        dep = "string KEY = $cfg(mode);\n"
        self.check({"test/lang/run/modules/keyed/main.ft": main,
                    "test/lang/run/modules/keyed/dep.ft": dep}, [])
        output = self.check_raw({"elsewhere/keyed/main.ft": main,
                                 "elsewhere/keyed/dep.ft": dep})
        self.assertIn("configuration key 'mode' is not set", output)

    def test_a_citation_names_a_callee_whose_row_holds_the_word(self):
        # A call row may cite the word of the row of another trusted function (D9.8 rule 3): the
        # body calls that callee, and its row holds a line that starts with the word.
        maker = ("import extern;\n\n/// Gives a value.\n// gives: 4 zero bytes, never null.\n"
                 "fn make() u64 {\n    return 4;\n}\n")
        user = ("import extern;\nimport maker;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: the gives: of maker.make states 4 bytes.\n"
                "fn use() i64 {\n    return labs(cast(maker.make(), i64));\n}\n")
        self.check({"maker.ft": maker, "user.ft": user}, [])
        no_row = maker.replace("// gives: 4 zero bytes, never null.\n", "")
        cite = lines_of(user, "// C call labs")[0]
        self.check({"maker.ft": no_row, "user.ft": user},
                   [("user.ft", cite, "cites the gives: of 'maker.make', and the row of "
                     "'maker.make' has no line '// gives: ...'")])
        other_word = maker.replace("// gives:", "// writes:")
        self.check({"maker.ft": other_word, "user.ft": user},
                   [("user.ft", cite, "has no line '// gives: ...'")])
        wrapped = user.replace("the gives: of maker.make states 4 bytes.",
                               "the writes: of\n// maker.make states 4 bytes.")
        self.check({"maker.ft": maker, "user.ft": wrapped},
                   [("user.ft", cite, "cites the writes: of 'maker.make'")])
        not_called = user.replace("maker.make states", "maker.other states")
        self.check({"maker.ft": maker, "user.ft": not_called},
                   [("user.ft", cite, "cites the gives: of 'maker.other', which its body does not "
                     "call")])

    def test_a_citation_names_a_callee_that_its_own_body_calls(self):
        # The body of the function that holds the citing row calls the callee: a call in another
        # function of the same file is no call of that body.
        maker = ("import extern;\n\n/// Gives a value.\n// gives: 4 zero bytes, never null.\n"
                 "fn make() u64 {\n    return 4;\n}\n")
        user = ("import extern;\nimport maker;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: the gives: of maker.make states 4 bytes.\n"
                "fn use() i64 {\n    return labs(4) + other();\n}\n\n"
                "fn other() i64 {\n    return cast(maker.make(), i64);\n}\n")
        cite = lines_of(user, "// C call labs")[0]
        self.check({"maker.ft": maker, "user.ft": user},
                   [("user.ft", cite, "cites the gives: of 'maker.make', which its body does not "
                     "call")])
        # The same call before the function, and not after it.
        before = user.replace("fn other() i64 {\n    return cast(maker.make(), i64);\n}\n", "")
        before = before.replace("// C call labs:", "fn other() i64 {\n    return cast(maker.make("
                                "), i64);\n}\n\n// C call labs:")
        self.check({"maker.ft": maker, "user.ft": before},
                   [("user.ft", lines_of(before, "// C call labs")[0],
                     "cites the gives: of 'maker.make', which its body does not call")])

    def test_a_citation_of_an_extern_is_reported_as_one(self):
        # The citation form of rule 3 names a fort function. The body calls the extern, so the
        # message does not say that the body does not call it.
        user = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs x2: the gives: of labs states a long.\n"
                "fn use() i64 {\n    return labs(labs(4));\n}\n")
        cite = lines_of(user, "// C call labs")[0]
        output = self.check({"user.ft": user}, [
            ("user.ft", cite, "cites the gives: of 'labs', which is an `extern fn`")])
        self.assertNotIn("does not call", output)
        # The citation names the extern as the body spells it, with its module path.
        path = ("import extern;\nimport std.libc;\n\n"
                "// C call libc.strlen: a literal; the gives: of libc.strlen states a count.\n"
                "fn use() u64 {\n    return libc.strlen(&\"a\"[0]);\n}\n")
        output = self.check({"user.ft": path}, [
            ("user.ft", lines_of(path, "// C call libc.strlen")[0],
             "cites the gives: of 'libc.strlen', which is an `extern fn`")])
        self.assertNotIn("does not call", output)
        # An extern that the body does not call keeps the message of a callee with no call.
        absent = user.replace("the gives: of labs states", "the gives: of llabs states")
        absent = absent.replace("extern fn labs(i64 v) i64;\n", "extern fn labs(i64 v) i64;\n"
                                + ROW.replace("labs", "llabs") + "extern fn llabs(i64 v) i64;\n")
        self.check({"user.ft": absent}, [
            ("user.ft", lines_of(absent, "// C call labs")[0],
             "cites the gives: of 'llabs', which its body does not call")])
        # A call of the extern in another function of the file is no call of this body.
        other = user.replace("return labs(labs(4));\n}\n", "return 4 + other();\n}\n\n"
                             "// C call labs x2: 4 is no pointer.\nfn other() i64 {\n"
                             "    return labs(labs(4));\n}\n")
        other = other.replace("// C call labs x2: the gives:", "// The gives:")
        self.check({"user.ft": other}, [
            ("user.ft", lines_of(other, "// The gives: of labs")[0],
             "cites the gives: of 'labs', which its body does not call")])

    def test_a_citation_of_a_function_of_a_module_that_is_not_trusted_is_reported(self):
        # Rule 3 trusts the row of a fort function of a trusted module only: no audit holds a
        # `// gives:` line in a module that is not trusted.
        maker = ("/// Gives a value.\n// gives: 4 zero bytes, never null.\n"
                 "fn make() u64 {\n    return 4;\n}\n")
        user = ("import extern;\nimport maker;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: the gives: of maker.make states 4 bytes.\n"
                "fn use() i64 {\n    return labs(cast(maker.make(), i64));\n}\n")
        cite = lines_of(user, "// C call labs")[0]
        self.check({"maker.ft": maker, "user.ft": user}, [
            ("user.ft", cite, "cites the gives: of 'maker.make', a function of a module that is "
             "not trusted")])
        # The same row of the same function passes when the module of the callee is trusted.
        self.check({"maker.ft": "import extern;\n\n" + maker, "user.ft": user}, [])

    def test_the_cited_rows_of_std_are_checked(self):
        # Each row of std that a row of test/lang cites: deleting its line, or its word, gives a
        # problem in the file that cites it, and in std.sys for the gives: of rt.args.
        cases = (("rt.ft", "fn decimal_text(", "// writes:", "test/lang/run/stdlib/095_rt_round_"
                  "trip_floats.ft", "rt.decimal_text"),
                 ("rt.ft", "fn alloc(", "// gives:", "test/lang/run/stdlib/070_rt_alloc_free.ft",
                  "rt.alloc"),
                 ("rt.ft", "fn alloc(", "// gives:", "test/lang/run/modes/014_rt_release.ft",
                  "rt.alloc"),
                 ("sys.ft", "fn args(", "// gives:", "test/lang/run/stdlib/019_sys_args_env.ft",
                  "sys.args"),
                 ("rt.ft", "fn args(", "// gives:", "std/sys.ft", "rt.args"))
        # A row of test/lang reads std from --std-dir, a copy of the std of the build; a row of
        # std reads std from the root, a copy of the std of the source tree.
        built = os.path.join(os.path.dirname(OPTIONS.fort), "std")
        for module, fn, word, citing, callee in cases:
            in_std = citing.startswith("std/")
            std = os.path.join(OPTIONS.source, "std") if in_std else built
            for spoil in ("line", "word"):
                with self.subTest(callee=callee, citing=citing, spoil=spoil), \
                        tempfile.TemporaryDirectory() as out:
                    copy = os.path.join(os.path.realpath(out), "std")
                    shutil.copytree(std, copy)
                    target = os.path.join(copy, module)
                    with open(target, encoding="utf-8") as handle:
                        lines = handle.read().split("\n")
                    at = [i for i, text in enumerate(lines) if text.startswith(fn)][0]
                    row = [i for i in range(at - 1, -1, -1) if lines[i].startswith(word)][0]
                    if spoil == "line":
                        del lines[row]
                    else:
                        lines[row] = lines[row].replace(word, word[:-1], 1)
                    with open(target, "w", encoding="utf-8") as handle:
                        handle.write("\n".join(lines))
                    root = os.path.dirname(copy) if in_std else OPTIONS.source
                    argv = [sys.executable, os.path.join(OPTIONS.source, "tools", "fort_lint.py"),
                            "--fort", OPTIONS.fort, "--root", root, os.path.join(root, citing)]
                    if not in_std:
                        argv[4:4] = ["--std-dir", copy]
                    else:
                        host = "darwin" if sys.platform == "darwin" else "linux"
                        argv[4:4] = ["--target", host]
                    proc = subprocess.run(argv, cwd=root, capture_output=True, text=True)
                    output = proc.stdout + proc.stderr
                    self.assertEqual(proc.returncode, 1, output)
                    self.assertIn("the row of '%s' has no line '// %s ...'" % (callee, word[3:]),
                                  output)

    def test_a_mut_global_that_reaches_a_byte_pointer_goes_to_c_only_from_a_boundary_function(
            self):
        # A value from a `mut` global is a parameter source (D9.8 rule 1), which stands only in a
        # boundary function. The lint sees the global by its name in the arguments of a call of
        # an extern fn or of a boundary function.
        head = ("import extern;\n\n// C: strlen, ISO C11 7.24.6.3, strlen(3).\n// gives: a count. "
                "writes: nothing. keeps: no pointer.\n// calls: no fort function. frees: nothing."
                "\nextern fn strlen(char* s) u64;\n\nchar* mut name = null;\nchar* FIXED = null;"
                "\ni32 mut count = 0;\nchar[4] mut buf = {};\n\n")
        plain = (head + "// C call strlen: name.\nfn plain() u64 {\n    return strlen(name);\n}\n"
                 "\n// C call strlen: &buf[0].\nfn nested() u64 {\n"
                 "    return strlen(cast(&buf[0], char*)) + cast(count, u64);\n}\n"
                 "\n// C call strlen: FIXED.\nfn steady() u64 {\n    return strlen(FIXED);\n}\n"
                 "\n// C call strlen: p is a parameter source.\nfn edge(char* p) u64 {\n"
                 "    return strlen(p) + strlen(name);\n}\n")
        plain = plain.replace("// C call strlen: p is a parameter source.",
                              "// C call strlen x2: p and name are parameter sources.")
        # `&buf[0]` is an address of the global, with the own bound of rule 2, and `char[4]` and
        # `i32` reach no byte pointer; `FIXED` is no `mut` global; `edge` is a boundary function.
        self.check({"globals.ft": plain}, [
            ("globals.ft", lines_of(plain, "return strlen(name);")[0],
             "fn 'plain' gives the `mut` global 'name'")])
        called = plain.replace("return strlen(name);", "return strlen(pick(name));").replace(
            "// C call strlen: name.", "// C call strlen: name.\n// C call pick: name.")
        called += "\nfn pick(char* p) char* {\n    return p;\n}\n"
        self.check({"globals.ft": called}, [
            ("globals.ft", lines_of(called, "return strlen(pick(name));")[0],
             "fn 'plain' gives the `mut` global 'name'")])

    # The head of a module with `mut` globals of each kind that the tests below name.
    GLOBALS = ("import extern;\nimport std.libc;\n\n// C: strlen, ISO C11 7.24.6.3, strlen(3).\n"
               "// gives: a count. writes: nothing. keeps: no pointer.\n"
               "// calls: no fort function. frees: nothing.\nextern fn strlen(char* s) u64;\n\n"
               "struct holder {\n    char* p;\n    u64 n;\n}\n\nchar* mut name = null;\n"
               "char*[2] mut names = {};\nu64 mut count = 0;\n\n")

    def test_a_mut_global_in_a_literal_or_an_index_of_an_argument_is_reported(self):
        # The search for the call of a name does not stop at the `{` of a struct or array
        # literal or at the `[` of an index: the name is still in the arguments of the call.
        take = ("// C call strlen: h.p is a parameter source.\nfn take(holder h) u64 {\n"
                "    return strlen(h.p);\n}\n\n")
        struct = (self.GLOBALS + take + "// C call take: a holder of name.\nfn f() u64 {\n"
                  "    return take(holder{.p = name, .n = 0});\n}\n")
        self.check({"globals.ft": struct}, [
            ("globals.ft", lines_of(struct, "return take(holder{")[0],
             "fn 'f' gives the `mut` global 'name'")])
        array = (self.GLOBALS + "// C call strlen: h[0].p is a parameter source.\n"
                 "fn take(holder[1] h) u64 {\n    return strlen(h[0].p);\n}\n\n"
                 "// C call take: a holder of name.\nfn f() u64 {\n"
                 "    return take(holder[1]{{.p = name, .n = 0}});\n}\n")
        self.check({"globals.ft": array}, [
            ("globals.ft", lines_of(array, "return take(holder[1]{{")[0],
             "fn 'f' gives the `mut` global 'name'")])
        index = (self.GLOBALS + "// C call strlen: an element of two.\nfn f() u64 {\n"
                 "    char*[2] mut two = {};\n    two[0] = &\"a\"[0];\n    two[1] = two[0];\n"
                 "    return strlen(two[names[1] == null ? 0 : 1]);\n}\n")
        self.check({"globals.ft": index}, [
            ("globals.ft", lines_of(index, "return strlen(two[")[0],
             "fn 'f' gives the `mut` global 'names'")])
        # A block is no argument list: a name in the block of an `if` whose condition calls C
        # stands in no call, and a statement ends the search.
        block = (self.GLOBALS + "// C call strlen: a literal with its NUL (rule 4).\n"
                 "fn f() char* {\n    if (strlen(&\"a\"[0]) > 0) {\n        return name;\n    }\n"
                 "    return names[0];\n}\n")
        self.check({"globals.ft": block}, [])

    def test_a_mut_global_is_searched_through_each_enclosing_call(self):
        # `cast(` is the innermost call of each name and it is no call of C: the search goes on
        # to the call of the extern that encloses it, once for each name.
        both = (self.GLOBALS + "// C call libc.memcmp: name twice, 0 bytes.\nfn f() i32 {\n"
                "    return libc.memcmp(cast(name, u8*), cast(name, u8*), 0);\n}\n")
        line = lines_of(both, "return libc.memcmp(")[0]
        self.check({"globals.ft": both}, [
            ("globals.ft", line, "fn 'f' gives the `mut` global 'name'"),
            ("globals.ft", line, "fn 'f' gives the `mut` global 'name'")])
        # A fort function that is no boundary function between the name and the extern.
        deep = (self.GLOBALS + "fn same(u64 v) u64 {\n    return v;\n}\n\n"
                "// C call strlen: an element of names.\nfn f() u64 {\n"
                "    return strlen(names[same(names[1] == null ? 0 : 1)]);\n}\n")
        line = lines_of(deep, "return strlen(names[same(")[0]
        self.check({"globals.ft": deep}, [
            ("globals.ft", line, "fn 'f' gives the `mut` global 'names'"),
            ("globals.ft", line, "fn 'f' gives the `mut` global 'names'")])

    def test_the_forms_of_a_mut_global_that_the_lint_does_not_see(self):
        # D9.8, "The lint", names these forms: the lint reads the name of the global between
        # the brackets of the call and nothing else. Each one is a parameter source by rule 1,
        # and a reviewer finds it. A change that closes one of them changes that text too.
        forms = {
            "a local copy": ("// C call strlen: p.\nfn f() u64 {\n    char* p = name;\n"
                             "    return strlen(p);\n}\n"),
            "the result of a function": ("fn get() char* {\n    return name;\n}\n\n"
                                         "// C call strlen: get().\nfn f() u64 {\n"
                                         "    return strlen(get());\n}\n"),
            "a length of another type": ("// C call libc.memcmp: a literal twice, count bytes.\n"
                                         "fn f() i32 {\n    return libc.memcmp(cast(&\"a\"[0], "
                                         "u8*), cast(&\"a\"[0], u8*), count);\n}\n"),
        }
        forms["a function pointer that C gave"] = (
            "// C: get_hook, a probe.\n// gives: a function pointer of C. writes: nothing. "
            "keeps: no pointer.\n// calls: no fort function. frees: nothing.\n"
            "extern fn get_hook() fn (char*) u64;\n\n// C call get_hook: no argument.\n"
            "// C call hook: a function pointer that C gave.\nfn f() u64 {\n"
            "    fn (char*) u64 hook = get_hook();\n    return hook(name);\n}\n")
        for form, body in forms.items():
            with self.subTest(form=form):
                self.check({"globals.ft": self.GLOBALS + body}, [])

    def test_a_callee_in_a_group_is_that_callee(self):
        # `(strlen)(name)` calls `strlen`. Its call row names the callee as the body spells it,
        # `(strlen)`, and the search for a `mut` global and a citation read through each group.
        take = ("// C call strlen: h.p is a parameter source.\nfn take(holder h) u64 {\n"
                "    return strlen(h.p);\n}\n\n")
        forms = (("(strlen)", "    return (strlen)(name);\n", "u64", ""),
                 ("((strlen))", "    return ((strlen))(name);\n", "u64", ""),
                 ("( strlen )", "    return ( strlen ) (name);\n", "u64", ""),
                 ("(libc.memcmp)", "    return (libc.memcmp)(cast(name, u8*), cast(&\"a\"[0], "
                  "u8*), 0);\n", "i32", ""),
                 ("(take)", "    return (take)(holder{.p = name, .n = 0});\n", "u64", take))
        for callee, body, result, before in forms:
            with self.subTest(callee=callee):
                module = (self.GLOBALS + before + "// C call %s: name.\nfn f() %s {\n%s}\n"
                          % (callee.replace(" ", ""), result, body))
                self.check({"globals.ft": module}, [
                    ("globals.ft", lines_of(module, body.strip())[0],
                     "fn 'f' gives the `mut` global 'name'")])
        # A citation of an extern that the body calls in a group: the message of an extern.
        user = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call (labs): the gives: of labs states a long.\n"
                "fn use() i64 {\n    return (labs)(4);\n}\n")
        output = self.check({"user.ft": user}, [
            ("user.ft", lines_of(user, "// C call (labs)")[0],
             "cites the gives: of 'labs', which is an `extern fn`")])
        self.assertNotIn("does not call", output)
        # A citation of a fort function that the body calls in a group stands.
        maker = ("import extern;\n\n/// Gives a value.\n// gives: 4 zero bytes, never null.\n"
                 "fn make() u64 {\n    return 4;\n}\n")
        user = ("import extern;\nimport maker;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C call labs: the gives: of maker.make states 4 bytes.\n"
                "// C call (maker.make): a function of fort code.\n"
                "fn use() i64 {\n    return labs(cast((maker.make)(), i64));\n}\n")
        self.check({"maker.ft": maker, "user.ft": user}, [])

    # The module that the programs of the callee forms import: externs, boundary functions and
    # a function that is no boundary function. The row of `takep` has no `// gives:` line.
    FORMS_MODULE = (
        "import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
        "// C: strlen, ISO C11 7.24.6.3, strlen(3).\n// gives: a count. writes: nothing. "
        "keeps: no pointer.\n// calls: no fort function. frees: nothing.\n"
        "extern fn strlen(char* s) u64;\n\n"
        "// C: take_cb, a C function of the test, which calls cb with a C string.\n"
        "// gives: no pointer. writes: nothing. keeps: no pointer.\n"
        "// calls: cb, with a C string. frees: nothing.\n"
        "extern fn take_cb(fn (char*) u64 cb) u64;\n\n"
        "// C: get_hook, a C function of the test.\n// gives: a function pointer of C. "
        "writes: nothing. keeps: no pointer.\n// calls: no fort function. frees: nothing.\n"
        "extern fn get_hook() fn (char*) u64;\n\n"
        "// C call strlen: p is a parameter source.\nfn takep(char* p) u64 {\n"
        "    return strlen(p);\n}\n\n"
        "fn keep(u64 n, fn (char*) u64 g) u64 {\n    return g == null ? n : n + 1;\n}\n\n"
        "fn pick(fn (char*) u64 g) fn (char*) u64 {\n    return g;\n}\n")

    # The forms of a callee at a call: (the form, the text before the argument list, the callee
    # of the call row). `%s` takes a callee that is a name or a path. The plain form is first.
    FORMS = (
        ("plain", "%s", "%s"),
        ("a group", "(%s)", "(%s)"),
        ("two groups", "((%s))", "((%s))"),
        ("white space", "( %s ) ", "(%s)"),
        ("a comment and a new line before the list", "(%s) // a comment (\n        ", "(%s)"),
        ("comments in the group", "( // )\n        %s // (\n        )", "(%s)"),
        ("no group, with white space and a comment before the list", "%s // a comment (\n        ",
         "%s"),
    )

    # A struct with a field of each function type that a program calls through.
    HOOKS = ("struct hooks {\n    fn (i64) i64 g;\n    fn (fn (char*) u64) u64 cb;\n}\n\n")
    A_ROW = "// C call %(row)s: the row of the call.\n"

    # One program for each place of the lint that takes the callee of a call: (the site, as the
    # function of the lint and the reader that it calls; the two callees, a name and a path;
    # the text above the rows; the rows; the head of the function; the argument list; the
    # problems, as (a part of the line, a part of the message)). `%(row)s` is the callee of the
    # row and `%(name)s` is the callee with no group. Parts.test_each_place_takes_its_callee_
    # from_the_one_reader holds the sites against the lint.
    PLACES = (
        # A call through a function value needs its row.
        (("value_calls", "call_callee"), ("g", "h.g"), HOOKS, "",
         "fn f(fn (i64) i64 g, hooks h) i64", "(1)",
         [("fn f(", "fn 'f' has 1 call(s) through the function value '%(row)s' and no call row")]),
        # A boundary function as the callee of a call goes to no value.
        (("goes_to_c", "named_call"), ("takep", "m.takep"), "", A_ROW,
         "fn f() u64", "(&\"a\"[0])", []),
        # A citation names an extern that the body calls.
        (("row_problems", "named_call", "extern_calls"), ("labs", "m.labs"), "",
         "// C call %(row)s: the gives: of %(name)s states a long.\n", "fn f() i64", "(4)",
         [("// C call", "cites the gives: of '%(name)s', which is an `extern fn`")]),
        # A citation names a fort function that the body calls, whose row lacks the word.
        (("row_problems", "named_call", "fn_calls"), ("takep", "m.takep"), "",
         "// C call %(row)s: the gives: of %(name)s states a count.\n", "fn f() u64",
         "(&\"a\"[0])",
         [("// C call", "cites the gives: of '%(name)s', and the row of '%(name)s' has no line")]),
        # A boundary function as an argument: the callee is a fort function, an extern, a value.
        (("goes_to_c", "call_callee", "a fort function"), ("keep", "m.keep"), "", A_ROW,
         "fn f() u64", "(0, m.takep)",
         [("(0, m.takep)", "boundary function 'm.takep' goes here as a value")]),
        (("goes_to_c", "call_callee", "an extern"), ("take_cb", "m.take_cb"), "", A_ROW,
         "fn f() u64", "(m.takep)", []),
        (("goes_to_c", "call_callee", "a function value"), ("cb", "h.cb"), HOOKS, A_ROW,
         "fn f(fn (fn (char*) u64) u64 cb, hooks h) u64", "(m.takep)", []),
        # A `mut` global between the brackets of a call of an extern and of a boundary function.
        (("mut_global_to_c", "call_callee", "an extern"), ("strlen", "m.strlen"),
         "char* mut name = null;\n\n", A_ROW, "fn f() u64", "(name)",
         [("(name)", "fn 'f' gives the `mut` global 'name'")]),
        (("mut_global_to_c", "call_callee", "a boundary function"), ("takep", "m.takep"),
         "char* mut name = null;\n\n", A_ROW, "fn f() u64", "(name)",
         [("(name)", "fn 'f' gives the `mut` global 'name'")]),
        # The callee of a call inside the callee: the call is through its result, a function
        # pointer, and D9.8 names the `mut` global there as not seen.
        (("mut_global_to_c", "named_call"), ("pick", "m.pick"), "char* mut name = null;\n\n",
         "// C call m.get_hook: no argument.\n" + A_ROW +
         "// C call %(row)s(hook): a function pointer that C gave.\n",
         "fn f() u64", "(hook)(name)", []),
        # A call of an extern needs its row, and the rows count the calls that the body holds.
        (("row_problems", "named_call", "the count of a call"), ("labs", "m.labs"), "", "",
         "fn f() i64", "(4)",
         [("fn f(", "'%(row)s' and no call row for it: write '// C call %(row)s: ...'")]),
        (("text_calls", "call_callee"), ("labs", "m.labs"), "",
         "// C call %(row)s x2: no pointer argument.\n", "fn f() i64", "(4)",
         [("// C call", "count 2 call(s) of '%(row)s', and its body holds 1")]),
        # A citation names its callee as the body spells it, in the groups of the row.
        (("cited_callee", "call_callee"), ("takep", "m.takep"), "",
         "// C call %(row)s: the gives: of %(row)s states a count.\n", "fn f() u64",
         "(&\"a\"[0])",
         [("// C call", "cites the gives: of '%(name)s', and the row of '%(name)s' has no line")]),
    )

    def test_each_place_reads_a_callee_through_each_group(self):
        # D9.8, "The lint": the lint reads the callee of a call through each group around it.
        # Each place gives each form the problems of its plain form, with the callee of the row
        # as the body spells it: 13 programs, each with 2 callees in 7 forms, so 182 modules,
        # 156 with another form against 26 with the plain form. 5 of the forms have a group.
        # The last form has none: the place that counts a call of an extern or of a boundary
        # function reads that form only, and leaves a callee in a group to `value_calls`.
        cells = 0
        for site, callees, above, rows, head, arguments, wanted in self.PLACES:
            files = {"m.ft": self.FORMS_MODULE}
            expected = {}
            for callee in callees:
                for number, (form, before, row) in enumerate(self.FORMS):
                    spelled = callee.replace(".", " . ") if "white space" in form else callee
                    names = {"row": row % callee, "name": callee}
                    text = "import extern;\nimport m;\n"
                    if "." not in callee and not above.startswith("struct"):
                        text += "import m.%s;\n" % callee
                    text += "\n" + above + rows % names + head + " {\n"
                    if "hook" in arguments:
                        text += "    fn (char*) u64 hook = m.get_hook();\n"
                    text += "    return " + before % spelled + arguments + ";\n}\n"
                    name = "%s%d.ft" % ("path" if "." in callee else "name", number)
                    files[name] = text
                    expected[(name, form, callee)] = [
                        (lines_of(text, part)[0], message % names) for part, message in wanted]
                    cells += 1
            with tempfile.TemporaryDirectory() as out:
                root = os.path.realpath(out)
                status, output, problems = lint(root, [write(root, name, text)
                                                       for name, text in files.items()])
            self.assertEqual(status, 1 if wanted else 0, (site, output))
            for (name, form, callee), want in expected.items():
                with self.subTest(site=site, form=form, callee=callee):
                    got = sorted((line, message) for f, line, message in problems if f == name)
                    self.assertEqual([line for line, _ in got], [line for line, _ in want], got)
                    for (_, message), (_, part) in zip(got, want):
                        self.assertIn(part, message)
        self.assertEqual(len(self.PLACES), 13)
        self.assertEqual(len(self.FORMS), 7)
        self.assertEqual(cells, 182)

    def test_a_boundary_function_to_a_callee_in_a_group_is_judged_by_that_callee(self):
        # `(keep)(0, takep)` gives `takep` to `keep`, a fort function, as `keep(0, takep)`
        # does: a group around a callee makes no function pointer of it. The call in a group
        # needs its row as a function value too (D9.8, "The lint").
        other = "fn run(fn (char*) u64 g, u64 n) u64 {\n    return g == null ? n : n + 1;\n}\n"
        head = ("import extern;\nimport other;\n\n// C: strlen, ISO C11 7.24.6.3, strlen(3).\n"
                "// gives: a count. writes: nothing. keeps: no pointer.\n"
                "// calls: no fort function. frees: nothing.\nextern fn strlen(char* s) u64;\n\n"
                "// C call strlen: p is a parameter source.\nfn takep(char* p) u64 {\n"
                "    return strlen(p);\n}\n\n"
                "fn keep(u64 n, fn (char*) u64 g) u64 {\n    return g == null ? n : n + 1;\n}\n\n")
        value = "boundary function 'takep' goes here as a value"
        forms = (
            # (the rows, the call, the count of problems of a row)
            ("", "other.run(takep, 0)", 0),
            ("// C call (other.run): a function of fort code.\n", "(other.run)(takep, 0)", 0),
            ("// C call ((other.run)): a function of fort code.\n", "((other.run))(takep, 0)", 0),
            ("", "(other.run)(takep, 0)", 1),
            ("// C call keep: a function of fort code.\n", "keep(0, takep)", 0),
            ("// C call (keep): a function of fort code.\n", "(keep)(0, takep)", 0),
            ("// C call ((keep)): a function of fort code.\n", "( ( keep ) ) (0, takep)", 0),
        )
        for rows, call, row_problems in forms:
            with self.subTest(call=call, rows=rows):
                text = head + rows + "fn f() u64 {\n    return %s;\n}\n" % call
                expected = [("globals.ft", lines_of(text, call)[0], value)]
                if row_problems:
                    expected.append(("globals.ft", lines_of(text, "fn f()")[0],
                                     "through the function value '(other.run)' and no call row"))
                self.check({"other.ft": other, "globals.ft": text}, expected)

    def test_a_boundary_function_alone_in_a_group_is_a_value(self):
        # A group with no argument list after it is no call: the name in it is a value, as
        # the result of a function, in a local and as an argument of a fort function. The
        # list of a `cast` is the argument list of no callee, so the name in it is a value too.
        other = "fn run(fn (char*) u64 g, u64 n) u64 {\n    return g == null ? n : n + 1;\n}\n"
        head = ("import extern;\nimport other;\n\n// C: strlen, ISO C11 7.24.6.3, strlen(3).\n"
                "// gives: a count. writes: nothing. keeps: no pointer.\n"
                "// calls: no fort function. frees: nothing.\nextern fn strlen(char* s) u64;\n\n"
                "// C call strlen: p is a parameter source.\nfn takep(char* p) u64 {\n"
                "    return strlen(p);\n}\n\n")
        cast = "cast(takep, fn (char*) u64)"
        forms = (("fn (char*) u64", "    return (takep);\n"),
                 ("u64", "    fn (char*) u64 g = (takep);\n    return g == null ? 0 : 1;\n"),
                 ("u64", "    return other.run((takep), 0);\n"),
                 ("u64", "    return other.run(((takep)), 0);\n"),
                 ("u64", "    fn (char*) u64 g = %s;\n    return g == null ? 0 : 1;\n" % cast),
                 ("u64", "    return other.run(%s, 0);\n" % cast))
        for result, body in forms:
            with self.subTest(body=body):
                text = head + "fn f() %s {\n%s}\n" % (result, body)
                self.check({"other.ft": other, "globals.ft": text}, [
                    ("globals.ft", lines_of(text, "(takep")[0],
                     "boundary function 'takep' goes here as a value")])
        # The message names the function as one name, with its module path and no white space.
        user = ("import extern;\nimport globals;\nimport other;\n\nfn f() u64 {\n"
                "    return other.run(globals . takep, 0);\n}\n")
        self.check({"other.ft": other, "globals.ft": head, "user.ft": user}, [
            ("user.ft", lines_of(user, "globals . takep")[0],
             "boundary function 'globals.takep' goes here as a value")])

    def test_a_call_through_a_member_of_a_pointer_takes_its_whole_place(self):
        # `p->g(1)` is a call through the function value `p->g`, and its row names that place.
        text = ("import extern;\n\nstruct hooks {\n    fn (i64) i64 g;\n}\n\n"
                "// C call p->g: a function value of fort code.\n"
                "fn f(hooks* p) i64 {\n    return p->g(1);\n}\n")
        self.check({"member.ft": text}, [])
        bare = text.replace("// C call p->g: a function value of fort code.\n", "")
        self.check({"member.ft": bare}, [
            ("member.ft", lines_of(bare, "fn f(")[0],
             "fn 'f' has 1 call(s) through the function value 'p->g' and no call row")])

    def test_a_callee_in_two_groups_is_that_callee(self):
        # Each check reads a callee through each group, and not through the first group only.
        # A boundary function in two groups is the callee of its call and no value.
        head = ("import extern;\n\n" + ROW + "extern fn labs(i64 v) i64;\n\n"
                "// C: strlen, ISO C11 7.24.6.3, strlen(3).\n// gives: a count. writes: nothing. "
                "keeps: no pointer.\n// calls: no fort function. frees: nothing.\n"
                "extern fn strlen(char* s) u64;\n\n"
                "// C call strlen: p is a parameter source.\nfn takep(char* p) u64 {\n"
                "    return strlen(p);\n}\n\n")
        callee = (head + "// C call ((takep)): a literal with its NUL (rule 4).\n"
                  "fn f() u64 {\n    return ((takep))(&\"a\"[0]);\n}\n")
        self.check({"two.ft": callee}, [])
        # With no row, the call in two groups is the one problem, and the name is no value.
        bare = callee.replace("// C call ((takep)): a literal with its NUL (rule 4).\n", "")
        self.check({"two.ft": bare}, [
            ("two.ft", lines_of(bare, "fn f()")[0],
             "fn 'f' has 1 call(s) through the function value '((takep))' and no call row")])
        # A citation of an extern that the body calls in two groups: the message of an extern.
        cited = (head + "// C call ((labs)): the gives: of labs states a long.\n"
                 "fn f() i64 {\n    return ((labs))(4);\n}\n")
        self.check({"two.ft": cited}, [
            ("two.ft", lines_of(cited, "// C call ((labs))")[0],
             "cites the gives: of 'labs', which is an `extern fn`")])
        # A citation of a fort function that the body calls in two groups stands.
        stands = (head.replace("// C call strlen: p is a parameter source.\n",
                               "// C call strlen: p is a parameter source.\n"
                               "// gives: a count and no pointer.\n") +
                  "// C call ((takep)): the gives: of takep states a count.\n"
                  "fn f() u64 {\n    return ((takep))(&\"a\"[0]);\n}\n")
        self.check({"two.ft": stands}, [])

    def test_a_mut_global_to_a_callee_that_holds_a_boundary_function_is_reported(self):
        # A callee that is no name can hold a boundary function as a value, and the call can
        # reach it. The lint reports the `mut` global for each such callee. It reports each
        # boundary function there as a value too, and the call needs a row. The compiler
        # refuses an `extern fn` as a value, so no callee holds one.
        other = "fn plain(char* p) u64 {\n    return p == null ? 0 : 1;\n}\n"
        head = (self.GLOBALS.replace("import std.libc;\n", "import other;\n") +
                "// C call strlen: p is a parameter source.\nfn takep(char* p) u64 {\n"
                "    return strlen(p);\n}\n\n")
        value = "boundary function 'takep' goes here as a value"
        for first, second, values in (("takep", "other.plain", 1), ("other.plain", "takep", 1),
                                      ("takep", "takep", 2)):
            with self.subTest(first=first, second=second):
                call = "(count > 0 ? %s : %s)" % (first, second)
                module = (head + "// C call %s: a function value.\n"
                          "fn f() u64 {\n    return %s(name);\n}\n"
                          % (call.replace(" ", ""), call))
                line = lines_of(module, "    return " + call)[0]
                self.check({"other.ft": other, "globals.ft": module}, [
                    ("globals.ft", line, "fn 'f' gives the `mut` global 'name'")] +
                    [("globals.ft", line, value)] * values)
        # No boundary function in the callee: a call through a function value, which D9.8
        # names as not seen.
        module = (head + "// C call (count>0?other.plain:other.plain): a function value.\n"
                  "fn f() u64 {\n    return (count > 0 ? other.plain : other.plain)(name);\n}\n")
        self.check({"other.ft": other, "globals.ft": module}, [])
        # The callee of a call inside the callee: the call is through its result, a function
        # pointer that C gave, in each group form of that inner callee.
        hook = ("// C: get_hook, a probe.\n// gives: a function pointer of C. writes: nothing. "
                "keeps: no pointer.\n// calls: no fort function. frees: nothing.\n"
                "extern fn get_hook() fn (char*) u64;\n\n")
        for inner in ("get_hook", "(get_hook)", "((get_hook))"):
            with self.subTest(inner=inner):
                rows = "// C call %s: no argument.\n// C call %s(): C gave it.\n" % (inner, inner)
                module = (self.GLOBALS + hook + rows + "fn f() u64 {\n    return %s()(name);\n}\n"
                          % inner)
                self.check({"globals.ft": module}, [])

    def test_an_argument_in_a_group_is_that_argument(self):
        # `take_cb((takep))` gives what `take_cb(takep)` gives: a group around an argument
        # changes no verdict. 4 callees, 2 spellings of the argument and 5 forms of it: 40
        # modules. An extern and a function value take the boundary function to C, and a fort
        # function takes it as a value, as its last argument and as its first one.
        forms = ("%s", "(%s)", "((%s))", "( %s )", "( // (\n        %s // )\n        )")
        kinds = (
            ("m.take_cb(%s)", "// C call m.take_cb: C calls takep in the call.\n", "fn f() u64",
             False),
            ("cb(%s)", "// C call cb: a function pointer that C gave.\n",
             "fn f(fn (fn (char*) u64) u64 cb) u64", False),
            ("m.keep(0, %s)", "// C call m.keep: a function of fort code.\n", "fn f() u64", True),
            ("m.keep2(%s, 0)", "// C call m.keep2: a function of fort code.\n", "fn f() u64",
             True),
        )
        module = self.FORMS_MODULE + ("\nfn keep2(fn (char*) u64 g, u64 n) u64 {\n"
                                      "    return g == null ? n : n + 1;\n}\n")
        files = {"m.ft": module}
        expected = []
        for kind, (call, rows, head, value) in enumerate(kinds):
            for argument in ("takep", "m.takep"):
                for number, form in enumerate(forms):
                    spelled = form % (argument.replace(".", " . ") if " %s " in form else argument)
                    text = "import extern;\nimport m;\n"
                    if "." not in argument:
                        text += "import m.takep;\n"
                    text += "\n" + rows + head + " {\n    return " + call % spelled + ";\n}\n"
                    name = "k%d_%s%d.ft" % (kind, "path" if "." in argument else "name", number)
                    files[name] = text
                    if value:
                        expected.append((name, lines_of(text, argument.split(".")[-1])[-1],
                                         "boundary function '%s' goes here as a value" % argument))
        self.assertEqual((len(files) - 1, len(expected)), (40, 20))
        self.check(files, expected)

    # A trusted module with a function whose row holds each word and one whose row holds none.
    MAKER = ("import extern;\n\n/// Gives a value.\n// gives: 4, and no pointer.\n"
             "// writes: no storage.\nfn good() u64 {\n    return 4;\n}\n\n"
             "fn bare() u64 {\n    return 4;\n}\n")

    def test_a_citation_reads_in_each_form_that_the_lint_accepts(self):
        # D9.8 rule 3 writes a citation as `the gives: of <the callee>` or `the writes: of <the
        # callee>`, with the callee as the body spells it. The lint accepts 128 forms of that
        # text: `the` or `The`, 2 words, a space or a line wrap after the colon, a space or a
        # line wrap after `of`, the callee plain, in a group, in two groups and in a group with
        # white space, and the callee a name or a name with its module path. Each form cites
        # `good`, whose row holds each word, and `bare`, whose row holds none: 256 functions.
        text = "import extern;\nimport maker;\nimport maker.good;\nimport maker.bare;\n"
        expected = []
        forms = set()
        for the in ("the", "The"):
            for word in ("gives", "writes"):
                for first in (" ", "\n// "):
                    for second in (" ", "\n// "):
                        for group in ("%s", "(%s)", "((%s))", "( %s )"):
                            for path in ("", "maker."):
                                forms.add((the, word, first, second, group, path))
                                for name in ("good", "bare"):
                                    callee = path + name
                                    spelled = group % (callee.replace(".", " . ")
                                                       if " %s " in group else callee)
                                    line = text.count("\n") + 2
                                    text += ("\n// A row of text: %s %s:%sof%s%s states it.\n"
                                             "fn f%d() u64 {\n    return %s();\n}\n"
                                             % (the, word, first, second, spelled, line, callee))
                                    if name == "bare":
                                        expected.append(("user.ft", line, "cites the %s: of '%s', "
                                                         "and the row of '%s' has no line '// %s: "
                                                         "...'" % (word, callee, callee, word)))
        self.assertEqual((len(forms), len(expected)), (128, 128))
        self.check({"maker.ft": self.MAKER, "user.ft": text}, expected)

    def test_text_that_starts_a_citation_and_does_not_read_as_one_is_reported(self):
        # A row that a reviewer takes for a citation and that the lint does not read is a
        # problem, and never silence. Text starts a citation when it holds `the gives:`,
        # `the gives of` or `gives: of`, or the same with `writes`, in each case, with white
        # space and quote characters between the words. Each of the 48 rows below starts one
        # and is no form of the 128.
        unread = (
            "THE gives: of good.", "tHe writes: of good.", "the Gives: of good.",
            "the WRITES: of good.", "the gives : of good.", "the  gives: of good.",
            "the\tgives: of good.", "the gives:  of good.", "the gives: of  good.",
            "the gives:of good.", "the gives: Of good.", "the gives: of\tgood.",
            "the gives of good.", "The writes of good.", "the gives\n// of good.",
            "its gives: of good.", "Writes: of good.", "gives:of good.", "its gives : of good.",
            "the\n// gives: of good.", "the gives\n// : of good.", "the gives:\n//\n// of good.",
            "the gives: of", "the gives: of 3.", "the gives: of 4good.", "the gives: of (good.",
            "the gives: of ().", "the gives: of (good + good).", "the gives: of (good, good).",
            "the gives: of (good()).", "the gives: of (table[0]).", "the gives: of .good.",
            "the gives: of `good`.", "the gives: of 'good'.", "the gives: of $cfg.",
            "the gives: of <good>.", "the gives: for good.", "the gives: good.",
            "the gives: of ((good).", "the gives: of [good].",
            "the `gives:` of good.", "the 'writes:' of good.", "the \"gives:\" of good.",
            "the `gives: of` good.", "the `gives`: of good.", "the `gives` of good.",
            "`gives:` of good.", "The \"writes\" : 'of' good.",
        )
        text = "import extern;\nimport maker;\nimport maker.good;\n"
        expected = []
        for row in unread:
            line = text.count("\n") + 2
            text += ("\n// A row of text: %s\nfn f%d() u64 {\n    return good();\n}\n"
                     % (row, line))
            expected.append(("user.ft", line, "starts a citation that the lint cannot read"))
        self.assertEqual(len(expected), 48)
        self.check({"maker.ft": self.MAKER, "user.ft": text}, expected)
        # A citation that reads, of a callee that the body does not call: the callee is the
        # name that the reader takes, and the message names it.
        others = (("the gives: of table[0].", "table"), ("the gives: of the function good.", "the"),
                  ("the gives: of good.x.", "good.x"), ("the gives: of (p->good).", "p->good"),
                  ("the gives: of goody.", "goody"), ("the gives: of maker.bare.", "maker.bare"),
                  ("the gives: of (good)(1).", None), ("the gives: of good().", None),
                  ("The writes: of good, and the gives: of good.", None))
        text = "import extern;\nimport maker;\nimport maker.good;\n"
        expected = []
        for row, callee in others:
            line = text.count("\n") + 2
            text += ("\n// A row of text: %s\nfn f%d() u64 {\n    return good();\n}\n"
                     % (row, line))
            if callee is not None:
                expected.append(("user.ft", line, "cites the gives: of '%s', which its body does "
                                 "not call" % callee))
        self.check({"maker.ft": self.MAKER, "user.ft": text}, expected)
        # Text with one of the words that starts no citation is no problem.
        plain = ("the writes stay inside the buffer.", "a caller flushes between the writes. Then",
                 "it gives: a count.", "writes: nothing of its own.", "C gives of its own storage.",
                 "the row holds the `// gives:` line.", "what the function gives: 4 bytes.",
                 "the function gives or writes: nothing of note.", "bathe gives. writes of C.",
                 "the gives and the writes.", "the giver: of good.", "the gives_: of good.",
                 "the (gives:) of good.", "the gives, of good.", "gives: - of good.")
        text = "import extern;\nimport maker;\nimport maker.good;\n"
        for number, row in enumerate(plain):
            text += ("\n// A row of text: %s\nfn f%d() u64 {\n    return good();\n}\n"
                     % (row, number))
        self.check({"maker.ft": self.MAKER, "user.ft": text}, [])

    # A body that holds each keyword that fort code writes before a `(`, 11 of them: `fn`,
    # `cast`, `sizeof`, `defer`, `if`, `while`, `for`, `switch`, `case`, `new` and `return`.
    KEYWORD_BODY = (
        "    fn (u64) u64 g = same;\n    u64 mut total = cast(sizeof(u64), u64);\n"
        "    defer (none)();\n    if (n > 0) {\n        total += 1;\n    } else if (n > 1) {\n"
        "        total += 2;\n    }\n    while (total > 9) {\n        total -= 1;\n    }\n"
        "    do {\n        total += 1;\n    } while (total < 2);\n"
        "    for (u64 mut i = 0; i < n; i++) {\n        total += i;\n    }\n"
        "    switch (n) {\n        case (1):\n            total += 1;\n        default:\n"
        "            total += 2;\n    }\n    u64 mut* own q = new(u64);\n"
        "    *q = g == null ? 0 : 1;\n    total += *q;\n    del(q);\n"
        "    return (strlen(p) + total);\n")

    def test_a_keyword_is_the_callee_of_no_row_and_no_citation(self):
        # A keyword is no name, so it is the callee of no call: a call row that names one counts
        # a call that the body does not hold, and a citation that names one does not read. The
        # test names each of the 41 keywords and the 10 reserved words of spec/grammar.md in a
        # row and in a citation. The body holds 11 of them before a `(`.
        words = sorted(sum(grammar_words(), []))
        self.assertEqual(len(words), 51)
        held = [word for word in words
                if re.search(r"(?<![A-Za-z0-9_$])%s ?\(" % word, self.KEYWORD_BODY)]
        self.assertEqual(held, ["case", "cast", "defer", "fn", "for", "if", "new", "return",
                                "sizeof", "switch", "while"])
        head = ("import extern;\n\n// C: strlen, ISO C11 7.24.6.3, strlen(3).\n"
                "// gives: a count. writes: nothing. keeps: no pointer.\n"
                "// calls: no fort function. frees: nothing.\nextern fn strlen(char* s) u64;\n\n"
                "fn same(u64 v) u64 {\n    return v;\n}\n\nfn none() void {\n}\n\n")
        rows = ("// C call strlen: p is a parameter source.\n"
                "// C call (none): a function of fort code.\n")
        self.check({"kw.ft": head + rows + "fn f(char* p, u64 n) u64 {\n" + self.KEYWORD_BODY
                    + "}\n"}, [])
        text = (head + rows + "".join("// C call %s: a keyword.\n" % word for word in words)
                + "fn f(char* p, u64 n) u64 {\n" + self.KEYWORD_BODY + "}\n")
        self.check({"kw.ft": text}, [
            ("kw.ft", lines_of(text, "// C call %s: a keyword." % word)[0],
             "the call rows of fn 'f' count 1 call(s) of '%s', and its body holds 0" % word)
            for word in words])
        text = (head + rows + "".join("// It has the gives: of %s here.\n" % word
                                      for word in words)
                + "fn f(char* p, u64 n) u64 {\n" + self.KEYWORD_BODY + "}\n")
        self.check({"kw.ft": text}, [
            ("kw.ft", lines_of(text, "the gives: of %s here." % word)[0],
             "starts a citation that the lint cannot read") for word in words])

    def test_a_compiler_form_is_the_callee_of_no_row(self):
        # `$if (...)` and `$cfg(key)` are compiler forms (D21.1, D21.2) and no call: a call row
        # that names `if` or `cfg` counts a call that the body does not hold.
        main = ("//! run\n//! flags: --release --cfg mode=good\nimport extern;\n\n" + ROW +
                "extern fn labs(i64 v) i64;\n\n// C call labs: no pointer argument.\n%s"
                "fn main() i32 {\n    $if ($cfg(mode) == \"good\") {\n"
                "        return cast(labs(4), i32);\n    }\n}\n")
        self.check({"test/lang/run/x/001_form.ft": main % ""}, [])
        rows = main % "// C call cfg: a compiler form.\n// C call if: a compiler form.\n"
        self.check({"test/lang/run/x/001_form.ft": rows}, [
            ("test/lang/run/x/001_form.ft", lines_of(rows, "// C call %s:" % word)[0],
             "the call rows of fn 'main' count 1 call(s) of '%s', and its body holds 0" % word)
            for word in ("cfg", "if")])

    def test_a_name_between_the_brackets_of_a_keyword_goes_to_no_call(self):
        # The `(` after a keyword opens no argument list. A `mut` global there goes to no call
        # of C, and a boundary function there is a value.
        takep = ("// C call strlen: p is a parameter source.\nfn takep(char* p) u64 {\n"
                 "    return strlen(p);\n}\n\n")
        text = (self.GLOBALS + takep + "fn f() u64 {\n    if (name == null) {\n        return 0;\n"
                "    }\n    while (names[0] != null) {\n        names[0] = null;\n    }\n"
                "    u8* raw = cast(name, u8*);\n    switch (count) {\n        case (1):\n"
                "            count = 2;\n        default:\n            count = 3;\n    }\n"
                "    for (u64 mut i = 0; names[1] != null; i++) {\n        names[1] = null;\n"
                "    }\n"
                "    return (raw == null ? cast(sizeof(char*), u64) : 1);\n}\n")
        self.check({"globals.ft": text}, [])
        value = "boundary function 'takep' goes here as a value"
        text = (self.GLOBALS + takep + "fn f() fn (char*) u64 {\n    if (takep == null) {\n"
                "        return (takep);\n    }\n    return (takep);\n}\n")
        self.check({"globals.ft": text}, [
            ("globals.ft", line, value) for line in lines_of(text, "(takep")])

    def test_a_row_in_the_standard_library_is_checked(self):
        # A module of std/ is trusted by its root (D9.8). The test removes the declaration row
        # of `strlen` from a copy of the std.libc of the target and the call row of mem.copy.
        target = "darwin" if sys.platform == "darwin" else "linux"
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            subprocess.run(["cp", "-R", os.path.join(OPTIONS.source, "std"), root], check=True)
            libc = os.path.join(root, "std", target, "libc.ft")
            with open(libc, encoding="utf-8") as handle:
                text = handle.read()
            text = re.sub(r"// C: strlen,.*\n(?://.*\n)*?(?=extern fn strlen)", "", text)
            with open(libc, "w", encoding="utf-8") as handle:
                handle.write(text)
            mem = os.path.join(root, "std", "mem.ft")
            with open(mem, encoding="utf-8") as handle:
                text = handle.read()
            text = re.sub(r"// C call libc\.memmove:.*\n(?://.*\n)*?(?=fn copy)", "", text)
            with open(mem, "w", encoding="utf-8") as handle:
                handle.write(text)
            status, output, problems = lint(root, ["std/%s/libc.ft" % target, "std/mem.ft"])
            self.assertEqual(status, 1, output)
            messages = sorted(message for _, _, message in problems)
            self.assertEqual(len(messages), 2, output)
            self.assertIn("extern 'strlen' has no declaration row", messages[0])
            self.assertIn("fn 'copy' has 1 call(s) of 'libc.memmove'", messages[1])


def lint_module():
    """The module tools/fort_lint.py of the tree under test."""
    tools = os.path.join(OPTIONS.source, "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    import fort_lint
    return fort_lint


def parsed(text):
    """The type `text` as the lint parses it."""
    fort_lint = lint_module()
    return fort_lint.TypeParser(fort_lint.type_tokens(text, 0, len(text))).parse_type()


class Parts(unittest.TestCase):
    """The parts of the lint that read text, with no compiler."""

    def test_a_byte_pointer_is_char_u8_or_void_with_a_first_star(self):
        fort_lint = lint_module()
        reach = {
            "char*": True, "u8*": True, "void*": True, "char mut*": True, "u8 mut*": True,
            "void* own": True, "void mut* own": True, "char*@": True, "u8**": True,
            "char* mut*": True, "u8*[4]": True, "(char)*": True, "(u8 mut)*": True,
            "((char))*": True, "(char*)@": True, "fn (u8*) void": True, "fn () char*": True,
            "fn () fn (char*) void": True, "(fn (u8*) void)[2]": True, "fn (i64, void*) i32": True,
            "u8@": False, "char@": False, "char[4]": False, "char[4]*": False, "u8@*": False,
            "string": False, "string own": False, "i8*": False, "u16*": False, "i64": False,
            "fn (i64*) i32": False, "(char[4])*": False, "(u8@)*": False, "bool*": False,
            "f64 mut*": False, "fn () void": False, "noreturn": False, "void": False,
        }
        for text, want in reach.items():
            with self.subTest(text=text):
                self.assertEqual(fort_lint.type_is_byte_pointer(parsed(text)), want)

    def test_a_type_names_its_structs_where_the_source_spells_them(self):
        ty = parsed("libc.dirent_head mut*")
        self.assertEqual(ty["base"]["name"], "dirent_head")
        self.assertEqual(ty["base"]["at"], len("libc."))
        self.assertEqual(ty["suffixes"], ["*"])
        fn = parsed("fn (walk mut*, u32, fn (holder*) void) i32")
        self.assertEqual(len(fn["base"]["fn"]), 3)
        self.assertEqual(fn["base"]["fn"][0]["base"]["name"], "walk")
        self.assertEqual(fn["base"]["fn"][2]["base"]["fn"][0]["base"]["name"], "holder")
        self.assertEqual(fn["base"]["result"]["base"]["name"], "i32")
        group = parsed("(node*[4])@")
        self.assertEqual(group["suffixes"], ["@"])
        self.assertEqual(group["base"]["group"]["suffixes"], ["*", "[]"])

    def test_comments_and_literals_are_blanked_in_place(self):
        fort_lint = lint_module()
        cases = {
            'a // f(x)\nb': 'a        \nb',
            'f("g(", h)': 'f(    , h)',
            "f('(', h)": "f(   , h)",
            "f('\\'', h)": "f(    , h)",
            'f("a\\"b", c)': 'f(      , c)',
            's = "//";\nt': 's =     ;\nt',
            "x = '/'; // y": "x =    ;     ",
        }
        for text, want in cases.items():
            with self.subTest(text=text):
                blank = fort_lint.blank_comments_and_literals(text)
                self.assertEqual(blank, want)
                self.assertEqual(len(blank), len(text))

    def test_the_comment_run_stops_at_doc_text_directives_blank_lines_and_code(self):
        fort_lint = lint_module()
        text = "\n".join([
            "//! run",            # 1
            "// a",               # 2
            "/// doc",            # 3
            "// C: x, y.",        # 4
            "    // indented",    # 5
            "extern fn x() i32;", # 6
            "",                   # 7
            "// lone",            # 8
            "",                   # 9
            "fn y() void {}",     # 10
            "i32 Z = 1;",         # 11
            "// after code",      # 12
            "fn z() void {}",     # 13
        ])
        source = fort_lint.Source(text)
        self.assertEqual(fort_lint.comment_run(source, 6),
                         [(4, "// C: x, y."), (5, "// indented")])
        self.assertEqual(fort_lint.comment_run(source, 10), [])
        self.assertEqual(fort_lint.comment_run(source, 13), [(12, "// after code")])
        self.assertEqual(fort_lint.comment_run(source, 3), [(2, "// a")])
        self.assertEqual(fort_lint.comment_run(source, 2), [])

    def test_a_call_row_reads_its_callee_and_its_count(self):
        fort_lint = lint_module()
        run = [
            (1, "// free text"),
            (2, "// C call libc.write: buf.ptr and buf.len."),
            (3, "// more text of the row"),
            (4, "// C call fail_begin x14: the file."),
            (5, "// C call z->introspect->enumerator: C gave it."),
            (6, "// C call labs x0: none."),
            (7, "// C call labs"),
            (8, "// C callee: no row."),
            (9, "// C call str.from_cstr:"),
        ]
        self.assertEqual(fort_lint.call_rows(run), [
            (2, "libc.write", 1), (4, "fail_begin", 14), (5, "z->introspect->enumerator", 1),
            (6, "labs", 0), (7, None, 0), (9, "str.from_cstr", 1)])

    def test_a_call_counts_by_its_whole_qualified_name(self):
        fort_lint = lint_module()
        code = ("libc.write(a); write(b); io.write(c); write_all(d); p->write(e); "
                "rewrite(f); write (g); libc.write\n(h); x.libc.write(i);")
        self.assertEqual(fort_lint.text_calls(code, "write", 0, len(code)), 2)
        self.assertEqual(fort_lint.text_calls(code, "libc.write", 0, len(code)), 2)
        self.assertEqual(fort_lint.text_calls(code, "p->write", 0, len(code)), 1)
        self.assertEqual(fort_lint.text_calls(code, "write_all", 0, len(code)), 1)
        code = ("p[0..libc.strlen(a)]; 9>libc.strlen(b); libc . strlen (c); x.libc.strlen(d); "
                "z->libc.strlen(e); table [0] (f); pick ( ) (g); h . on(h)")
        self.assertEqual(fort_lint.text_calls(code, "libc.strlen", 0, len(code)), 3)
        self.assertEqual(fort_lint.text_calls(code, "table[0]", 0, len(code)), 1)
        self.assertEqual(fort_lint.text_calls(code, "pick()", 0, len(code)), 1)
        self.assertEqual(fort_lint.text_calls(code, "h.on", 0, len(code)), 1)

    def test_the_callee_takes_its_module_path(self):
        fort_lint = lint_module()
        code = ("x = libc.write(a) + str.from_cstr(p) + fail_begin(f) + a.b.c(d) + io . read(e)"
                " + p[0..m.size(g)];")
        for name, want in (("write", "libc.write"), ("from_cstr", "str.from_cstr"),
                           ("fail_begin", "fail_begin"), ("c", "a.b.c"), ("read", "io.read"),
                           ("size", "m.size")):
            start = code.index(name + "(")
            self.assertEqual(fort_lint.spelled_callee(code, start)[1], want)

    def test_the_index_count_is_the_authority_over_the_text(self):
        # A callee that the index counts twice and the text once is an error, whatever the
        # rows say: the text count only places the rows (D9.8).
        fort_lint = lint_module()
        text = "// C call k: a value.\nfn f(fn (i64) i64 k) i64 {\n    return k(1);\n}\n"
        source = fort_lint.Source(text)
        record = {"name": "f", "line": 2, "col": 4}
        problems = fort_lint.function_row_problems(None, "f.ft", source, record, {"k": 2},
                                                   {"k"})
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("fn 'f' has 2 call(s) of 'k' by the calls through a value that the "
                      "brackets of the body give, and the lint finds 1 in its text",
                      problems[0][2])
        # The count of a callee that is no function value comes from the index.
        problems = fort_lint.function_row_problems(None, "f.ft", source, record, {"k": 2}, ())
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("fn 'f' has 2 call(s) of 'k' by the index of the compiler, and the lint "
                      "finds 1 in its text", problems[0][2])
        self.assertEqual(fort_lint.function_row_problems(None, "f.ft", source, record,
                                                         {"k": 1}, {"k"}), [])

    def test_the_callee_of_a_call_is_read_at_its_open_bracket(self):
        # `^` marks the `(` that the test reads. The result is (the callee as the body spells
        # it, the count of groups, the callee in the groups, the name that holds the record).
        fort_lint = lint_module()
        cases = (
            ("strlen^(p)", ("strlen", 0, "strlen", "strlen")),
            ("libc . strlen ^(p)", ("libc.strlen", 0, "libc.strlen", "strlen")),
            ("(strlen)^(p)", ("(strlen)", 1, "strlen", "strlen")),
            ("x = (strlen)\n    ^(p)", ("(strlen)", 1, "strlen", "strlen")),
            ("( ( libc . strlen ) ) ^(p)", ("((libc.strlen))", 2, "libc.strlen", "strlen")),
            ("(((strlen)))^(p)", ("(((strlen)))", 3, "strlen", "strlen")),
            ("if (cmp^(&a[i], b) < 0)", ("cmp", 0, "cmp", "cmp")),
            ("t [0]^(x)", ("t[0]", 0, "t[0]", None)),
            ("t[0] [1]^(x)", ("t[0][1]", 0, "t[0][1]", None)),
            ("pick()^(y)", ("pick()", 0, "pick()", None)),
            ("pick ( ) ^(y)", ("pick()", 0, "pick()", None)),
            ("cast(m, n)^(o)", ("cast(m,n)", 0, "cast(m,n)", None)),
            ("(pick())^(y)", ("(pick())", 1, "pick()", None)),
            ("(t[0])^(y)", ("(t[0])", 1, "t[0]", None)),
            ("(a + f)^(x)", ("(a+f)", 1, "a+f", None)),
            ("(f, g)^(x)", ("(f,g)", 1, "f,g", None)),
            ("(c ? f : g)^(x)", ("(c?f:g)", 1, "c?f:g", None)),
            ("((f) + 1)^(x)", ("((f)+1)", 1, "(f)+1", None)),
            ("(f)(g)^(x)", ("(f)(g)", 0, "(f)(g)", None)),
            ("((f)(g))^(x)", ("((f)(g))", 1, "(f)(g)", None)),
            ("h.on^(z)", ("h.on", 0, "h.on", "on")),
            ("(h . on)^(z)", ("(h.on)", 1, "h.on", "on")),
            ("p->f^(w)", ("p->f", 0, "p->f", "f")),
            ("z->introspect->enumerator^(v)",
             ("z->introspect->enumerator", 0, "z->introspect->enumerator", "enumerator")),
            ("x.libc.write^(i)", ("x.libc.write", 0, "x.libc.write", "write")),
            ("q[0..r^(s)]", ("r", 0, "r", "r")),
            ("q[0..(r)^(s)]", ("(r)", 1, "r", "r")),
            ("return (k)^(x)", ("(k)", 1, "k", "k")),
            ("defer (d)^(y)", ("(d)", 1, "d", "d")),
            ("returned(k)^(x)", ("returned(k)", 0, "returned(k)", None)),
            ("returned^(k)(x)", ("returned", 0, "returned", "returned")),
            ("iffy^(n)", ("iffy", 0, "iffy", "iffy")), ("m.cfg^(n)", ("m.cfg", 0, "m.cfg", "cfg")),
            ("x.sizeof_^(n)", ("x.sizeof_", 0, "x.sizeof_", "sizeof_")),
            # A keyword is no name: the `(` after it opens no argument list of a callee.
            ("return ^(strlen)(p)", None), ("defer ^(strlen)(fd)", None), ("if ^(n > 0) {", None),
            ("x = cast^(m, n)(o)", None), ("x = sizeof^(u64)", None), ("p = new^(u64)", None),
            ("fn ^(char*) u64 g", None), ("(if)^(n)", None), ("((while))^(n)", None),
            ("m.if^(n)", None), ("p->return^(n)", None),
            # A compiler form is no call.
            ("$cfg^(mode)", None), ("$if ^($cfg(mode) == x) {", None), ("$if ($cfg^(mode) == x) {",
                                                                        None),
            # A group: no name, `)` or `]` stands before the `(`.
            ("^(strlen)(p)", None), ("x = ^(strlen)(p)", None),
            ("f(^(strlen)(p))", None), ("a, ^(strlen)(p)", None),
            ("1 + ^(strlen)(p)", None), ("~^(strlen)(p)", None), ("c ? ^(f)(x) : 0", None),
            ("q[^(f)(x)]", None), ("{ ^(f)(x); }", None), ("q[0..^(r)(s)]", None),
            ("(^(strlen))(p)", None),
            # The list of a function type and its grouped result are no call.
            ("fn () ^(void) g", None), ("fn (char*) ^(fn (i64) i64) g", None),
        )
        for marked, want in cases:
            with self.subTest(code=marked):
                code = marked.replace("^", "")
                callee = fort_lint.call_callee(code, marked.index("^"))
                if want is None:
                    self.assertIsNone(callee)
                    continue
                name = None
                if callee.name_at >= 0:
                    name = fort_lint.IDENTIFIER.match(code, callee.name_at).group(0)
                self.assertEqual((callee.text, callee.groups, callee.name, name), want)
                self.assertEqual(re.sub(r"\s+", "", code[callee.begin:callee.end]), callee.text)
        self.assertEqual(len(cases), 61)

    def test_an_import_is_no_place_of_a_value(self):
        fort_lint = lint_module()
        text = ("import extern;\nimport std.str.from_cstr;\nimport std.libc as\n    c;\n"
                "fn f() void {\n    g(from_cstr);\n}\n")
        source = fort_lint.Source(text)
        self.assertTrue(fort_lint.in_import(source, text.index("from_cstr;")))
        self.assertTrue(fort_lint.in_import(source, text.index("c;")))
        self.assertFalse(fort_lint.in_import(source, text.index("from_cstr)")))

    def test_a_type_starts_after_its_delimiter(self):
        fort_lint = lint_module()
        code = "fn f(i32 a, fn (u8*, i64) void cb, (char)* g) {"
        for name, want in (("a", "i32"), ("cb", "fn (u8*, i64) void"), ("g", "(char)*")):
            at = code.index(" %s," % name) + 1 if name != "g" else code.index(" g)") + 1
            start = fort_lint.type_start(code, at, "(,")
            self.assertEqual(code[start:at].strip(), want)
        fields = "struct s {\n    char[256] name;\n    u8* data;\n}"
        at = fields.index("data")
        self.assertEqual(fields[fort_lint.type_start(fields, at, "{;"):at].strip(), "u8*")
        at = fields.index("name")
        self.assertEqual(fields[fort_lint.type_start(fields, at, "{;"):at].strip(), "char[256]")

    def test_brackets_close_in_their_order(self):
        fort_lint = lint_module()
        code = "f(a[1], {b}, (c)) x"
        self.assertEqual(fort_lint.matching_close(code, 1), code.index(") x"))
        self.assertEqual(fort_lint.matching_close("f(a]", 1), -1)
        self.assertEqual(fort_lint.matching_close("f(a", 1), -1)

    def test_a_name_is_the_callee_of_a_call_through_each_group(self):
        # The name is `strlen`. The result is the callee of the call as the body spells it, or
        # None when `strlen` is the callee of no call.
        fort_lint = lint_module()
        cases = (
            ("strlen(p)", "strlen"), ("strlen (p)", "strlen"), ("strlen\n    (p)", "strlen"),
            ("libc.strlen(p)", "libc.strlen"), ("(strlen)(p)", "(strlen)"),
            ("(strlen) (p)", "(strlen)"), ("( ( strlen ) ) (p)", "((strlen))"),
            ("(((strlen)))(p)", "(((strlen)))"), ("(libc.strlen)(p)", "(libc.strlen)"),
            ("((libc . strlen))(p)", "((libc.strlen))"), ("f((strlen)(p))", "(strlen)"),
            ("return (strlen)(p)", "(strlen)"), ("x[0..(strlen)(p)]", "(strlen)"),
            ("f(strlen)", None), ("(strlen)", None), ("((strlen))", None),
            ("(strlen) + 1", None), ("f((strlen))", None), ("f((strlen), 1)(p)", None),
            ("pick(strlen)(p)", None), ("cast(strlen)(p)", None), ("t[0](strlen)(p)", None),
            ("f()(strlen)(p)", None), ("returned(strlen)(p)", None),
            ("(strlen, x)(p)", None), ("(x + strlen)(p)", None), ("((strlen) + 1)(p)", None),
            ("(x, (strlen))(p)", None), ("(strlen)[0](p)", None), ("(strlen.x)(p)", None),
            ("strlen.x(p)", None), ("strlen[0](p)", None), ("(c ? strlen : strlen)(p)", None),
            ("fn () (strlen) g", None),
        )
        for code, want in cases:
            with self.subTest(code=code):
                start = code.index("strlen")
                callee = fort_lint.named_call(code, start)
                self.assertEqual(None if callee is None else callee.text, want)
                if callee is not None:
                    self.assertEqual(callee.name_at, start)
        self.assertEqual(len(cases), 34)

    def test_the_keywords_of_the_lint_are_the_ones_of_the_grammar(self):
        # The lint has one list of the words that are no name, and the reader reads it. The list
        # is the 41 keywords and the 10 reserved words of spec/grammar.md.
        fort_lint = lint_module()
        keywords, reserved = grammar_words()
        self.assertEqual((len(keywords), len(reserved)), (41, 10))
        self.assertEqual(sorted(fort_lint.KEYWORDS), sorted(keywords + reserved))
        self.assertEqual(len(fort_lint.KEYWORDS), 51)
        self.assertEqual(fort_lint.STATEMENT_KEYWORDS, ("return", "defer"))
        self.assertLessEqual(set(fort_lint.STATEMENT_KEYWORDS), fort_lint.KEYWORDS)
        with open(os.path.join(OPTIONS.source, "tools", "fort_lint.py"), encoding="utf-8") as f:
            tree = ast.parse(f.read())
        readers = sorted(function.name for function in tree.body
                         if isinstance(function, ast.FunctionDef) and
                         any(isinstance(node, ast.Name) and node.id == "KEYWORDS"
                             for node in ast.walk(function)))
        self.assertEqual(readers, ["call_callee"])

    def test_a_keyword_is_the_callee_of_no_call_for_each_reader(self):
        # Each word of the list, at each function that reads a callee: the reader at the `(`,
        # the reader from the name, the count of the calls in a text and the reader of a
        # citation. 51 words in 11 forms. The same word with one more letter is a name.
        fort_lint = lint_module()
        cells = 0
        for word in sorted(fort_lint.KEYWORDS):
            with self.subTest(word=word):
                for before in ("%s", "x = %s ", "(%s)", "( ( %s ) ) ", "m.%s", "$%s"):
                    code = before % word + "(a)"
                    self.assertIsNone(fort_lint.call_callee(code, code.index("(a)")), code)
                    cells += 1
                for code in ("%s(a)", "(%s)(a)"):
                    code = code % word
                    self.assertIsNone(fort_lint.named_call(code, code.index(word)), code)
                    cells += 1
                code = "%s (a); %s(b); (%s)(c);" % (word, word, word)
                for callee in (word, "(%s)" % word):
                    self.assertEqual(fort_lint.text_calls(code, callee, 0, len(code)), 0, code)
                    cells += 1
                self.assertIsNone(fort_lint.cited_callee(word + " states", 0))
                self.assertIsNone(fort_lint.cited_callee("(%s) states" % word, 0))
                self.assertIsNone(fort_lint.cited_callee("m.%s states" % word, 0))
                cells += 1
                name = word + "_"
                code = "x = %s (a) + (%s)(b);" % (name, name)
                self.assertEqual(fort_lint.call_callee(code, code.index("(a)")).text, name)
                self.assertEqual(fort_lint.named_call(code, code.index(name)).text, name)
                self.assertEqual(fort_lint.text_calls(code, name, 0, len(code)), 1)
                self.assertEqual(fort_lint.text_calls(code, "(%s)" % name, 0, len(code)), 1)
                self.assertEqual(fort_lint.cited_callee(name + " states", 0), name)
        self.assertEqual(cells, 51 * 11)

    def test_a_citation_names_its_callee_as_a_body_spells_it(self):
        # The reader of a citation gives the callee with no group and no white space, or None.
        fort_lint = lint_module()
        cases = (
            ("rt.alloc states", "rt.alloc"), ("alloc.", "alloc"), ("alloc", "alloc"),
            ("alloc, and", "alloc"), ("rt.alloc. Then", "rt.alloc"), ("a.b.c d", "a.b.c"),
            ("(rt.alloc) states", "rt.alloc"), ("((alloc)).", "alloc"),
            ("(( rt . alloc )) x", "rt.alloc"), ("( alloc )", "alloc"), ("rt.alloc()", "rt.alloc"),
            ("(alloc)(1)", "alloc"), ("(p->f)", "p->f"), ("iffy", "iffy"), ("table[0]", "table"),
            ("", None), (" alloc", None), ("3", None), ("4x", None), (".alloc", None),
            ("`alloc`", None), ("(alloc", None), ("((alloc)", None), ("()", None), ("(())", None),
            ("(a + b)", None), ("(a, b)", None), ("(t[0])", None), ("(pick())", None),
            ("(c ? a : b)", None), ("if", None), ("(return)", None), ("(alloc]", None),
        )
        for text, want in cases:
            with self.subTest(text=text):
                self.assertEqual(fort_lint.cited_callee(text, 0), want)
                self.assertEqual(fort_lint.cited_callee("of " + text, 3), want)
        self.assertEqual(len(cases), 33)

    def test_an_offset_that_starts_no_name_is_the_callee_of_no_call(self):
        fort_lint = lint_module()
        for code, start in (("(x)", 0), ("f(x)", 1), ("f (x)", 1), ("1(x)", 0), ("", 0), ("f(", 2)):
            with self.subTest(code=code, start=start):
                self.assertIsNone(fort_lint.named_call(code, start))

    def test_each_place_takes_its_callee_from_the_one_reader(self):
        # D9.8, "The lint": the lint reads the callee of a call through each group around it.
        # One function, `call_callee`, reads a callee, at the `(` of the call. `named_call`
        # asks it for the call of a name. This test counts the places that take a callee from
        # the two functions, and Rules.PLACES has a program for each one, in each form of a
        # group. It also counts the means that read a callee with no reader: a text of
        # brackets, a step to the next character, a pattern that ends at an offset, a walk by
        # one character, and a pattern that matches a bracket.
        # The test is no proof. It fails for 16 measured mutants: 10 that change one place to
        # another reader, and 6 that add a place with one of those means. A new
        # place that reads a callee by another means passes here, and a reviewer finds it.
        # For a new place that asks the reader: add it to `sites` and give it a program.
        with open(os.path.join(OPTIONS.source, "tools", "fort_lint.py"), encoding="utf-8") as f:
            tree = ast.parse(f.read())
        functions = []
        for node in tree.body:
            if isinstance(node, ast.FunctionDef):
                functions.append((node.name, node))
            elif isinstance(node, ast.ClassDef):
                functions.extend(("%s.%s" % (node.name, item.name), item) for item in node.body
                                 if isinstance(item, ast.FunctionDef))
        sites = {}
        brackets = set()
        steps = set()
        ends = set()
        walks = set()
        for name, function in functions:
            for node in ast.walk(function):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                    if node.func.id in ("call_callee", "named_call"):
                        sites[(name, node.func.id)] = sites.get((name, node.func.id), 0) + 1
                    if node.func.id in ("callee_span", "prev_char", "next_char"):
                        steps.add((name, node.func.id))
                # A text of brackets and no word: the function looks at a bracket of the code.
                if isinstance(node, ast.Constant) and isinstance(node.value, str) and \
                        re.search(r"[()]", node.value) and \
                        not re.search(r"[A-Za-z0-9 %]", node.value):
                    brackets.add(name)
                # A pattern that ends at `\Z`: the function reads a word that ends at an offset.
                if isinstance(node, ast.Constant) and isinstance(node.value, str) and \
                        node.value.endswith("\\Z"):
                    ends.add(name)
                # A test of one character: the function walks the code by hand.
                if isinstance(node, ast.Attribute) and node.attr in (
                        "isspace", "isalnum", "isalpha", "isdigit", "isidentifier"):
                    walks.add(name)
                if isinstance(node, ast.Name) and node.id == "IDENTIFIER_CHAR":
                    walks.add(name)
        self.assertEqual(sites, {
            ("named_call", "call_callee"): 1,
            ("value_calls", "call_callee"): 1,
            ("goes_to_c", "named_call"): 1,
            ("goes_to_c", "call_callee"): 1,
            ("text_calls", "call_callee"): 1,
            ("mut_global_to_c", "call_callee"): 1,
            ("mut_global_to_c", "named_call"): 1,
            ("row_problems", "named_call"): 3,
            ("cited_callee", "call_callee"): 1,
        })
        # 10 places: each call above but the one of `named_call` itself. Each has a program.
        places = sum(count for (name, _), count in sites.items() if name != "named_call")
        self.assertEqual(places, 10)
        held = {}
        for site, *_ in Rules.PLACES:
            held.setdefault(site[:2], set()).add(site)
        self.assertEqual({site: len(programs) for site, programs in held.items()}, {
            ("value_calls", "call_callee"): 1,
            ("goes_to_c", "named_call"): 1,
            ("goes_to_c", "call_callee"): 3,
            ("text_calls", "call_callee"): 1,
            ("mut_global_to_c", "call_callee"): 2,
            ("mut_global_to_c", "named_call"): 1,
            ("row_problems", "named_call"): 3,
            ("cited_callee", "call_callee"): 1,
        })
        # No other function reads a callee. Each function that looks at a bracket of the code
        # is the reader, a place that asks the reader, or one of these: the two that pair
        # brackets, the two that find the `(` around an offset, and the four that read a type.
        self.assertEqual(brackets, {
            "callee_span", "call_callee", "named_call",
            "value_calls", "goes_to_c", "text_calls", "cited_callee",
            "matching_open", "matching_close", "call_open", "enclosing_opens",
            "TypeParser.parse_base", "type_start", "is_function_type", "Rows.is_boundary"})
        # Only the reader walks a callee. `spelled_callee` steps back over the module path of
        # a name for a message and for the start of an argument, and `goes_to_c` looks at the
        # `(` or `,` before an argument and the `,` or `)` after it.
        self.assertEqual(steps, {
            ("call_callee", "callee_span"), ("named_call", "next_char"),
            ("spelled_callee", "prev_char"), ("goes_to_c", "prev_char"),
            ("goes_to_c", "next_char")})
        # A pattern that ends at an offset reads the word before a `.`, a `(` or a group: the
        # module path of a name, and the keyword that the reader looks for.
        self.assertEqual(ends, {"spelled_callee", "callee_span", "call_callee"})
        # The reader and the two steps walk the code by one character, and the tokens of a type.
        self.assertEqual(walks, {"callee_span", "call_callee", "next_char", "prev_char",
                                 "type_tokens"})
        # No pattern of the lint matches a bracket: a callee is no match of a pattern.
        patterns = [node.value for node in ast.walk(tree) if isinstance(node, ast.Constant) and
                    isinstance(node.value, str) and re.search(r"\\[()]", node.value)]
        self.assertEqual(patterns, [])

    def test_the_trust_form_is_the_first_item(self):
        fort_lint = lint_module()
        for text, want in (("import extern;\nfn f() void {}", True),
                           ("/// doc\n// note\nimport  extern ;", True),
                           ("import std.libc;\nimport extern;", False),
                           ("// import extern;\nfn f() void {}", False),
                           ("import externs;", False)):
            with self.subTest(text=text):
                self.assertEqual(fort_lint.Source(text).is_trusted_form(), want)

    def test_the_options_of_a_language_test_are_its_cfg_pairs(self):
        fort_lint = lint_module()
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            write(root, "test/lang/run/a/001_one.ft",
                  "//! run\n//! flags: --release --cfg a=1,b= --no-bounds-check --cfg c=x=y\n"
                  "//! stdout:\n//| 1\n// --cfg z=0 is no directive\nfn main() i32 {}\n")
            write(root, "test/lang/run/modules/d/main.ft", "//! run\n//! flags: --cfg k=v\n")
            write(root, "test/lang/run/modules/d/dep.ft", "//! flags: --cfg other=1\n")
            write(root, "test/lang/programs/plain.ft", "//! run\nfn main() i32 {}\n")
            write(root, "test/fort/x.ft", "//! flags: --cfg k=v\n")
            for relative, want in (
                    ("test/lang/run/a/001_one.ft", ("--cfg", "a=1,b=", "--cfg", "c=x=y")),
                    ("test/lang/run/modules/d/main.ft", ("--cfg", "k=v")),
                    ("test/lang/run/modules/d/dep.ft", ("--cfg", "k=v")),
                    ("test/lang/programs/plain.ft", ()),
                    ("test/fort/x.ft", ())):
                with self.subTest(relative=relative):
                    self.assertEqual(fort_lint.lang_options(root, relative), want)
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            write(root, "test/lang/run/b/001_two.ft",
                  "//! run\n//! stdout:\n//| x\n//< y\n//! flags: --cfg late=1\nfn main() i32 {}\n")
            write(root, "test/lang/run/modules/e/main.ft", "//! run\n//! flags: --cfg deep=1\n")
            write(root, "test/lang/run/modules/e/sub/leaf.ft", "fn f() void {}\n")
            self.assertEqual(fort_lint.lang_options(root, "test/lang/run/b/001_two.ft"),
                             ("--cfg", "late=1"))
            self.assertEqual(fort_lint.lang_options(root, "test/lang/run/modules/e/sub/leaf.ft"),
                             ("--cfg", "deep=1"))
        self.assertTrue(fort_lint.rows_only("test/lang/programs/calc.ft"))
        self.assertTrue(fort_lint.rows_only("test/lang/run/ffi/001_libc.ft"))
        self.assertFalse(fort_lint.rows_only("test/fort/lang_test.ft"))
        self.assertFalse(fort_lint.rows_only("std/rt.ft"))

    def test_the_standard_roots_include_each_place_of_std(self):
        fort_lint = lint_module()
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            os.environ["FORT_STD_DIR"] = os.path.join(root, "env")
            try:
                roots = fort_lint.standard_roots(os.path.join(root, "bin", "fort"), root, "given")
            finally:
                del os.environ["FORT_STD_DIR"]
            self.assertEqual(set(roots), {os.path.join(root, "std"),
                                          os.path.join(root, "bin", "std"),
                                          os.path.join(root, "given"),
                                          os.path.join(root, "env")})


# The module that is not trusted of the tests of a function value: a function that takes a
# function, and keeps nothing.
SINK = "fn keep(fn (char*) u64 f) void {\n}\n\nfn empty(char* p) u64 {\n    return 0;\n}\n"

# Each callee of a call through a function value: the name of the function of the trusted
# module that calls it, and the callee as the body spells it.
VALUE_CALLEES = (("local", "k"), ("param", "k"), ("global", "keeper"), ("constant", "KEEPER"),
                 ("field", "h.on"), ("table", "table[0]"), ("result", "get_hook()"))


def gate(rows):
    """The trusted module of the tests of a function value: a call through each kind of value,
    with a call row for each value when `rows`. Each value but one is a function pointer that C
    gave (`get_hook`), which takes the boundary function `size` as C takes it. The constant holds
    a function of fort code, and it takes `sink.empty`, which is no boundary function (D9.8)."""
    def row(callee, text="a function pointer that C gave by get_hook, so size goes to C."):
        return "// C call %s: %s\n" % (callee, text) if rows else ""
    hook = "// C call get_hook: no pointer argument; C gives a function pointer of C.\n"
    return ("import extern;\nimport sink;\n\n"
            "fn size(char* p) u64 {\n    return 0;\n}\n\n"
            "// C: get_hook, a C function of the test, which gives a function of C.\n"
            "// gives: a function pointer of C that takes a C string. writes: no fort storage.\n"
            "// keeps: no pointer. calls: no fort function. frees: nothing.\n"
            "extern fn get_hook() fn (fn (char*) u64) void;\n\n"
            "struct hooks {\n    fn (fn (char*) u64) void on;\n}\n\n"
            "(fn (fn (char*) u64) void) mut keeper = null;\n"
            "fn (fn (char*) u64) void KEEPER = sink.keep;\n\n"
            "fn nothing() void {\n}\n\n"
            + hook + row("k") + "fn by_local() void {\n"
            "    fn (fn (char*) u64) void k = get_hook();\n    k(size);\n"
            "    fn () (void) quiet = nothing;\n}\n\n"
            + row("k", "k, a function pointer that C gave, which each trusted caller gives.")
            + "fn by_param(fn (fn (char*) u64) void k) void {\n    k(size);\n}\n\n"
            + hook + row("keeper") + "fn by_global() void {\n    keeper = get_hook();\n"
            "    keeper(size);\n}\n\n"
            + row("KEEPER", "sink.keep, a function of fort code; sink.empty is no boundary "
                  "function.")
            + "fn by_constant() void {\n    KEEPER(sink.empty);\n}\n\n"
            + hook + row("h.on") + "fn by_field() void {\n    hooks h = hooks{get_hook()};\n"
            "    h.on(size);\n}\n\n"
            + hook + row("table[0]") + "fn by_table() void {\n"
            "    (fn (fn (char*) u64) void)[1] table = {get_hook()};\n    table[0](size);\n}\n\n"
            + hook + row("get_hook()") + "fn by_result() void {\n    get_hook()(size);\n}\n")


# The module edge of the reach test: a function for each way that a parameter type reaches a
# byte pointer, and one for each type that reaches none, as test/fort/check_trust_test.ft holds
# them for the compiler.
EDGE = """import extern;

struct holder { u8* data; u64 len; }
struct chain { void* item; chain* next; }
struct ring { ring* next; i64 v; }
struct plain { i64 x; i32* y; }
struct wrap { plain p; }
struct both { plain p; holder h; }
struct hooks { fn (u8*) void on; }
struct fwd { back* b; }
struct back { fwd* f; char* name; }
enum shade { dark, light }
fn by_char(char* p) void {}
fn by_byte(u8 mut* p) void {}
fn by_void(void* p) void {}
fn by_owner(void* own p) void {}
fn by_pointee(u8** p) void {}
fn by_element(char*@ names) void {}
fn by_array(void*[2] slots) void {}
fn by_field(holder* h) void {}
fn by_value_field(both b) void {}
fn by_chain(chain* c) void {}
fn by_callback(fn (u8*) void cb) void {}
fn by_result(fn () char* cb) void {}
fn by_two_levels(fn () fn (char*) void cb) void {}
fn by_hooks(hooks* h) void {}
fn by_holders(holder[2] hs) void {}
fn by_nested(holder*@ hs) void {}
fn by_group((char)* p) void {}
fn by_double_group(((u8 mut))* p) void {}
fn by_cycle(fwd* f) void {}
fn by_string(string s) void {}
fn by_bytes(u8@ b) void {}
fn by_typed(fn (i64*) i32 cb) void {}
fn by_plain(plain* p) void {}
fn by_wrap(wrap w) void {}
fn by_ring(ring* r) void {}
fn by_i8(i8* p) void {}
fn by_shade(shade s) void {}
fn by_char_array(char[4]* a) void {}
fn by_span_pointer(u8@* s) void {}
fn gives_char() char* {
    return null;
}
"""


def compiler_refusals(root, probe, includes=()):
    """The names that `fort --check` refuses as boundary functions in the module `probe`."""
    argv = [OPTIONS.fort, "--check"]
    for include in includes:
        argv.extend(["-I", include])
    proc = subprocess.run(argv + [probe], cwd=root, capture_output=True, text=True)
    return {found.group(1) for found in REFUSED.finditer(proc.stdout + proc.stderr)}


def lint_boundaries(root, entry, includes=()):
    """The functions that the lint finds to be boundary functions, by the real path of their
    file and their name, in the closure of `entry`."""
    sys.path.insert(0, os.path.join(OPTIONS.source, "tools"))
    import fort_lint
    document, error = fort_lint.index_document(OPTIONS.fort, root, entry, includes)
    if error is not None:
        raise AssertionError(error)
    cache = {}
    buckets = fort_lint.group_records(document, root, cache)
    rows = fort_lint.Rows(root, buckets, cache,
                          fort_lint.standard_roots(OPTIONS.fort, root, None), {})
    found = {}
    for path, records in buckets.items():
        for record in records:
            if record["kind"] == "fn" and record.get("is_decl"):
                key = (path, record["line"], record["col"])
                found[(path, record["name"])] = rows.is_boundary(key)
    return found


class Reach(unittest.TestCase):
    def test_the_lint_and_the_compiler_find_the_same_boundary_functions(self):
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            write(root, "edge.ft", EDGE)
            names = re.findall(r"^fn ([a-z_]+)\(", EDGE, re.M)
            probe = "import edge;\n" + "".join(
                "\nfn p%d() void {\n    edge.%s();\n}\n" % (i, name)
                for i, name in enumerate(names))
            write(root, "probe.ft", probe)
            refused = compiler_refusals(root, "probe.ft", (".",))
            found = lint_boundaries(root, "edge.ft")
            edge = os.path.join(root, "edge.ft")
            by_lint = {"edge." + name for (path, name), yes in found.items()
                       if path == edge and yes}
            self.assertEqual(by_lint, refused)
            expected = {"edge." + name for name in names
                        if name.startswith("by_") and name not in (
                            "by_string", "by_bytes", "by_typed", "by_plain", "by_wrap",
                            "by_ring", "by_i8", "by_shade", "by_char_array",
                            "by_span_pointer")}
            self.assertEqual(by_lint, expected)

    def test_the_lint_finds_the_boundary_functions_of_std_that_the_compiler_refuses(self):
        with tempfile.TemporaryDirectory() as out:
            root = os.path.realpath(out)
            std = os.path.join(os.path.dirname(OPTIONS.fort), "std")
            modules = []
            for top, _, files in os.walk(std):
                for name in sorted(files):
                    if name.endswith(".ft"):
                        relative = os.path.relpath(os.path.join(top, name), std)
                        modules.append("std." + relative[:-3].replace(os.sep, "."))
            modules.sort()
            entry = "".join("import %s;\n" % m for m in modules) + \
                "fn main() i32 {\n    return 0;\n}\n"
            write(root, "entry.ft", entry)
            found = lint_boundaries(root, "entry.ft")
            calls = []
            by_lint = set()
            std_real = os.path.realpath(std)
            for (path, name), yes in sorted(found.items()):
                if not path.startswith(std_real + os.sep):
                    continue
                module = "std." + os.path.relpath(path, std_real)[:-3].replace(os.sep, ".")
                last = module.rsplit(".", 1)[1]
                if name == "args_init" and last == "rt":
                    # D6.9 refuses each name of rt.args_init first, so the compiler gives no
                    # error of D9.8 for it; the lint finds its `char* mut*`.
                    self.assertTrue(yes)
                    continue
                calls.append("\nfn p%d() void {\n    %s.%s();\n}\n" % (len(calls), last, name))
                if yes:
                    by_lint.add("%s.%s" % (module, name))
            probe = "".join("import %s;\n" % m for m in modules) + "".join(calls)
            write(root, "probe.ft", probe)
            refused = compiler_refusals(root, "probe.ft")
            self.assertEqual(by_lint, refused)
            self.assertIn("std.str.from_cstr", by_lint)
            self.assertIn("std.rt.print_enum", by_lint)


# The C functions whose declaration no row makes legal (D9.8): the 5 of "One thread" and the 6 of
# "Calls from C".
THREAD_NAMES = ("pthread_create", "clone", "clone3", "thrd_create", "vfork")
RETURN_NAMES = ("setjmp", "sigsetjmp", "longjmp", "siglongjmp", "setcontext", "swapcontext")
ONE_THREAD = "a fort program has one thread"
RETURNS_ONCE = "a call of C returns one time to its caller or ends the process"


def declared(name):
    """A trusted module that declares the C function `name` in line 6, below a row that gives
    each point once."""
    return "import extern;\n\n" + ROW.replace("labs", name) + "extern fn %s(i64 v) i64;\n" % name


def decision_names(paragraph):
    """The names of the sentence `The lint reports a declaration of ...` in the paragraph of D9.8
    that starts with `**<paragraph>.**`, in the order of the text."""
    with open(os.path.join(OPTIONS.source, "spec", "decisions.md"), encoding="utf-8") as handle:
        text = handle.read()
    start = text.index("  **%s.**" % paragraph)
    body = text[start:text.index("\n\n", start)]
    sentence = re.search(r"The lint reports a declaration of ((?:`[a-z0-9_]+`(?:,|\s+or)?\s+)+)",
                         body)
    return tuple(re.findall(r"`([a-z0-9_]+)`", sentence.group(1)))


class Names(unittest.TestCase):
    """The declarations that no row makes legal (D9.8, "One thread" and "Calls from C")."""

    check = Rules.check
    check_raw = Rules.check_raw

    def refused(self, name, reason):
        """A declaration of `name` with a row that gives each point is 1 problem, at the name."""
        self.check({"named.ft": declared(name)},
                   [("named.ft", 6, "extern '%s': %s, and no row makes this declaration legal "
                     "(D9.8, " % (name, reason))])

    def test_a_declaration_of_pthread_create_is_reported(self):
        self.refused("pthread_create", ONE_THREAD)

    def test_a_declaration_of_clone_is_reported(self):
        self.refused("clone", ONE_THREAD)

    def test_a_declaration_of_clone3_is_reported(self):
        self.refused("clone3", ONE_THREAD)

    def test_a_declaration_of_thrd_create_is_reported(self):
        self.refused("thrd_create", ONE_THREAD)

    def test_a_declaration_of_vfork_is_reported(self):
        self.refused("vfork", ONE_THREAD)

    def test_a_declaration_of_setjmp_is_reported(self):
        self.refused("setjmp", RETURNS_ONCE)

    def test_a_declaration_of_sigsetjmp_is_reported(self):
        self.refused("sigsetjmp", RETURNS_ONCE)

    def test_a_declaration_of_longjmp_is_reported(self):
        self.refused("longjmp", RETURNS_ONCE)

    def test_a_declaration_of_siglongjmp_is_reported(self):
        self.refused("siglongjmp", RETURNS_ONCE)

    def test_a_declaration_of_setcontext_is_reported(self):
        self.refused("setcontext", RETURNS_ONCE)

    def test_a_declaration_of_swapcontext_is_reported(self):
        self.refused("swapcontext", RETURNS_ONCE)

    def test_the_names_of_the_lint_are_the_ones_of_the_decision(self):
        # The lint has one list for each of the two paragraphs of D9.8, and each list is the
        # names of the sentence of its paragraph: 5 and 6, 11 names, each one time.
        fort_lint = lint_module()
        self.assertEqual(fort_lint.THREAD_NAMES, THREAD_NAMES)
        self.assertEqual(fort_lint.RETURN_NAMES, RETURN_NAMES)
        self.assertEqual(decision_names("One thread"), THREAD_NAMES)
        self.assertEqual(decision_names("Calls from C"), RETURN_NAMES)
        self.assertEqual((len(THREAD_NAMES), len(RETURN_NAMES)), (5, 6))
        self.assertEqual(len(set(THREAD_NAMES + RETURN_NAMES)), 11)

    def test_each_name_gives_the_reason_of_its_paragraph(self):
        fort_lint = lint_module()
        for names, reason, paragraph in ((THREAD_NAMES, ONE_THREAD, "One thread"),
                                         (RETURN_NAMES, RETURNS_ONCE, "Calls from C")):
            for name in names:
                found = fort_lint.refused_declaration_problems("extern fn %s() void;\n" % name)
                self.assertEqual(found, [(1, 11, "extern '%s': %s, and no row makes this "
                                          "declaration legal (D9.8, \"%s\")"
                                          % (name, reason, paragraph))])

    def test_the_problem_stands_at_the_name(self):
        output = self.check_raw({"named.ft": declared("setjmp")})
        self.assertIn("named.ft:6:11: extern 'setjmp': ", output)

    def test_a_declaration_with_no_row_gets_both_problems(self):
        text = "import extern;\n\nextern fn longjmp(i64 v) i64;\n"
        self.check({"bare.ft": text}, [("bare.ft", 3, "extern 'longjmp' has no declaration row"),
                                       ("bare.ft", 3, "extern 'longjmp': " + RETURNS_ONCE)])

    def test_each_declaration_of_a_file_is_reported(self):
        text = declared("setjmp") + "\n" + ROW.replace("labs", "vfork") + \
            "extern fn vfork() i32;\n\n" + ROW + "extern fn labs(i64 v) i64;\n"
        self.check({"both.ft": text}, [("both.ft", 6, "extern 'setjmp': " + RETURNS_ONCE),
                                       ("both.ft", 11, "extern 'vfork': " + ONE_THREAD)])

    def test_a_declaration_in_a_branch_that_the_compile_does_not_take_is_reported(self):
        # The index holds no record of a declaration in a branch of a `$if` that the compile does
        # not take (D21.2), so the check of the names reads the text and not the index.
        text = ("import extern;\n\n$if ($cfg(target_os) == \"plan9\") {\n"
                "    extern fn swapcontext(i64 v) i64;\n}\n\n"
                "fn two() i32 {\n    return 2;\n}\n")
        self.check({"branch.ft": text}, [("branch.ft", 4, "extern 'swapcontext': " +
                                          RETURNS_ONCE)])
        other = text.replace("swapcontext", "labs")
        self.check({"branch.ft": other}, [])

    def test_a_declaration_in_a_module_that_is_not_trusted_is_reported(self):
        # The compiler refuses the declaration (D9.8), and the lint names the function too.
        text = "extern fn siglongjmp(i64 v) i64;\n"
        output = self.check_raw({"plain.ft": text})
        self.assertIn("plain.ft:1:11: extern 'siglongjmp': " + RETURNS_ONCE, output)
        self.assertNotIn("extern 'labs': ", self.check_raw({"plain.ft": text.replace(
            "siglongjmp", "labs")}))

    def test_a_language_test_gets_the_check(self):
        # A file of test/lang/run gets the checks of D9.8, and this is one of them.
        text = "//! run\n" + declared("clone") + "\nfn main() i32 {\n    return 0;\n}\n"
        name = "test/lang/run/ffi/001_thread.ft"
        self.check({name: text}, [(name, 7, "extern 'clone': " + ONE_THREAD)])

    def test_a_name_in_a_comment_or_a_literal_is_no_declaration(self):
        text = ("import std.io;\n\n// extern fn setjmp(i64 v) i64;\n"
                "fn main() i32 {\n    println(\"extern fn longjmp(i64 v) i64;\");\n"
                "    return 0; // extern fn vfork() i32;\n}\n")
        self.check({"quiet.ft": text}, [])

    def test_a_fort_function_with_a_listed_name_is_no_declaration(self):
        # A fort function is no C function: its symbol is a mangled name (D9.7).
        text = ("fn setjmp(i64 v) i64 {\n    return v;\n}\n\n"
                "fn main() i32 {\n    i64 vfork = setjmp(2);\n    return cast(vfork, i32);\n}\n")
        self.check({"own.ft": text}, [])

    def test_a_longer_name_is_no_listed_name(self):
        # The lint reports the 11 names and no other: a reviewer reads the row of each other
        # declaration.
        for name in ("_setjmp", "setjmp2", "clone_range", "clone33", "my_vfork", "longjmper",
                     "pthread_create_np", "getcontext"):
            self.check({"near.ft": declared(name)}, [])
        # `fork` has the type that the runtime declares (D9.8: one type for one symbol).
        self.check({"near.ft": declared("fork").replace("(i64 v) i64", "() i32")}, [])

    def test_white_space_and_comments_between_the_words_change_nothing(self):
        fort_lint = lint_module()
        message = "extern 'setjmp': %s, and no row makes this declaration legal (D9.8, " \
            "\"Calls from C\")" % RETURNS_ONCE
        cases = (("extern fn setjmp(i64 v) i64;", (1, 11)),
                 ("extern  fn\tsetjmp (i64 v) i64;", (1, 12)),
                 ("extern\nfn\n  setjmp(i64 v) i64;", (3, 3)),
                 ("extern // C\n    fn // C\n    setjmp(i64 v) i64;", (3, 5)),
                 ("$if (a) {\n    extern fn setjmp(i64 v) i64;\n}", (2, 15)),
                 ("i32 x = 1;extern fn setjmp(i64 v) i64;", (1, 21)),
                 # A column is a byte column (D20.3): the 2 letters of the literal are 4 bytes,
                 # so the name at character 28 stands at byte 30.
                 ("string s = \"éé\"; extern fn setjmp(i64 v) i64;", (1, 30)))
        for text, where in cases:
            self.assertEqual(fort_lint.refused_declaration_problems(text), [where + (message,)],
                             text)
        self.assertEqual(len(cases), 7)

    def test_text_that_is_no_declaration_gives_no_problem(self):
        fort_lint = lint_module()
        cases = ("", "fn setjmp(i64 v) i64 {\n    return v;\n}\n", "extern fn labs(i64 v) i64;",
                 "// extern fn setjmp(i64 v) i64;", "string s = \"extern fn setjmp(\";",
                 "extern fn _setjmp(i64 v) i64;", "extern fn setjmp_(i64 v) i64;",
                 "extern fn clone4() i32;", "my_extern fn setjmp(i64 v) i64;",
                 "extern fnx setjmp(i64 v) i64;", "externfn setjmp(i64 v) i64;",
                 "extern fn (setjmp)(i64 v) i64;", "setjmp(env);", "extern setjmp(i64 v) i64;")
        for text in cases:
            self.assertEqual(fort_lint.refused_declaration_problems(text), [], text)
        self.assertEqual(len(cases), 14)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--root", required=True, help="the top of the source tree")
    OPTIONS, rest = parser.parse_known_args()
    OPTIONS.fort = os.path.abspath(OPTIONS.fort)
    OPTIONS.source = os.path.abspath(OPTIONS.root)
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
