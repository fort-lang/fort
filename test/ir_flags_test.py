#!/usr/bin/env python3
"""Hold the emitted IR free of each flag and attribute that adds an undefined state.

The census of `notes/compiler.md` 9 lists each construct of the emitted IR that has a
precondition in the LLVM language reference. This test holds the part of it that the text of a
module shows. It compiles each run entry of test/lang (the directories `run` and `programs`), the
compiler (src/fort/main.ft) and the language server (src/lsp/main.ft) with `-S`, for the Linux
and the Darwin target, in the checked and in the release mode, and it reads each module. Each
module holds the functions of `std` that the program reaches. The test does not compile the unit
tests of test/fort: each one holds the modules of the compiler again. It holds five things:
- Each token of ZERO stands 0 times outside the header lines of a module: a flag that makes a
  result poison (`nsw`, `exact`, a fast-math flag), an attribute that makes a call undefined
  (`noundef`, `nonnull`, `dereferenceable`), `undef`, `poison`, a plain `fptosi`, and each
  instruction that the emitter does not write (`phi`, `select`, `inttoptr`, a `tail` call).
- Each token of HEADER_ONLY stands 0 times outside the header lines too. A header line is an
  `attributes` line or a `declare` line, and the test holds each one at a text of ATTRIBUTES, of
  INTRINSICS or of DECLARE. So `noalias`, `willreturn` and `memory(...)` stand only on the
  declaration of an intrinsic, with the attributes that LLVM gives that intrinsic.
- Each `inbounds` stands in one of the three forms of GEP_SHAPES, and each `unreachable` stands
  after a call of a failure entry of `std.rt`, after `llvm.trap`, or in a block that no path from
  `entry` reaches. The test walks the blocks of the function for the third form.
- Each `alloca` has a fixed size and stands in the block `entry`. Each `load` and each `store`
  gives its alignment. Each call of `llvm.memcpy` and of `llvm.memset` has a fixed length and is
  not volatile. An `align` stands on no other line of a function. Each definition carries the
  attribute group 0, 1 or 8, and a call carries the group 3 or none. No module has a
  `target datalayout` line or a metadata attachment.
- The shapes that the corpus shows are the shapes of EXPECTED_SHAPES. A shape that the corpus
  does not show any more turns the test red, so the census loses its row in the same change.

The test holds the count of the shapes of a token and not the count of its occurrences: an
occurrence count changes with each new entry of the corpus and with each edit of `std`, which each
module holds. The test prints the occurrence counts of each target and mode. A count with the
prefix `header` is the count of a HEADER_ONLY token on the header lines. A control module
holds each token in each form that is no token (a name, a string, a comment), so the test cannot
pass when it counts nothing or counts a name.

`--root` names the tree that the test reads: its std and its test/lang. The harness module
run_tests comes from the tree of this script.
"""

import argparse
import collections
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "lang"))
import run_tests  # noqa: E402

TARGETS = (("linux", "x86_64-linux-gnu"), ("darwin", "arm64-apple-macosx11.0.0"))
MODES = (("checked", []), ("release", ["--release"]))

# The directories of test/lang whose run entries the test compiles.
CORPUS_TOPS = ("run/", "programs/")

# The run entries at the change that added this test, as a floor, so that a parallel change that
# adds an entry does not turn the test red, and a pattern that finds none does.
RUN_ENTRIES = 602

# The products that the test compiles from the top of the tree with the search root `src`.
PRODUCTS = ("src/fort/main.ft", "src/lsp/main.ft")

