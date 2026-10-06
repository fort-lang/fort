#!/usr/bin/env python3
"""Test tools/panic_coverage.py over small repository trees that each seed one class of problem."""

import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "panic_coverage", REPOSITORY / "tools/panic_coverage.py"
)
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)

# A module of src/fort with one tested site and one site the list names.
AST = """\
fn child(u64 i) u64 {
    if (i > 3) {
        panic("ast.child: index out of range");
    }
    return i;
}

fn grow(u64 n) u64 {
    if (n == 0) {
        panic("ast.grow: capacity overflow");
    }
    return n;
}
"""
AST_TEST = """\
//! run
//! flags: -I ../../src/fort
//! abort
//! stderr: panic: ast.child: index out of range
//! stdout:
//| 1
/// The body is not read.
"""
STD_VEC = """\
fn pop(u64 n) u64 {
    if (n == 0) {
        panic("vec.pop: empty");
    }
    return n - 1;
}
"""
LANG_POP = """\
//! run
//! abort
//! stderr: vec.pop: empty
import std.vec;
"""
LANG_OWN = """\
//! run
//! abort
//! stderr: panic: boom
fn main() i32 {
    panic("boom");
}
"""
GAPS = 'src/fort/ast.ft "ast.grow: capacity overflow"\n'


def panic_test(text, directive="stderr"):
    return "//! run\n//! abort\n//! %s: %s\n//! stdout:\n" % (directive, text)


class Tree:
    """A repository tree in a temporary directory."""

    def __init__(self, files):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        for rel in ("src/fort", "src/lsp", "std", "test/fort", "test/lsp", "test/lang"):
            (self.root / rel).mkdir(parents=True, exist_ok=True)
        for rel, text in files.items():
            self.write(rel, text)

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def remove(self, rel):
        (self.root / rel).unlink()

    def run(self, *extra):
        out, err = io.StringIO(), io.StringIO()
        status = coverage.main(["--root", str(self.root), *extra], out, err)
        return status, out.getvalue(), err.getvalue()

    def close(self):
        self.dir.cleanup()


def base_files():
    """A green tree: a tested site, a listed site, a std site and a language program."""
    return {
        "src/fort/ast.ft": AST,
        "test/fort/ast_child_panic_test.ft": AST_TEST,
        "std/vec.ft": STD_VEC,
        "test/lang/run/stdlib/001_vec_pop.ft": LANG_POP,
        "test/lang/run/errors/002_own_panic.ft": LANG_OWN,
        "test/panic_gaps.txt": GAPS,
    }


class TreeCase(unittest.TestCase):
    def tree(self, changes=None):
        """The base tree with each path of `changes` set to its text, or removed for None."""
        files = base_files()
        files.update(changes or {})
        tree = Tree({k: v for k, v in files.items() if v is not None})
        self.addCleanup(tree.close)
        return tree

    def assertProblem(self, tree, fragment):
        status, out, err = tree.run()
        self.assertEqual(status, 1, out + err)
        self.assertIn(fragment, err)
        self.assertIn("panic_coverage: error: ", err)
        return out, err

    def assertGreen(self, tree):
        status, out, err = tree.run()
        self.assertEqual(status, 0, out + err)
        self.assertEqual(err, "")
        self.assertIn("panic_coverage: no problems", out)
        return out


