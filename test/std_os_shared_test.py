#!/usr/bin/env python3
"""Hold the two sources of std.os together: the same names, and the same text where the targets
do not differ.

`std.os` has one source for each target, std/linux/os.ft and std/darwin/os.ft (stdlib.md 1.6,
2.14). The test holds five things:
- The functions of the file system and of the error text are the same on both targets, so each
  source holds them after one marker line, and the text from that line to the end of the file
  must be the same in both.
- Above the marker, both sources declare the same names, but for the one name of each source in
  TARGET_ONLY.
- Above the marker, each item that both sources declare has the same text, with the comment
  lines directly above it, but for the items in PER_TARGET, whose value or body is the fact of a
  target.
- Above the marker, each line is a blank line, a comment, an import or a line of an item. So a
  `$if` block, or another form that the test does not read, is an error.
- Both sources have the same imports.
A change to one copy alone makes this test fail, and its message names the line or the item.
"""

from pathlib import Path
import re
import sys
import unittest

REPOSITORY = Path(__file__).resolve().parents[1]
SOURCES = ("std/linux/os.ft", "std/darwin/os.ft")
MARKER = "// ---- the file system and the error text, the same on both targets ----"
# The one name of each source that the other does not declare (stdlib.md 2.14).
TARGET_ONLY = {"std/linux/os.ft": {"PROC_SELF_EXE"}, "std/darwin/os.ft": {"_NSGetExecutablePath"}}
# The items above the marker whose text is per target: the triple, the path of the running
# binary, the flag value of O_NONBLOCK, the place of `st_mode` in `struct stat`, and the two
# functions that give C a `mode_t`, which is a u32 on Linux and a u16 on Darwin.
PER_TARGET = {
    "TARGET",
    "exe_path",
    "O_NONBLOCK",
    "STAT_MODE_WORD",
    "STAT_MODE_SHIFT",
    "make_fifo",
    "set_umask",
}

# A line that starts an item and the name that it declares: a function, an extern, a struct, an
# enum, a global of a function type, or another global.
DECLARATION = re.compile(
    r"^(?:extern fn|fn) ([A-Za-z_]\w*)\(|^struct ([A-Za-z_]\w*)|^enum ([A-Za-z_]\w*)"
    r"|^fn \(.*\) [^=;]*?([A-Za-z_]\w*)\s*(?:=|;)"
    r"|^[A-Za-z_][\w.\[\]@* ]* ([A-Za-z_]\w*)\s*(?:=|;)"
)

# A comment after the `;` or the `{` that ends the code of a line.
TRAILING_COMMENT = re.compile(r"([;{])\s*//.*$")
# A string literal, which the test blanks before it reads the code of a line.
STRING = re.compile(r'"(\\.|[^"\\])*"')
# Code after the `;` that ends the first item of a line: a second item on that line.
SECOND_ITEM = re.compile(r";\s*\S")


def split(text, name):
    """The lines of `text` above the marker line and from it to the end. Raises ValueError when
    the marker does not stand once in `text`."""
    lines = text.splitlines()
    found = [i for i, line in enumerate(lines) if line == MARKER]
    if len(found) != 1:
        raise ValueError(f"{name}: the marker line stands {len(found)} times, not once")
    return lines[: found[0]], lines[found[0] :]


def shared_part(text, name):
    """The lines of `text` from the marker line to the end."""
    return split(text, name)[1]


def is_comment(line):
    return line.startswith("//")


def scan(lines):
    """The top-level items of `lines` and the lines that are no item. The items are a map from
    each name to the lines of its item: the comment lines directly above it, then its own lines.
    An item starts at a line in column 0 that declares a name. A line that ends in `{` opens a body
    that ends at the next `}` in column 0; another item is its one line. A blank line, a comment
    line and an import belong to no item. Each other line is in the list of the 1-based numbers of
    the lines that are no item, and so is the first line of an item that holds code after the `;`
    of its first item, such as a second item."""
    found = {}
    strange = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.strip() == "" or is_comment(line) or line.startswith("import "):
            i += 1
            continue
        match = DECLARATION.match(line)
        if match is None:
            strange.append(i + 1)
            i += 1
            continue
        name = next(g for g in match.groups() if g)
        code = TRAILING_COMMENT.sub(r"\1", STRING.sub('""', line)).rstrip()
        if SECOND_ITEM.search(code):
            strange.append(i + 1)
        start = i
        while start > 0 and is_comment(lines[start - 1]):
            start -= 1
        end = i
        if code.endswith("{") or not code.endswith((";", "}")):
            while end < len(lines) and lines[end] != "}":
                end += 1
        found[name] = lines[start : end + 1]
        i = end + 1
    return found, strange