# The tokens that stand 0 times in the body of a module. The census gives the reason of each.
ZERO = (
    # a flag that makes the result of an instruction poison
    "nsw", "nuw", "exact", "nneg", "disjoint", "inrange",
    # the fast-math flags
    "nnan", "ninf", "nsz", "arcp", "contract", "afn", "reassoc", "fast",
    # an attribute that makes a call or a return undefined for some value
    "noundef", "nonnull", "dereferenceable", "dereferenceable_or_null", "mustprogress",
    "readnone", "allocsize", "allockind", "alwaysinline",
    # an attribute of a parameter that adds a precondition to a call
    "byval", "byref", "inalloca", "preallocated", "nest", "returned", "swifterror", "swiftself",
    "inreg", "align_stack",
    # a value with no defined bits
    "undef", "poison",
    # the float to integer instructions that are poison out of range (D3.14)
    "fptosi", "fptoui",
    # the instructions that the emitter does not write
    "phi", "select", "freeze", "inttoptr", "tail", "musttail", "notail", "invoke", "callbr",
    "volatile", "atomic", "va_arg", "addrspacecast", "asm", "indirectbr", "blockaddress",
    "cmpxchg", "atomicrmw", "landingpad", "resume", "personality", "cleanuppad", "catchpad",
    "bitcast", "insertvalue", "extractelement", "insertelement", "shufflevector",
)

# The tokens that stand only on a header line of a module.
HEADER_ONLY = (
    "noalias", "nocapture", "readonly", "writeonly", "immarg", "willreturn", "nofree", "nosync",
    "nocallback", "speculatable", "memory", "noreturn", "nounwind", "cold", "nobuiltin",
)

# The text of each attribute group that a module can hold (`attr_text`, src/fort/gen_data.ft).
FRAME = '"frame-pointer"="all" "probe-stack"="inline-asm"'
ATTRIBUTES = {
    0: "nounwind " + FRAME,
    1: "noreturn nounwind " + FRAME,
    3: "nobuiltin",
    4: "nocallback nofree nosync nounwind speculatable willreturn memory(none)",
    5: "nocallback nofree nounwind willreturn memory(argmem: readwrite)",
    6: "nocallback nofree nounwind willreturn memory(argmem: write)",
    7: "cold noreturn nounwind memory(inaccessiblemem: write)",
    8: "cold noreturn nounwind " + FRAME,
}

# The declaration of each intrinsic that a module can hold, by the name of its shape. The
# attributes of each line are the ones that LLVM 18 gives the intrinsic.
INTRINSICS = (
    ("overflow", re.compile(
        r"^declare \{ i(8|16|32|64), i1 \} @llvm\.[su](add|sub|mul)\.with\.overflow\.i\1"
        r"\(i\1, i\1\) #4$")),
    ("saturating", re.compile(
        r"^declare i(8|16|32|64) @llvm\.fpto[su]i\.sat\.i\1\.f(32|64)\((float|double)\) #4$")),
    ("memcpy", re.compile(
        r"^declare void @llvm\.memcpy\.p0\.p0\.i64\(ptr noalias nocapture writeonly, "
        r"ptr noalias nocapture readonly, i64, i1 immarg\) #5$")),
    ("memset", re.compile(
        r"^declare void @llvm\.memset\.p0\.i64\("
        r"ptr nocapture writeonly, i8, i64, i1 immarg\) #6$")),
    ("trap", re.compile(r"^declare void @llvm\.trap\(\) #7$")),
)

# The declaration of a function that is no intrinsic: an `extern fn`, which carries `nobuiltin`,
# and a fort function of an interface, which carries the group 0, 1 or 8.
DECLARE = (
    ("extern", re.compile(r"^declare [^@]*@[A-Za-z_][A-Za-z_0-9]*\(.*\) nobuiltin$")),
    ("interface", re.compile(r"^declare dso_local [^@]*@.*\) #[018]$")),
)

# A name, in the text of a module with each string changed to `Q`: `@Q`, `%Q`, `@main`, `%t12`.
NAME = re.compile(r"[@%](?:Q|[-A-Za-z$._0-9]+)")
STRING = re.compile(r'c?"[^"\n]*"')
COMMENT = re.compile(r"[ \t]*;[^\n]*")
WORD = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")