class GreenTree(TreeCase):
    def test_the_base_tree_has_no_problem(self):
        out = self.assertGreen(self.tree())
        self.assertIn("src/fort/ast.ft: 2 sites, 1 tested, 0 unreachable, 1 untested\n", out)
        self.assertIn('  untested src/fort/ast.ft:10 "ast.grow: capacity overflow" (listed)\n', out)
        self.assertIn("std/vec.ft: 1 sites, 1 tested, 0 unreachable, 0 untested\n", out)
        self.assertIn("test/fort: 1 panic tests, 0 with no single site\n", out)
        self.assertIn("test/lang: 1 aborting programs answer a site\n", out)
        self.assertIn("total: 3 sites, 2 tested, 0 unreachable, 1 untested\n", out)

    def test_a_language_program_that_panics_in_its_own_code_is_no_orphan(self):
        tree = self.tree()
        out = self.assertGreen(tree)
        self.assertNotIn("002_own_panic", out)

    def test_an_unreachable_declaration_answers_its_site(self):
        tree = self.tree({
            "src/fort/ast.ft": AST.replace(
                "    if (n == 0) {\n",
                "    if (n == 0) {\n        // panic-coverage: unreachable: n is never 0.\n"),
            "test/panic_gaps.txt": "",
        })
        out = self.assertGreen(tree)
        self.assertIn("src/fort/ast.ft: 2 sites, 1 tested, 1 unreachable, 0 untested\n", out)

    def test_a_declaration_may_stand_above_other_comment_lines(self):
        tree = self.tree({
            "src/fort/ast.ft": AST.replace(
                "    if (n == 0) {\n",
                "    if (n == 0) {\n        // panic-coverage: unreachable: n is never 0.\n"
                "        // The caller holds n above 0.\n"),
            "test/panic_gaps.txt": "",
        })
        self.assertGreen(tree)

    def test_a_tests_declaration_answers_a_site_with_a_forwarded_text(self):
        source = """\
fn bit(u64 i, string who) u64 {
    if (i > 63) {
        // panic-coverage: tests: flow_get flow_set
        panic(who);
    }
    return i;
}
"""
        tree = self.tree({
            "src/fort/flow.ft": source,
            "test/fort/flow_get_panic_test.ft": panic_test("panic: flow.get: out of range"),
            "test/fort/flow_set_panic_test.ft": panic_test("flow.set: out of range"),
        })
        out = self.assertGreen(tree)
        self.assertIn("src/fort/flow.ft: 1 sites, 1 tested, 0 unreachable, 0 untested\n", out)

    def test_a_tests_declaration_may_take_several_lines(self):
        source = """\
fn bit(string who) void {
    // panic-coverage: tests: flow_get
    // panic-coverage: tests: flow_set
    panic(who);
}
"""
        tree = self.tree({
            "src/fort/flow.ft": source,
            "test/fort/flow_get_panic_test.ft": panic_test("flow.get"),
            "test/fort/flow_set_panic_test.ft": panic_test("flow.set"),
        })
        self.assertGreen(tree)

    def test_an_lsp_site_takes_its_test_from_test_lsp(self):
        tree = self.tree({
            "src/lsp/rpc.ft": 'fn f() void {\n    panic("rpc.read: chunk too long");\n}\n',
            "test/lsp/lsp_rpc_chunk_panic_test.ft":
                panic_test("panic: rpc.read: chunk too long"),
        })
        out = self.assertGreen(tree)
        self.assertIn("test/lsp: 1 panic tests, 0 with no single site\n", out)

    def test_a_per_os_stderr_directive_answers_a_site(self):
        tree = self.tree({
            "test/fort/ast_child_panic_test.ft":
                panic_test("panic: ast.child: index out of range", "stderr-macos"),
        })
        self.assertGreen(tree)

    def test_a_language_program_with_signal_abrt_answers_a_std_site(self):
        tree = self.tree({
            "test/lang/run/stdlib/001_vec_pop.ft":
                "//! run\n//! signal: ABRT\n//! stderr: panic: vec.pop: empty\n",
        })
        self.assertGreen(tree)

    def test_a_list_line_may_carry_a_comment(self):
        tree = self.tree({"test/panic_gaps.txt": "# head\n\n" + GAPS.rstrip("\n")
                            + "  # a reason\n"})
        self.assertGreen(tree)

    def test_a_missing_list_is_an_empty_list(self):
        tree = self.tree({"test/panic_gaps.txt": None})
        self.assertProblem(tree, 'the site "ast.grow: capacity overflow" has no test')

    def test_the_gaps_option_names_another_list(self):
        tree = self.tree({"test/panic_gaps.txt": None})
        tree.write("other.txt", GAPS)
        status, out, err = tree.run("--gaps", str(tree.root / "other.txt"))
        self.assertEqual(status, 0, out + err)