def items(lines):
    """The top-level items of `lines`, as scan gives them."""
    return scan(lines)[0]


def imports(lines):
    return sorted(line for line in lines if line.startswith("import "))


def first_difference(a, b):
    """The 1-based number of the first line of `a` and `b` that differs, or 0 when the two are
    the same."""
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i + 1
    if len(a) != len(b):
        return min(len(a), len(b)) + 1
    return 0


def problems(texts):
    """Each way in which the sources `texts` (a map from a path to its text) do not hold the
    five rules, as a list of messages."""
    found = []
    upper, lower = {}, {}
    for name, text in texts.items():
        upper[name], lower[name] = split(text, name)
    a, b = SOURCES
    line = first_difference(lower[a], lower[b])
    if line:
        found.append(f"{a} and {b} differ at line {line} after the marker")
    declared = {}
    for name in SOURCES:
        declared[name], strange = scan(upper[name])
        for number in strange:
            found.append(f"line {number} of {name} is no item, comment or import")
    names = {name: set(declared[name]) - TARGET_ONLY[name] for name in SOURCES}
    for name in sorted(names[a] ^ names[b]):
        where = a if name in names[a] else b
        found.append(f"'{name}' stands above the marker of {where} alone")
    for name in sorted(names[a] & names[b]):
        if name not in PER_TARGET and declared[a][name] != declared[b][name]:
            found.append(f"the item '{name}' differs above the marker")
    if imports(upper[a]) != imports(upper[b]):
        found.append("the imports differ above the marker")
    return found


def read_sources():
    return {name: (REPOSITORY / name).read_text() for name in SOURCES}


