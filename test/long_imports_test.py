#!/usr/bin/env python3
"""Test tools/long_imports.py over small repository trees (D9.11, grammar.md 2)."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("long_imports", REPOSITORY / "tools/long_imports.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)

# The modules of a small compiler: two flat modules and one nested one.
COMPILER = {
    "src/fort/ast.ft": "fn a() void {}\n",
    "src/fort/types.ft": "fn t() void {}\n",
    "src/fort/fir/emptiness.ft": "fn e() void {}\n",
}


class Tree:
    """A repository tree in a temporary directory."""

    def __init__(self, files):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        for name, text in {**COMPILER, **files}.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    def read(self, name):
        return (self.root / name).read_text()

    def run(self, *args):
        """Runs the tool; returns (status, stdout)."""
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = tool.main(["--root", str(self.root)] + list(args))
        return status, out.getvalue()

    def close(self):
        self.dir.cleanup()


class LongImportsTest(unittest.TestCase):
    def tree(self, files):
        tree = Tree(files)
        self.addCleanup(tree.close)
        return tree

    def test_short_import_of_a_compiler_module_gets_the_prefix(self):
        tree = self.tree({"src/fort/check.ft": "import ast;\nimport std.io;\nfn c() void {}\n"})
        self.assertEqual(tree.run(), (0, "changed src/fort/check.ft\n"
                                         "long_imports: 1 of 4 files changed\n"))
        self.assertEqual(tree.read("src/fort/check.ft"),
                         "import fort.ast;\nimport std.io;\nfn c() void {}\n")

    def test_nested_path_alias_and_brace_list_get_the_prefix(self):
        text = ("/// doc\n\nimport fir.emptiness as e;\nimport types.{\n    t,\n};\n"
                "import ast;\nfn c() void {}\n")
        tree = self.tree({"src/fort/check.ft": text})
        tree.run()
        self.assertEqual(tree.read("src/fort/check.ft"),
                         "/// doc\n\nimport fort.fir.emptiness as e;\nimport fort.types.{\n"
                         "    t,\n};\nimport fort.ast;\nfn c() void {}\n")

    def test_a_sibling_in_the_library_is_no_local_module(self):
        # src/fort/types.ft stands beside check.ft, and the library imports it by the long path
        tree = self.tree({"src/fort/check.ft": "import types;\n"})
        tree.run()
        self.assertEqual(tree.read("src/fort/check.ft"), "import fort.types;\n")

    def test_a_local_module_keeps_its_short_path(self):
        tree = self.tree({
            "test/fort/support/types.ft": "fn s() void {}\n",
            "test/fort/x/ast.ft": "fn s() void {}\n",
            "test/fort/x/one_test.ft": "import types;\nimport ast;\nimport ast.a;\n"
                                       "import fir.emptiness;\n",
        })
        tree.run()
        # types is the support module, ast and its symbol a are beside the test
        self.assertEqual(tree.read("test/fort/x/one_test.ft"),
                         "import types;\nimport ast;\nimport ast.a;\nimport fort.fir.emptiness;\n")

    def test_a_path_of_no_compiler_module_is_kept(self):
        tree = self.tree({"src/lsp/json.ft": "import lsp.text;\nimport flt;\nimport fort.ast;\n"})
        self.assertEqual(tree.run("--check"), (0, "long_imports: 0 of 4 files would change\n"))

    def test_the_flags_root_moves_to_src_once(self):
        tree = self.tree({
            "test/fort/a_test.ft":
                "//! run\n//! flags: -I ../../src/fort -I support\nimport ast;\n",
            "test/fort/b_test.ft": "//! flags: -I ../../src/fort -I ../fort/support -I ../../src\n",
            "test/fort/c_test.ft": "//! flags: -I ../../src/fort -I ../../src\n",
            "test/fort/d_test.ft": "//! flags: --release\n",
        })
        tree.run()
        self.assertEqual(tree.read("test/fort/a_test.ft"),
                         "//! run\n//! flags: -I ../../src -I support\nimport fort.ast;\n")
        self.assertEqual(tree.read("test/fort/b_test.ft"),
                         "//! flags: -I ../../src -I ../fort/support\n")
        self.assertEqual(tree.read("test/fort/c_test.ft"), "//! flags: -I ../../src\n")
        self.assertEqual(tree.read("test/fort/d_test.ft"), "//! flags: --release\n")

    def test_imports_in_and_after_an_import_selector_get_the_prefix(self):
        text = ('import ast;\n$if ($cfg(target_os) == "linux") {\n    import types;\n'
                '} else $if ($cfg(x) == "}") {\n    import ast as a;\n} else {\n'
                '    import types as t; // { a brace in a comment\n}\n'
                'import fir.emptiness;\nfn c() void {}\n')
        tree = self.tree({"src/fort/check.ft": text})
        tree.run()
        self.assertEqual(tree.read("src/fort/check.ft"), text
                         .replace("import ast;", "import fort.ast;")
                         .replace("import types;", "import fort.types;")
                         .replace("import ast as a;", "import fort.ast as a;")
                         .replace("import types as t;", "import fort.types as t;")
                         .replace("import fir.emptiness;", "import fort.fir.emptiness;"))

    def test_a_declaration_selector_ends_the_header(self):
        text = ('import ast;\n$if ($cfg(target_os) == "linux") {\n    fn f() void {}\n}\n'
                'import types;\n')
        tree = self.tree({"src/fort/check.ft": text})
        tree.run()
        self.assertEqual(tree.read("src/fort/check.ft"),
                         text.replace("import ast;", "import fort.ast;"))

    def test_a_line_after_the_first_declaration_never_changes(self):
        text = 'import ast;\nfn c() string {\n    return "\nimport types;\n";\n}\n'
        tree = self.tree({"src/fort/check.ft": text})
        tree.run()
        self.assertEqual(tree.read("src/fort/check.ft"),
                         text.replace("import ast;", "import fort.ast;"))

    def test_check_writes_nothing_and_exits_1(self):
        tree = self.tree({"src/fort/check.ft": "import ast;\n"})
        self.assertEqual(tree.run("--check"), (1, "would change src/fort/check.ft\n"
                                                  "long_imports: 1 of 4 files would change\n"))
        self.assertEqual(tree.read("src/fort/check.ft"), "import ast;\n")

    def test_a_second_run_changes_nothing(self):
        tree = self.tree({"src/fort/check.ft": "import ast;\nimport fir.emptiness;\n"})
        tree.run()
        self.assertEqual(tree.run(), (0, "long_imports: 0 of 4 files changed\n"))

    def test_a_module_named_fort_stops_the_tool(self):
        for name in ("src/fort/fort.ft", "src/fort/fort/x.ft"):
            with self.subTest(name=name):
                tree = self.tree({name: "fn f() void {}\n", "src/fort/check.ft": "import ast;\n"})
                with self.assertRaises(SystemExit) as caught:
                    tree.run()
                self.assertIn("a long import is ambiguous", str(caught.exception))
                self.assertEqual(tree.read("src/fort/check.ft"), "import ast;\n")


if __name__ == "__main__":
    unittest.main()