class UncoveredSite(TreeCase):
    def test_a_site_with_no_test_and_no_line_fails(self):
        tree = self.tree({"test/panic_gaps.txt": ""})
        out, err = self.assertProblem(
            tree, 'src/fort/ast.ft:10: the site "ast.grow: capacity overflow" has no test, no '
                  "declaration and no line in test/panic_gaps.txt")
        self.assertIn('  untested src/fort/ast.ft:10 "ast.grow: capacity overflow" (NOT LISTED)',
                      out)
        self.assertIn("panic_coverage: 1 problems\n", err)

    def test_deleting_the_one_test_of_a_site_fails(self):
        tree = self.tree()
        tree.remove("test/fort/ast_child_panic_test.ft")
        self.assertProblem(tree, 'src/fort/ast.ft:3: the site "ast.child: index out of range"')

    def test_deleting_the_language_program_of_a_std_site_fails(self):
        tree = self.tree()
        tree.remove("test/lang/run/stdlib/001_vec_pop.ft")
        self.assertProblem(tree, 'std/vec.ft:3: the site "vec.pop: empty" has no test')

    def test_a_language_program_that_does_not_abort_answers_no_site(self):
        tree = self.tree({"test/lang/run/stdlib/001_vec_pop.ft":
                            "//! run\n//! stderr: vec.pop: empty\n"})
        self.assertProblem(tree, 'std/vec.ft:3: the site "vec.pop: empty" has no test')

    def test_a_fragment_of_the_text_answers_no_site(self):
        tree = self.tree({"test/fort/ast_child_panic_test.ft":
                            panic_test("panic: ast.child: index out")})
        self.assertProblem(tree, 'src/fort/ast.ft:3: the site "ast.child: index out of range"')

    def test_a_text_with_more_words_answers_no_site(self):
        tree = self.tree({"test/fort/ast_child_panic_test.ft":
                            panic_test("t.ft:3:9: panic: ast.child: index out of range")})
        self.assertProblem(tree, 'src/fort/ast.ft:3: the site "ast.child: index out of range"')

    def test_a_stderr_line_after_the_header_is_not_read(self):
        tree = self.tree({"test/fort/ast_child_panic_test.ft":
                            "//! run\n//! abort\nimport ast;\n"
                            "//! stderr: panic: ast.child: index out of range\n"})
        self.assertProblem(tree,
                           "test/fort/ast_child_panic_test.ft: the test answers no panic site")

    def test_a_site_whose_argument_is_no_literal_needs_a_declaration(self):
        tree = self.tree({"src/fort/flow.ft": "fn f(string who) void {\n    panic(who);\n}\n"})
        self.assertProblem(tree, "src/fort/flow.ft:2: the argument is not one string literal, so "
                                 "the site needs a declaration")

    def test_a_literal_with_more_after_it_is_no_literal_site(self):
        tree = self.tree({"src/fort/flow.ft":
                            'fn f(string w) void {\n    panic("a" + w);\n}\n'})
        self.assertProblem(tree, "src/fort/flow.ft:2: the argument is not one string literal")

    def test_two_sites_on_one_line_are_two_sites(self):
        tree = self.tree({"src/fort/flow.ft":
                            'fn f(bool b) void {\n'
                            '    if (b) { panic("flow.a: one"); } else { panic("flow.b: two"); }\n'
                            '}\n'})
        status, out, err = tree.run()
        self.assertEqual(status, 1)
        self.assertIn('src/fort/flow.ft:2: the site "flow.a: one"', err)
        self.assertIn('src/fort/flow.ft:2: the site "flow.b: two"', err)


class OrphanTest(TreeCase):
    def test_a_panic_test_that_answers_no_site_fails(self):
        tree = self.tree({"test/fort/ast_gone_panic_test.ft":
                            panic_test("panic: ast.gone: no such site")})
        out, err = self.assertProblem(
            tree, "test/fort/ast_gone_panic_test.ft: the test answers no panic site")
        self.assertIn("test/fort: 2 panic tests, 1 with no single site\n", out)
        self.assertIn("  no single site test/fort/ast_gone_panic_test.ft\n", out)

    def test_a_panic_test_with_no_stderr_directive_fails(self):
        tree = self.tree({"test/lsp/lsp_x_panic_test.ft": "//! run\n//! abort\n"})
        self.assertProblem(tree, "test/lsp/lsp_x_panic_test.ft: the test answers no panic site")

    def test_a_test_named_by_a_declaration_and_matching_a_text_answers_two_sites(self):
        source = "fn bit(string who) void {\n    // panic-coverage: tests: ast_child\n" \
                 "    panic(who);\n}\n"
        tree = self.tree({"src/fort/flow.ft": source})
        self.assertProblem(tree, "test/fort/ast_child_panic_test.ft: the test answers 2 sites: "
                                 "src/fort/flow.ft:3, src/fort/ast.ft:3")

    def test_one_test_with_the_texts_of_two_sites_fails(self):
        tree = self.tree({
            "test/fort/ast_child_panic_test.ft":
                AST_TEST.replace("//! stdout:", "//! stderr: ast.grow: capacity overflow\n"
                                 "//! stdout:"),
            "test/panic_gaps.txt": "",
        })
        self.assertProblem(tree, "test/fort/ast_child_panic_test.ft: the test answers 2 sites")

    def test_a_language_program_answers_no_compiler_site(self):
        tree = self.tree({"test/lang/run/errors/003_ast_child.ft":
                          "//! run\n//! abort\n//! stderr: panic: ast.child: index out of range\n"})
        tree.remove("test/fort/ast_child_panic_test.ft")
        out, err = self.assertProblem(tree, 'src/fort/ast.ft:3: the site "ast.child: index out of '
                                            'range" has no test')
        self.assertIn("test/lang: 1 aborting programs answer a site\n", out)

    def test_a_language_program_with_two_texts_answers_no_site(self):
        tree = self.tree({
            "std/mem.ft": 'fn f() void {\n    panic("mem.copy: short");\n}\n',
            "test/lang/run/stdlib/001_vec_pop.ft":
                "//! run\n//! abort\n//! stderr: vec.pop: empty\n//! stderr: mem.copy: short\n",
        })
        status, out, err = tree.run()
        self.assertEqual(status, 1)
        self.assertIn('std/vec.ft:3: the site "vec.pop: empty" has no test', err)
        self.assertIn('std/mem.ft:2: the site "mem.copy: short" has no test', err)