class SharedPartTest(unittest.TestCase):
    def test_the_two_sources_hold_the_five_rules(self):
        self.assertEqual(problems(read_sources()), [])

    def test_the_shared_part_holds_the_functions_of_the_file_system(self):
        self.assertGreater(len(shared_part(read_sources()[SOURCES[0]], SOURCES[0])), 200)

    def test_the_items_above_the_marker_are_read(self):
        found, strange = scan(split(read_sources()[SOURCES[0]], SOURCES[0])[0])
        self.assertEqual(strange, [])
        for name in (
            "TARGET",
            "exe_path",
            "real_path",
            "file_mode",
            "path_mode",
            "fstat",
            "stat",
            "open_nonblocking",
            "O_NONBLOCK",
            "STAT_WORDS",
            "PROC_SELF_EXE",
            "make_fifo",
            "set_umask",
        ):
            self.assertIn(name, found)
        self.assertEqual(found["file_mode"][-1], "}")
        self.assertTrue(found["file_mode"][0].startswith("/// "))
        self.assertEqual(len([x for x in found["MODE_BITS"] if not is_comment(x)]), 1)

    def test_a_changed_line_of_file_mode_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        texts[a] = texts[a].replace(
            "    if (fstat(fd, &st[0]) != 0) {", "    if (fstat(fd, &st[0]) < 0) {", 1
        )
        self.assertNotEqual(texts[a], read_sources()[a])
        self.assertEqual(problems(texts), ["the item 'file_mode' differs above the marker"])

    def test_a_changed_doc_line_of_file_mode_is_seen(self):
        texts = read_sources()
        a = SOURCES[0]
        old = "/// Writes the type bits and the permission bits of the file that `fd` names"
        texts[a] = texts[a].replace(old, "/// Writes nothing of the file that `fd` names", 1)
        self.assertNotEqual(texts[a], read_sources()[a])
        self.assertEqual(problems(texts), ["the item 'file_mode' differs above the marker"])

    def test_a_changed_comment_of_a_shared_global_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        old = "// The buffer sizes the path is read with."
        texts[a] = texts[a].replace(old, "// The buffer sizes.", 1)
        self.assertNotEqual(texts[a], read_sources()[a])
        self.assertEqual(
            problems(texts), ["the item 'EXE_PATH_FIRST_CAP' differs above the marker"]
        )

    def test_a_new_function_above_the_marker_of_one_source_is_seen(self):
        texts = read_sources()
        a = SOURCES[0]
        texts[a] = texts[a].replace(MARKER, "fn extra() void {\n}\n\n" + MARKER, 1)
        self.assertEqual(problems(texts), [f"'extra' stands above the marker of {a} alone"])

    def test_a_conditional_block_above_the_marker_is_seen(self):
        texts = read_sources()
        a = SOURCES[0]
        block = '$if ($cfg(target_arch) == "x86_64") {\n    i32 EXTRA = 1;\n}\n\n'
        texts[a] = texts[a].replace(MARKER, block + MARKER, 1)
        upper = split(texts[a], a)[0]
        first = upper.index('$if ($cfg(target_arch) == "x86_64") {') + 1
        self.assertEqual(
            problems(texts),
            [
                f"line {first} of {a} is no item, comment or import",
                f"line {first + 1} of {a} is no item, comment or import",
                f"line {first + 2} of {a} is no item, comment or import",
            ],
        )

    def test_a_global_of_a_function_type_above_the_marker_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        texts[a] = texts[a].replace(MARKER, "fn (i32) void HOOK = null;\n\n" + MARKER, 1)
        self.assertEqual(problems(texts), [f"'HOOK' stands above the marker of {a} alone"])

    def test_a_second_global_on_a_line_of_a_comment_is_seen(self):
        texts = read_sources()
        a = SOURCES[0]
        added = "u64 MODE_BITS = 0xFFFF; // the low 16 bits\nu64 EXTRA2 = 1;"
        texts[a] = texts[a].replace("u64 MODE_BITS = 0xFFFF;", added, 1)
        self.assertEqual(
            problems(texts),
            [
                f"'EXTRA2' stands above the marker of {a} alone",
                "the item 'MODE_BITS' differs above the marker",
            ],
        )

    def test_a_second_item_on_the_line_of_a_per_target_item_is_seen(self):
        texts = read_sources()
        a = SOURCES[0]
        old = "u64 STAT_MODE_SHIFT = 0;"
        texts[a] = texts[a].replace(old, old + " u64 EXTRA = 1;", 1)
        number = split(texts[a], a)[0].index(old + " u64 EXTRA = 1;") + 1
        self.assertEqual(problems(texts), [f"line {number} of {a} is no item, comment or import"])

    def test_a_second_item_on_the_line_of_a_shared_item_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        old = "u64 MODE_BITS = 0xFFFF;"
        texts[a] = texts[a].replace(old, old + " u64 EXTRA = 1;", 1)
        number = split(texts[a], a)[0].index(old + " u64 EXTRA = 1;") + 1
        self.assertEqual(
            problems(texts),
            [
                f"line {number} of {a} is no item, comment or import",
                "the item 'MODE_BITS' differs above the marker",
            ],
        )

    def test_a_semicolon_in_a_string_is_no_second_item(self):
        found, strange = scan(['string S = "a; b";', "u64 X = 1; // a; b"])
        self.assertEqual(strange, [])
        self.assertEqual(sorted(found), ["S", "X"])

    def test_an_import_of_one_source_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        texts[a] = texts[a].replace("import std.str;\n", "import std.str;\nimport std.io;\n", 1)
        self.assertEqual(problems(texts), ["the imports differ above the marker"])

    def test_a_changed_line_below_the_marker_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        texts[a] = texts[a].replace("u64 PATH_LAST_CAP = 65536;", "u64 PATH_LAST_CAP = 65537;", 1)
        self.assertEqual(len(problems(texts)), 1)
        self.assertIn("after the marker", problems(texts)[0])

    def test_a_per_target_item_can_differ(self):
        texts = read_sources()
        for name in ("exe_path", "make_fifo", "set_umask", "STAT_MODE_WORD"):
            self.assertNotEqual(
                items(split(texts[SOURCES[0]], "a")[0])[name],
                items(split(texts[SOURCES[1]], "b")[0])[name],
            )
        self.assertEqual(problems(texts), [])

    def test_a_missing_marker_is_an_error(self):
        with self.assertRaises(ValueError):
            shared_part("fn a() void {}\n", "x.ft")

    def test_a_second_marker_is_an_error(self):
        with self.assertRaises(ValueError):
            shared_part(f"{MARKER}\n{MARKER}\n", "x.ft")

    def test_a_line_that_is_no_item_is_named(self):
        found, strange = scan(["import std.libc;", "", "// c", "$if (x) {", "    i32 A = 1;", "}"])
        self.assertEqual(found, {})
        self.assertEqual(strange, [4, 5, 6])

    def test_the_comment_lines_directly_above_belong_to_the_item(self):
        found = items(["// a", "", "// b", "/// c", "u64 X = 1;", "u64 Y = 2;"])
        self.assertEqual(found["X"], ["// b", "/// c", "u64 X = 1;"])
        self.assertEqual(found["Y"], ["u64 Y = 2;"])

    def test_the_first_difference_is_named(self):
        self.assertEqual(first_difference(["a", "b", "c"], ["a", "b", "c"]), 0)
        self.assertEqual(first_difference(["a", "b", "c"], ["a", "x", "c"]), 2)
        self.assertEqual(first_difference(["a", "b"], ["a", "b", "c"]), 3)
        self.assertEqual(first_difference(["a", "b", "c"], ["a"]), 2)


if __name__ == "__main__":
    sys.exit(unittest.main())
