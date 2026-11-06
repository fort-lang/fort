#!/usr/bin/env python3
"""Hold the two sources of std.os together: the same names, and the same text where the targets
do not differ.

`std.os` has one source for each target, std/linux/os.ft and std/darwin/os.ft (stdlib.md 1.6,
2.14). The test holds three things:
- The functions of the file system and of the error text are the same on both targets, so each
  source holds them after one marker line, and the text from that line to the end of the file
  must be the same in both.
- Above the marker, both sources declare the same names, but for the one private name of each
  target in TARGET_ONLY.
- Above the marker, each item that both sources declare has the same text, but for the items in
  PER_TARGET, whose value or body is the fact of a target.
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
# binary, the flag value of O_NONBLOCK and the place of `st_mode` in `struct stat`.
PER_TARGET = {"TARGET", "exe_path", "O_NONBLOCK", "STAT_MODE_WORD", "STAT_MODE_SHIFT"}

DECLARATION = re.compile(
    r"^(?:extern fn|fn) ([A-Za-z_]\w*)\(|^struct ([A-Za-z_]\w*)|^enum ([A-Za-z_]\w*)"
    r"|^[A-Za-z_][\w.\[\]@* ]* ([A-Za-z_]\w*)\s*(?:=|;)"
)


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


def items(lines):
    """The top-level items of `lines`, as a map from each name to the lines of its item. An item
    starts at a line in column 0 that declares a name. A line that ends in `{` opens a body that
    ends at the next `}` in column 0; another item is its one line. Comments, imports and blank
    lines belong to no item."""
    found = {}
    i = 0
    while i < len(lines):
        line = lines[i]
        match = DECLARATION.match(line)
        if match is None or line.startswith(("//", "import ")):
            i += 1
            continue
        name = next(g for g in match.groups() if g)
        end = i
        if line.rstrip().endswith("{") or not line.rstrip().endswith((";", "}")):
            while end < len(lines) and lines[end] != "}":
                end += 1
        found[name] = lines[i : end + 1]
        i = end + 1
    return found


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
    three rules, as a list of messages."""
    found = []
    upper, lower = {}, {}
    for name, text in texts.items():
        upper[name], lower[name] = split(text, name)
    a, b = SOURCES
    line = first_difference(lower[a], lower[b])
    if line:
        found.append(f"{a} and {b} differ at line {line} after the marker")
    declared = {name: items(upper[name]) for name in SOURCES}
    names = {name: set(declared[name]) - TARGET_ONLY[name] for name in SOURCES}
    for name in sorted(names[a] ^ names[b]):
        where = a if name in names[a] else b
        found.append(f"'{name}' stands above the marker of {where} alone")
    for name in sorted(names[a] & names[b]):
        if name not in PER_TARGET and declared[a][name] != declared[b][name]:
            found.append(f"the item '{name}' differs above the marker")
    return found


def read_sources():
    return {name: (REPOSITORY / name).read_text() for name in SOURCES}


class SharedPartTest(unittest.TestCase):
    def test_the_two_sources_hold_the_three_rules(self):
        self.assertEqual(problems(read_sources()), [])

    def test_the_shared_part_holds_the_functions_of_the_file_system(self):
        self.assertGreater(len(shared_part(read_sources()[SOURCES[0]], SOURCES[0])), 200)

    def test_the_items_above_the_marker_are_read(self):
        found = items(split(read_sources()[SOURCES[0]], SOURCES[0])[0])
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
        ):
            self.assertIn(name, found)
        self.assertEqual(found["file_mode"][-1], "}")
        self.assertEqual(len(found["STAT_WORDS"]), 1)

    def test_a_changed_line_of_file_mode_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        texts[a] = texts[a].replace(
            "    if (fstat(fd, &st[0]) != 0) {", "    if (fstat(fd, &st[0]) < 0) {", 1
        )
        self.assertNotEqual(texts[a], read_sources()[a])
        self.assertEqual(problems(texts), ["the item 'file_mode' differs above the marker"])

    def test_a_new_function_above_the_marker_of_one_source_is_seen(self):
        texts = read_sources()
        a = SOURCES[0]
        texts[a] = texts[a].replace(MARKER, "fn extra() void {\n}\n\n" + MARKER, 1)
        self.assertEqual(problems(texts), [f"'extra' stands above the marker of {a} alone"])

    def test_a_changed_line_below_the_marker_is_seen(self):
        texts = read_sources()
        a = SOURCES[1]
        texts[a] = texts[a].replace("u64 PATH_LAST_CAP = 65536;", "u64 PATH_LAST_CAP = 65537;", 1)
        self.assertEqual(len(problems(texts)), 1)
        self.assertIn("after the marker", problems(texts)[0])

    def test_a_per_target_item_can_differ(self):
        texts = read_sources()
        self.assertNotEqual(
            items(split(texts[SOURCES[0]], "a")[0])["exe_path"],
            items(split(texts[SOURCES[1]], "b")[0])["exe_path"],
        )
        self.assertEqual(problems(texts), [])

    def test_a_missing_marker_is_an_error(self):
        with self.assertRaises(ValueError):
            shared_part("fn a() void {}\n", "x.ft")

    def test_a_second_marker_is_an_error(self):
        with self.assertRaises(ValueError):
            shared_part(f"{MARKER}\n{MARKER}\n", "x.ft")

    def test_the_first_difference_is_named(self):
        self.assertEqual(first_difference(["a", "b", "c"], ["a", "b", "c"]), 0)
        self.assertEqual(first_difference(["a", "b", "c"], ["a", "x", "c"]), 2)
        self.assertEqual(first_difference(["a", "b"], ["a", "b", "c"]), 3)
        self.assertEqual(first_difference(["a", "b", "c"], ["a"]), 2)


if __name__ == "__main__":
    sys.exit(unittest.main())