class SharedText(TreeCase):
    def test_two_sites_of_one_module_with_one_text_fail(self):
        tree = self.tree({"src/fort/ast.ft": AST.replace(
            "ast.grow: capacity overflow", "ast.child: index out of range"),
            "test/panic_gaps.txt": ""})
        self.assertProblem(tree, 'src/fort/ast.ft:10: the text "ast.child: index out of range" is '
                                 "also the text of src/fort/ast.ft:3")

    def test_two_sites_of_two_roots_with_one_text_fail(self):
        tree = self.tree({"src/lsp/json.ft":
                            'fn f() void {\n    panic("ast.child: index out of range");\n}\n'})
        self.assertProblem(tree, 'src/lsp/json.ft:2: the text "ast.child: index out of range" is '
                                 "also the text of src/fort/ast.ft:3")


class StaleDeclaration(TreeCase):
    def test_a_declaration_above_no_site_fails(self):
        tree = self.tree({"src/fort/ast.ft": AST + "\n// panic-coverage: unreachable: gone.\n"
                            "fn h() void {\n}\n"})
        self.assertProblem(tree, "src/fort/ast.ft:15: the declaration stands above no panic site")

    def test_a_declaration_separated_from_its_site_by_code_fails(self):
        source = AST.replace("    if (n == 0) {\n",
                             "    // panic-coverage: unreachable: n is never 0.\n"
                             "    if (n == 0) {\n")
        tree = self.tree({"src/fort/ast.ft": source, "test/panic_gaps.txt": ""})
        out, err = self.assertProblem(tree, "src/fort/ast.ft:9: the declaration stands above no "
                                            "panic site")
        self.assertIn('the site "ast.grow: capacity overflow" has no test', err)

    def test_a_declaration_separated_from_its_site_by_a_blank_line_fails(self):
        source = AST.replace("    if (n == 0) {\n",
                             "    if (n == 0) {\n"
                             "        // panic-coverage: unreachable: n is never 0.\n\n")
        tree = self.tree({"src/fort/ast.ft": source, "test/panic_gaps.txt": ""})
        out, err = self.assertProblem(tree, "src/fort/ast.ft:10: the declaration stands above no "
                                            "panic site")
        self.assertIn('the site "ast.grow: capacity overflow" has no test', err)

    def test_a_declaration_that_names_a_missing_test_fails(self):
        source = "fn bit(string who) void {\n    // panic-coverage: tests: flow_get\n" \
                 "    panic(who);\n}\n"
        tree = self.tree({"src/fort/flow.ft": source})
        self.assertProblem(tree, "src/fort/flow.ft:3: the declaration names "
                                 "test/fort/flow_get_panic_test.ft, which does not exist")

    def test_an_unreachable_site_that_a_test_answers_fails(self):
        source = AST.replace("    if (i > 3) {\n",
                             "    if (i > 3) {\n        // panic-coverage: unreachable: no.\n")
        tree = self.tree({"src/fort/ast.ft": source})
        self.assertProblem(tree, "src/fort/ast.ft:4: the site is declared unreachable, but "
                                 "test/fort/ast_child_panic_test.ft answers it")

    def test_a_declaration_with_an_unknown_kind_fails(self):
        source = AST.replace("    if (n == 0) {\n",
                             "    if (n == 0) {\n        // panic-coverage: untested: later.\n")
        tree = self.tree({"src/fort/ast.ft": source})
        self.assertProblem(tree, "src/fort/ast.ft:10: a declaration is 'unreachable: <reason>' or "
                                 "'tests: <stem> ...'")

    def test_an_unreachable_declaration_needs_a_reason(self):
        source = AST.replace("    if (n == 0) {\n",
                             "    if (n == 0) {\n        // panic-coverage: unreachable:\n")
        tree = self.tree({"src/fort/ast.ft": source})
        self.assertProblem(tree, "src/fort/ast.ft:10: the 'unreachable' declaration is empty")

    def test_a_tests_declaration_needs_a_name(self):
        source = "fn bit(string who) void {\n    // panic-coverage: tests:\n    panic(who);\n}\n"
        tree = self.tree({"src/fort/flow.ft": source})
        out, err = self.assertProblem(tree, "src/fort/flow.ft:2: the 'tests' declaration is empty")
        self.assertIn("src/fort/flow.ft:3: the argument is not one string literal", err)

    def test_unreachable_and_tests_on_one_site_fail(self):
        source = "fn bit(string who) void {\n    // panic-coverage: unreachable: no.\n" \
                 "    // panic-coverage: tests: ast_child\n    panic(who);\n}\n"
        tree = self.tree({"src/fort/flow.ft": source})
        self.assertProblem(tree, "a site takes one unreachable declaration and nothing else")

    def test_two_unreachable_declarations_on_one_site_fail(self):
        source = "fn bit(string who) void {\n    // panic-coverage: unreachable: no.\n" \
                 "    // panic-coverage: unreachable: never.\n    panic(who);\n}\n"
        tree = self.tree({"src/fort/flow.ft": source})
        self.assertProblem(tree, "a site takes one unreachable declaration and nothing else")

    def test_a_tests_declaration_on_a_literal_site_fails(self):
        source = AST.replace("    if (i > 3) {\n",
                             "    if (i > 3) {\n        // panic-coverage: tests: ast_child\n")
        tree = self.tree({"src/fort/ast.ft": source})
        self.assertProblem(tree, "src/fort/ast.ft:3: the site has a literal text, so its tests "
                                 "answer it by that text")

    def test_a_tests_declaration_in_std_fails(self):
        source = "fn f(string who) void {\n    // panic-coverage: tests: vec_pop\n" \
                 "    panic(who);\n}\n"
        tree = self.tree({"std/sort.ft": source})
        self.assertProblem(tree, "std/sort.ft:2: the tests of std are no panic tests")

    def test_a_declaration_above_two_sites_fails(self):
        source = 'fn f(bool b) void {\n    // panic-coverage: unreachable: no.\n' \
                 '    if (b) { panic("flow.a: one"); } else { panic("flow.b: two"); }\n}\n'
        tree = self.tree({"src/fort/flow.ft": source})
        self.assertProblem(tree, "src/fort/flow.ft:2: the declaration stands above more than one "
                                 "site")