# The forms of a `getelementptr inbounds`, on a line with each name changed to `V`
# (`gen_gep_field`, `gen_gep_array` and `gen_gep_element`, src/fort/gen.ft).
GEP_SHAPES = (
    ("gep-field", re.compile(r"^  V = getelementptr inbounds V, ptr V, i32 0, i32 \d+$")),
    ("gep-array", re.compile(
        r"^  V = getelementptr inbounds \[\d+ x .+\], ptr V, i64 0, i64 (V|\d+)$")),
    ("gep-element", re.compile(r"^  V = getelementptr inbounds [^,]+, ptr V, i64 (V|\d+)$")),
)

# A call of `llvm.memcpy` and of `llvm.memset` (`gen_memcpy` and `gen_memset_zero`): the length is
# a literal, the byte of a fill is 0, and the last argument says that the call is not volatile.
COPY = re.compile(
    r"^  call void V\(ptr align \d+ V, ptr align \d+ V, i64 \d+, i1 false\)$")
FILL = re.compile(r"^  call void V\(ptr align \d+ V, i8 0, i64 \d+, i1 false\)$")

ALLOCA = re.compile(r"^  V = alloca [^,]+, align \d+$")
LOAD = re.compile(r"^  V = load [^,]+, ptr V, align \d+$")
STORE = re.compile(r"^  store .+, ptr V, align \d+$")
DEFINE = re.compile(r"^define dso_local .*\) #([018]) \{$")
CALL_GROUP = re.compile(r"^  (?:V = )?call .*\) #(\d+)$")
ATTRIBUTES_LINE = re.compile(r"^attributes #(\d+) = \{ (.*) \}$")
BLOCK_LABEL = re.compile(r"^([-A-Za-z$._0-9]+):")
TARGET_LABEL = re.compile(r"label %([-A-Za-z$._0-9]+)")
CONSTANT_BR = re.compile(
    r"^  br i1 (true|false), label %([-A-Za-z$._0-9]+), label %([-A-Za-z$._0-9]+)$")
CONSTANT_SWITCH = re.compile(r"^  switch \S+ (-?\d+|true|false), label %([-A-Za-z$._0-9]+) \[$")
SWITCH_CASE = re.compile(r"^    \S+ (-?\d+|true|false), label %([-A-Za-z$._0-9]+)$")
TRAP_CALL = "  call void @llvm.trap()"
FAIL_CALL = re.compile(r'^  call void @"std\.rt\.([a-z_]+)"\(')

# The functions of `std.rt` that a failed check calls (`check_entry`, src/fort/fir.ft, and the
# user checks `assert` and `panic`). Each one is `noreturn`.
FAIL_ENTRIES = (
    "fail_overflow", "fail_shift", "fail_div_zero", "fail_div_overflow", "fail_bounds",
    "fail_span", "fail_alloc_count", "fail_enum", "assert_fail", "panic",
)

# The shapes that the corpus shows, of each construct that stands above 0 times. The census of
# notes/compiler.md 9 gives each shape a row.
EXPECTED_SHAPES = {
    "inbounds": {"gep-field", "gep-array", "gep-element"},
    "unreachable": {"after-fail", "after-trap", "no-path"},
    "intrinsic": {"overflow", "saturating", "memcpy", "memset", "trap"},
    "declare": {"extern"},
    "attributes": {0, 1, 3, 4, 5, 6, 7, 8},
    "define": {0, 1, 8},
    "call": {3},
}

