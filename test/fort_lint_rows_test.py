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
import os
import re
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

    def test_the_callee_of_a_call_through_a_value_is_its_whole_place(self):
        fort_lint = lint_module()
        code = ("if (cmp(&a[i], b) < 0 && t [0](x) + pick()(y) + h.on(z) + p->f(w) + "
                "z->introspect->enumerator(v) + (g)(u) + q[0..r(s)]); return (k)(x); "
                "cast(m, n)(o); t[0] [1](x); defer (d)(y);")
        wants = ["if", "cmp", "t[0]", "pick", "pick()", "h.on", "p->f",
                 "z->introspect->enumerator", "(g)", "r", "(k)", "cast(m,n)", "t[0][1]", "(d)"]
        opens = [i for i, c in enumerate(code) if c == "(" and code[i - 1] != "&"]
        got = [fort_lint.callee_text(code, at) for at in opens]
        for want in wants:
            self.assertIn(want, got)

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