class GapList(TreeCase):
    def test_a_line_that_names_no_site_fails(self):
        tree = self.tree({"test/panic_gaps.txt": GAPS + 'src/fort/ast.ft "ast.old: gone"\n'})
        self.assertProblem(tree, 'test/panic_gaps.txt:2: the list names src/fort/ast.ft '
                                 '"ast.old: gone", which is no site')

    def test_a_line_with_the_wrong_module_names_no_site(self):
        tree = self.tree({"test/panic_gaps.txt":
                            'src/fort/types.ft "ast.grow: capacity overflow"\n'})
        out, err = self.assertProblem(tree,
                                      "test/panic_gaps.txt:1: the list names src/fort/types.ft")
        self.assertIn('the site "ast.grow: capacity overflow" has no test', err)

    def test_a_line_that_names_a_tested_site_fails(self):
        tree = self.tree({"test/panic_gaps.txt":
                            GAPS + 'src/fort/ast.ft "ast.child: index out of range"\n'})
        self.assertProblem(tree, "test/panic_gaps.txt:2: test/fort/ast_child_panic_test.ft answers "
                                 "this site, so remove the line")

    def test_a_line_that_names_a_declared_site_fails(self):
        source = AST.replace("    if (n == 0) {\n",
                             "    if (n == 0) {\n        // panic-coverage: unreachable: no.\n")
        tree = self.tree({"src/fort/ast.ft": source})
        self.assertProblem(tree, "test/panic_gaps.txt:1: the site is declared unreachable, so "
                                 "remove the line")

    def test_a_line_twice_fails(self):
        tree = self.tree({"test/panic_gaps.txt": GAPS + GAPS})
        self.assertProblem(tree, "test/panic_gaps.txt:2: the list names this site twice")

    def test_a_malformed_line_fails(self):
        tree = self.tree({"test/panic_gaps.txt": GAPS + "src/fort/ast.ft ast.grow\n"})
        self.assertProblem(tree, 'test/panic_gaps.txt:2: a line of the list is <path> "<text>"')

    def test_a_line_decodes_an_escaped_quote(self):
        tree = self.tree({
            "src/fort/q.ft": 'fn f() void {\n    panic("q.f: the \\"x\\" key");\n}\n',
            "test/panic_gaps.txt": GAPS + 'src/fort/q.ft "q.f: the \\"x\\" key"\n',
        })
        self.assertGreen(tree)