# The control module: each token of ZERO and of HEADER_ONLY in a form that is no token, and one
# line for each thing that the scan finds. test_the_scan_reads_the_control gives the counts.
CONTROL = """target triple = "x86_64-unknown-linux-gnu"
@"m.nsw" = dso_local constant i32 0, align 4
@.str.0 = private unnamed_addr constant [22 x i8] c"exact undef ; poison\\22\\00", align 1
define dso_local i32 @"m.fast"(i32 %nuw.in, ptr noalias %undef.in) #0 {
entry:
  %poison.0 = alloca i32, align 4
  %tmp0 = alloca [4 x i32], align 4
  store i32 %nuw.in, ptr %poison.0, align 4 ; nsw exact
  %t0 = load i32, ptr %poison.0, align 4
  %t1 = add nsw i32 %t0, 1
  %t2 = getelementptr inbounds [4 x i32], ptr %tmp0, i64 0, i64 3
  %t3 = getelementptr inbounds i32, ptr %undef.in, i64 %t1
  %t4 = getelementptr inbounds %struct.m.exact, ptr %undef.in, i32 0, i32 1
  %t5 = getelementptr inbounds i8, ptr %undef.in, i32 4
  %t6 = fptosi double 1.0 to i32
  %t7 = call i32 @llvm.fptosi.sat.i32.f64(double 1.0)
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %undef.in, i64 16, i1 false)
  call void @llvm.memset.p0.i64(ptr align 4 %tmp0, i8 0, i64 16, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %undef.in, i64 %t0, i1 false)
  call void @llvm.memset.p0.i64(ptr align 4 %tmp0, i8 1, i64 16, i1 false)
  call void @"m.reached"(ptr align 8 %tmp0)
  %t8 = icmp eq i32 %t1, 0
  br i1 %t8, label %fail0, label %bb1
fail0:
  call void @"std.rt.fail_overflow"(ptr @.file.0, i32 1, i32 1)
  unreachable
bb1:
  %t9 = alloca i32, i32 %t1, align 4
  %t10 = load i32, ptr %t9
  call void @llvm.trap()
  unreachable
bb2:
  unreachable
bb3:
  br label %exact
exact:
  unreachable
}
define dso_local void @"m.folded"(i32 %x) #1 {
entry:
  br i1 true, label %loop, label %after
loop:
  br i1 false, label %after, label %pick
pick:
  switch i32 2, label %other [
    i32 1, label %one
    i32 2, label %two
  ]
one:
  unreachable
two:
  switch i32 %x, label %other [
    i32 1, label %loop
    i32 2, label %three
  ]
three:
  switch i32 7, label %loop [
    i32 1, label %one
  ]
other:
  unreachable
after:
  unreachable
}
define dso_local void @"m.reached"() #2 {
entry:
  call void @abort() #9
  br label %end
end:
  unreachable
}
declare void @abort() nobuiltin
declare i32 @strlen(ptr) noalias
declare i32 @llvm.fptosi.sat.i32.f64(double) #4
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1) #5
attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #9 = { mustprogress }
"""

OPTIONS = None


def normalize(text):
    """The text of a module with each string `Q`, no comment, and each name `V`. The count of
    lines stays, so line `i` of the result is line `i` of the module."""
    text = STRING.sub("Q", text)
    text = COMMENT.sub("", text)
    return NAME.sub("V", text)


def no_path(raw, at):
    """Whether no path from `entry` reaches the block of line `at`, in the function that holds
    that line. `raw` is the lines of the module. The walk takes each edge of a terminator, but for
    a `br` or a `switch` on a constant it takes the one edge that the constant selects. That is the
    walk of rule V9 of the FIR verifier (`mark_reachable`, src/fort/fir_verify.ft)."""
    first = at
    while not raw[first].startswith("define "):
        first -= 1
    blocks = {}
    block = None
    here = None
    constant = None
    i = first + 1
    while raw[i] != "}":
        line = raw[i]
        label = BLOCK_LABEL.match(line)
        folded = CONSTANT_BR.match(line)
        switch = CONSTANT_SWITCH.match(line)
        case = SWITCH_CASE.match(line)
        if label:
            block = label.group(1)
            blocks[block] = set()
        elif block is None:
            pass
        elif folded:
            blocks[block].add(folded.group(2) if folded.group(1) == "true" else folded.group(3))
        elif switch:
            constant = switch.group(1)
            blocks[block].add(switch.group(2))
        elif case and constant is not None:
            if case.group(1) == constant:
                blocks[block] = {case.group(2)}
        else:
            constant = None
            blocks[block].update(TARGET_LABEL.findall(line))
        if i == at:
            here = block
        i += 1
    seen = set()
    stack = ["entry"]
    while stack:
        block = stack.pop()
        if block in seen:
            continue
        seen.add(block)
        stack.extend(blocks.get(block, ()))
    return here is not None and here not in seen


def scan(text):
    """The findings of one module: (counts, shapes, problems). `counts` gives the occurrences of
    each token and of each shape, `shapes` gives the shapes by construct, and `problems` is a list
    of texts, one for each line that breaks a rule of this test."""
    counts = collections.Counter()
    shapes = collections.defaultdict(set)
    problems = []
    raw = text.split("\n")
    lines = normalize(text).split("\n")
    body = []
    entry_block = False
    for i, line in enumerate(lines):
        if not line:
            continue
        head = raw[i]
        if head.startswith(("attributes #", "declare ")):
            for word in WORD.findall(line):
                if word in HEADER_ONLY:
                    counts["header " + word] += 1
        if head.startswith("attributes #"):
            m = ATTRIBUTES_LINE.match(head)
            if m and ATTRIBUTES.get(int(m.group(1))) == m.group(2):
                shapes["attributes"].add(int(m.group(1)))
            else:
                problems.append("an attribute group that the test does not know: " + head)
            continue
        if head.startswith("declare "):
            table, construct = DECLARE, "declare"
            if "@llvm." in head:
                table, construct = INTRINSICS, "intrinsic"
            found = [name for name, form in table if form.match(head)]
            if found:
                shapes[construct].add(found[0])
                counts[construct + " " + found[0]] += 1
            else:
                problems.append("a declaration that the test does not know: " + head)
            continue
        if head.startswith("target datalayout"):
            problems.append("a data layout line: " + head)
            continue
        label = BLOCK_LABEL.match(head)
        if label:
            entry_block = label.group(1) == "entry"
            continue
        body.append(line)
        if head.startswith("@"):
            counts["global"] += 1
            continue
        if "!" in line:
            problems.append("a metadata attachment: " + head)
        if head.startswith("define "):
            m = DEFINE.match(head)
            if m:
                shapes["define"].add(int(m.group(1)))
            else:
                problems.append("a definition with no group 0, 1 or 8: " + head)
            continue
        if " alloca " in line:
            counts["alloca"] += 1
            if not ALLOCA.match(line):
                problems.append("an alloca with a count: " + head)
            elif not entry_block:
                problems.append("an alloca outside the entry block: " + head)
        elif " = load " in line:
            counts["load"] += 1
            if not LOAD.match(line):
                problems.append("a load with no alignment: " + head)
        elif line.startswith("  store "):
            counts["store"] += 1
            if not STORE.match(line):
                problems.append("a store with no alignment: " + head)
        elif " inbounds " in line:
            found = [name for name, form in GEP_SHAPES if form.match(line)]
            if found:
                shapes["inbounds"].add(found[0])
                counts[found[0]] += 1
            else:
                problems.append("an inbounds of no known form: " + head)
        elif line == "  unreachable":
            before = raw[i - 1]
            fail = FAIL_CALL.match(before)
            if fail and fail.group(1) in FAIL_ENTRIES:
                shape = "after-fail"
            elif before == TRAP_CALL:
                shape = "after-trap"
            elif no_path(raw, i):
                shape = "no-path"
            else:
                shape = None
                problems.append("an unreachable that a path from entry reaches, line %d" % (i + 1))
            if shape:
                shapes["unreachable"].add(shape)
                counts[shape] += 1
        elif head.startswith("  call void @llvm.memcpy."):
            counts["memcpy call"] += 1
            if not COPY.match(line):
                problems.append("a copy of no known form: " + head)
        elif head.startswith("  call void @llvm.memset."):
            counts["memset call"] += 1
            if not FILL.match(line):
                problems.append("a fill of no known form: " + head)
        elif " align " in line:
            problems.append("an align outside a load, a store, an alloca and a copy: " + head)
        elif " call " in line or line.startswith("  call "):
            m = CALL_GROUP.match(line)
            if m:
                group = int(m.group(1))
                shapes["call"].add(group)
                if group != 3:
                    problems.append("a call with the group %d: %s" % (group, head))
    words = collections.Counter(WORD.findall("\n".join(body)))
    for token in ZERO + HEADER_ONLY:
        if words[token]:
            counts[token] = words[token]
            problems.append("%d of the token %s" % (words[token], token))
    for token in ("inbounds", "unreachable", "sret", "zeroext", "signext"):
        counts[token] = words[token]
    return counts, shapes, problems