class Scanner(unittest.TestCase):
    def test_a_comment_ends_the_code_of_a_line(self):
        self.assertEqual(coverage.code_of('x(); // panic("a")'), "x(); ")

    def test_a_string_keeps_its_slashes(self):
        self.assertEqual(coverage.code_of('panic("a//b"); // c'), 'panic("a//b"); ')

    def test_a_char_literal_keeps_its_quote(self):
        self.assertEqual(coverage.code_of("f('\"'); // c"), "f('\"'); ")

    def test_an_escaped_quote_does_not_end_a_string(self):
        self.assertEqual(coverage.code_of('panic("a\\"//b"); // c'), 'panic("a\\"//b"); ')

    def test_the_declaration_of_the_runtime_entry_is_no_call(self):
        self.assertEqual(coverage.call_texts("fn panic(char* ptr, u64 len) noreturn {"), [])

    def test_a_name_that_ends_in_panic_is_no_call(self):
        self.assertEqual(coverage.call_texts("return lower_panic(lw, n);"), [])

    def test_a_qualified_panic_is_no_call(self):
        self.assertEqual(coverage.call_texts('rt.panic("x", 1);'), [])

    def test_a_panic_inside_a_string_is_no_call(self):
        self.assertEqual(coverage.call_texts('f("panic(\\"x\\")");'), [])

    def test_a_literal_call_gives_its_decoded_text(self):
        self.assertEqual(coverage.call_texts('panic( "a: \\\\ \\"b\\"" );'), ['a: \\ "b"'])

    def test_another_escape_keeps_its_two_bytes(self):
        self.assertEqual(coverage.call_texts('panic("a\\nb");'), ["a\\nb"])

    def test_a_call_with_an_expression_gives_none(self):
        self.assertEqual(coverage.call_texts("panic(strbuf.view(&text));"), [None])

    def test_a_literal_the_line_does_not_close_gives_none(self):
        self.assertEqual(coverage.call_texts('panic("open'), [None])

    def test_a_call_whose_argument_is_on_the_next_line_gives_none(self):
        self.assertEqual(coverage.call_texts("panic("), [None])

    def test_answers_takes_the_whole_text_with_or_without_the_prefix(self):
        self.assertTrue(coverage.answers("panic: a: b", "a: b"))
        self.assertTrue(coverage.answers("a: b", "a: b"))
        self.assertTrue(coverage.answers(": a: b", "a: b"))
        self.assertFalse(coverage.answers("a:", "a: b"))
        self.assertFalse(coverage.answers("a: b c", "a: b"))
        self.assertFalse(coverage.answers("x panic: a: b", "a: b"))

    def test_aborts_reads_abort_and_signal_abrt(self):
        self.assertTrue(coverage.aborts([("abort", "")]))
        self.assertTrue(coverage.aborts([("abort-macos", "")]))
        self.assertTrue(coverage.aborts([("signal", "ABRT")]))
        self.assertTrue(coverage.aborts([("signal-linux", "ABRT")]))
        self.assertFalse(coverage.aborts([("signal", "TRAP"), ("exit", "1")]))


class Command(unittest.TestCase):
    def test_a_root_without_src_fort_exits_2(self):
        with tempfile.TemporaryDirectory() as empty:
            err = io.StringIO()
            self.assertEqual(coverage.main(["--root", empty], io.StringIO(), err), 2)
            self.assertIn("holds no src/fort", err.getvalue())


if __name__ == "__main__":
    unittest.main()