def std_root(target, out):
    """The standard root of `target`, as the build assembles it, in `out`."""
    script = os.path.join(OPTIONS.root, "tools", "assemble_std.sh")
    subprocess.run(["bash", script, target, os.path.join(OPTIONS.root, "std"), out],
                   check=True, capture_output=True)
    return out


def entries():
    """The run entries of the corpus with their flags, as run_tests finds them."""
    lang = pathlib.Path(OPTIONS.root) / "test" / "lang"
    tests, problems = run_tests.discover(lang)
    if problems:
        raise AssertionError("%s: %s" % (lang, problems))
    found = []
    for test in tests:
        run_tests.load_test(lang, test)
        if test.kind == "run" and test.path.startswith(CORPUS_TOPS):
            found.append(test)
    return found


def module_of(job):
    """The findings of the module of one entry: (label, counts, shapes, problems). The compiler
    must exit 0 and write the module."""
    fort, triple, std, cwd, flags, entry, out = job
    argv = [fort, "--target", triple, "--std-dir", std] + flags + ["-S", "-o", out, entry]
    proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    label = "%s %s" % (" ".join(flags), entry)
    if proc.returncode != 0:
        return label, None, None, ["%s exits %d: %s" % (argv, proc.returncode, proc.stderr[-500:])]
    with open(out) as handle:
        text = handle.read()
    os.remove(out)
    counts, shapes, problems = scan(text)
    return label, counts, shapes, problems


class Scan(unittest.TestCase):
    def test_the_scan_reads_the_control(self):
        counts, shapes, problems = scan(CONTROL)
        want = {
            "nsw": 1, "fptosi": 1, "noalias": 1, "mustprogress": 0,
            "inbounds": 4, "gep-array": 1, "gep-element": 1, "gep-field": 1,
            "unreachable": 8, "after-fail": 1, "after-trap": 1, "no-path": 4,
            "alloca": 3, "load": 2, "store": 1, "global": 2, "memcpy call": 2, "memset call": 2,
            "header noalias": 1, "header nobuiltin": 1, "header willreturn": 1, "header memory": 1,
            "header nounwind": 2,
            "exact": 0, "undef": 0, "poison": 0, "nuw": 0, "fast": 0,
        }
        self.assertEqual({key: counts[key] for key in want}, want)
        self.assertEqual(shapes["define"], {0, 1})
        self.assertEqual(shapes["call"], {9})
        self.assertEqual(shapes["attributes"], {0, 4})
        self.assertEqual(shapes["intrinsic"], {"saturating"})
        self.assertEqual(shapes["declare"], {"extern"})
        kinds = sorted(problem.split(":")[0].split(", line")[0] for problem in problems)
        self.assertEqual(kinds, [
            "1 of the token fptosi",
            "1 of the token noalias",
            "1 of the token nsw",
            "a call with the group 9",
            "a copy of no known form",
            "a declaration that the test does not know",
            "a declaration that the test does not know",
            "a definition with no group 0, 1 or 8",
            "a fill of no known form",
            "a load with no alignment",
            "an align outside a load, a store, an alloca and a copy",
            "an alloca with a count",
            "an attribute group that the test does not know",
            "an inbounds of no known form",
            "an unreachable that a path from entry reaches",
            "an unreachable that a path from entry reaches",
        ])

    def test_a_clean_module_has_no_problem(self):
        clean = "\n".join(line for line in CONTROL.split("\n") if not any(mark in line for mark in (
            "add nsw", "i32 4", "fptosi double", "i32 %t1, align", "ptr %t9", '"m.reached"',
            "i64 %t0, i1", "i8 1, i64",
            "@abort() #9", "label %end", "end:", "@strlen", "memmove", "#9 =")))
        clean = clean.replace("}\nentry:\n  unreachable\n}", "}").replace("ptr noalias", "ptr")
        clean = clean.replace("switch i32 %x, label %other [", "switch i32 %x, label %loop [")
        counts, shapes, problems = scan(clean)
        self.assertEqual(problems, [])
        self.assertEqual(counts["unreachable"], 7)
        self.assertEqual(counts["no-path"], 5)

    def test_a_metadata_attachment_and_a_data_layout_are_problems(self):
        text = 'target datalayout = "e"\n' + CONTROL.replace(
            "ptr %poison.0, align 4\n", "ptr %poison.0, align 4, !tbaa !1\n", 1)
        kinds = set(problem.split(":")[0] for problem in scan(text)[2])
        self.assertIn("a data layout line", kinds)
        self.assertIn("a metadata attachment", kinds)

    def test_an_alloca_outside_the_entry_block_is_a_problem(self):
        text = CONTROL.replace("bb2:\n", "bb2:\n  %late = alloca i32, align 4\n")
        kinds = set(problem.split(":")[0] for problem in scan(text)[2])
        self.assertIn("an alloca outside the entry block", kinds)


class Corpus(unittest.TestCase):
    def test_the_corpus_holds_its_entries(self):
        self.assertGreaterEqual(len(entries()), RUN_ENTRIES)

    def test_each_module_holds_the_rules_on_each_target_in_each_mode(self):
        lang = str(pathlib.Path(OPTIONS.root) / "test" / "lang")
        tests = entries()
        seen = collections.defaultdict(set)
        for target, triple in TARGETS:
            for mode, options in MODES:
                with tempfile.TemporaryDirectory() as out:
                    std = std_root(target, os.path.join(out, "std"))
                    jobs = [(OPTIONS.fort, triple, std, lang, options + test.flags, test.entry,
                             os.path.join(out, "%d.ll" % number))
                            for number, test in enumerate(tests)]
                    jobs += [(OPTIONS.fort, triple, std, OPTIONS.root, options + ["-I", "src"],
                              entry, os.path.join(out, "product%d.ll" % number))
                             for number, entry in enumerate(PRODUCTS)]
                    total = collections.Counter()
                    failed = []
                    with concurrent.futures.ProcessPoolExecutor(OPTIONS.j) as pool:
                        for label, counts, shapes, problems in pool.map(module_of, jobs,
                                                                        chunksize=4):
                            if problems:
                                failed.append("%s: %s" % (label, problems[:5]))
                            if counts is not None:
                                total.update(counts)
                                total["modules"] += 1
                                for construct, found in shapes.items():
                                    seen[construct] |= found
                    print("ir-flags: %s %s: %s" % (target, mode, " ".join(
                        "%s=%d" % pair for pair in sorted(total.items()))))
                    with self.subTest(target=target, mode=mode):
                        self.assertEqual(failed[:10], [], "%d modules with a problem" % len(failed))
                        self.assertEqual(total["modules"], len(tests) + len(PRODUCTS))
        self.assertEqual(dict(seen), EXPECTED_SHAPES)


def main():
    global OPTIONS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fort", required=True, help="the compiler")
    parser.add_argument("--root", required=True,
                        help="the top of the source tree: its std and its test/lang")
    parser.add_argument("-j", type=int, default=os.cpu_count() or 1, help="compiles in parallel")
    OPTIONS, rest = parser.parse_known_args()
    OPTIONS.fort = os.path.abspath(OPTIONS.fort)
    OPTIONS.root = os.path.abspath(OPTIONS.root)
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
