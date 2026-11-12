#!/usr/bin/env python3
"""Hold the emitted IR free of each flag and attribute that adds an undefined state.

The census of `notes/compiler.md` 9 lists each construct of the emitted IR that has a
precondition in the LLVM language reference. This test holds the part of it that the text of a
module shows. It compiles each run entry of test/lang (the directories `run` and `programs`), the
compiler (src/fort/main.ft) and the language server (src/lsp/main.ft) with `-S`, for the Linux
and the Darwin target, in the checked and in the release mode, and it reads each module. Each
module holds the functions of `std` that the program reaches. The test does not compile the unit
tests of test/fort: each one holds the modules of the compiler again. It holds six things:
- Each token of ZERO stands 0 times on each line of a module, of each kind: a flag that makes a
  result poison (`nsw`, `exact`, a fast-math flag), an attribute that makes a call undefined
  (`noundef`, `nonnull`, `dereferenceable`), `undef`, `poison`, a plain `fptosi`, and each
  instruction that the emitter does not write (`phi`, `select`, `inttoptr`, a `tail` call).
- Each line of a module has a kind that the test knows: the `target triple` line, a type, a
  global, a definition, a declaration, an `attributes` line, a label, an instruction, a case of a
  `switch`. Each kind has a form and a list of the words that it can hold (PERMITTED). A word
  outside the list is a problem. So an attribute, a calling convention, a linkage or a flag that
  the emitter starts to write turns the test red until PERMITTED names it, and the census gets
  its row in the same change. An `attributes` line and the declaration of an intrinsic have one
  exact text each (ATTRIBUTES, INTRINSICS). So `willreturn` and `memory(...)` stand only on
  the declaration of an intrinsic, with the attributes that LLVM gives that intrinsic.
- Each `inbounds` stands in one of the three forms of GEP_SHAPES: the count of the word in a
  module is the count of the lines of the three forms. Each `unreachable` stands after a call of
  a failure entry of `std.rt`, after `llvm.trap`, or in a block that no path from `entry`
  reaches. The test walks the blocks of the function for the third form.
- Each `alloca` has a fixed size and stands in the block `entry`. Each `load` and each `store`
  gives its alignment, and no `load` has the type `i1`. Each call of the copy intrinsic and of
  `llvm.memset` has a fixed length and is not volatile. The copy intrinsic is the one intrinsic
  that copies an aggregate: `llvm.memmove`, which permits two ranges that overlap (D19.3). No
  line holds the text `llvm.memcpy`, but a global can hold it in the bytes of a string. No line
  holds the name of an intrinsic in quotes: LLVM reads `@"llvm.memcpy.p0.p0.i64"` as the
  intrinsic, and the forms of this test name an intrinsic with no quotes. `noalias` is a token
  of ZERO. An `align` stands on no other line of a function. Each definition carries the
  attribute group 0, 1 or 8, and a call carries the group 3 or none.
- No module has a `target datalayout` line, a metadata attachment, or a line of another kind.
- The shapes that the corpus shows are the shapes of EXPECTED_SHAPES. A shape that the corpus
  does not show any more turns the test red, so the census loses its row in the same change.

The test holds the count of the shapes of a token and not the count of its occurrences: an
occurrence count changes with each new entry of the corpus and with each edit of `std`, which each
module holds. It holds no count of the entries as an equal value: RUN_ENTRIES is a floor. The
test prints the occurrence counts of each target and mode. A count with the prefix `header` is
the count of a HEADER_ONLY token on the `attributes` and the `declare` lines. A control module
holds each token in each form that is no token (a name, a string, a comment) and one line for
each rule, so the test cannot pass when it counts nothing or counts a name.

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

# A floor for the count of the run entries, and no equal value: a change that adds an entry
# leaves the test green, and a pattern of CORPUS_TOPS that finds too few entries turns it red.
# The value is the count when the test went in:
#   grep -rlx '//! run' test/lang/run test/lang/programs | wc -l
RUN_ENTRIES = 630

# The products that the test compiles from the top of the tree with the search root `src`.
PRODUCTS = ("src/fort/main.ft", "src/lsp/main.ft")

# The tokens that stand 0 times on each line of a module. The census gives the reason of each.
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
    "inreg", "align_stack", "noalias",
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

# The tokens that stand only on an `attributes` line or a `declare` line of a module, in the texts
# of ATTRIBUTES, INTRINSICS and DECLARE. PERMITTED has none of them for another line kind. The
# test prints the count of each one.
HEADER_ONLY = (
    "nocapture", "readonly", "writeonly", "immarg", "willreturn", "nofree", "nosync",
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
# attributes of each line are the ones that LLVM 18 gives the intrinsic. The shape `copy` is
# `llvm.memmove`, which has no `noalias`: `llvm.memcpy` has no text here.
INTRINSICS = (
    ("overflow", re.compile(
        r"^declare \{ i(8|16|32|64), i1 \} @llvm\.[su](add|sub|mul)\.with\.overflow\.i\1"
        r"\(i\1, i\1\) #4$")),
    ("saturating", re.compile(
        r"^declare i(8|16|32|64) @llvm\.fpto[su]i\.sat\.i\1\.f(32|64)\((float|double)\) #4$")),
    ("copy", re.compile(
        r"^declare void @llvm\.memmove\.p0\.p0\.i64\(ptr nocapture writeonly, "
        r"ptr nocapture readonly, i64, i1 immarg\) #5$")),
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
# A word of a normalized line. A letter after a digit or a point starts no word, so the number
# `0x3FF0000000000000` and the number `1.5e+10` hold none.
WORD = re.compile(r"(?<![0-9A-Za-z_.])[A-Za-z_][A-Za-z_0-9]*")

# The words of a type, on a normalized line: `V` is a name, and `x` stands in `[4 x i32]`.
TYPE_WORDS = frozenset(("i1", "i8", "i16", "i32", "i64", "float", "double", "ptr", "void", "x",
                        "V"))
CONSTANT_WORDS = frozenset(("true", "false", "null"))
OPCODES = frozenset((
    "add", "sub", "mul", "sdiv", "udiv", "srem", "urem", "shl", "lshr", "ashr", "and", "or",
    "xor", "fadd", "fsub", "fmul", "fdiv", "fneg", "icmp", "fcmp", "trunc", "zext", "sext",
    "fptrunc", "fpext", "sitofp", "uitofp", "ptrtoint", "extractvalue", "br", "switch", "ret",
))
PREDICATES = frozenset((
    "eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge",
    "oeq", "une", "olt", "ole", "ogt", "oge",
))

# The words that a line of each kind can hold, on the normalized line. They are the words that
# the emitter writes at that position (src/fort/gen.ft, gen_data.ft, fir_llvm.ft, gen_fir.ft). A
# position that takes an attribute or a calling convention is here: the `declare` line of an
# `extern` and of an interface, the `define` line, the `call` line and the line of a global. The
# kind `instruction` is each instruction with no rule of its own.
PERMITTED = {
    "extern": TYPE_WORDS | {"declare", "zeroext", "signext", "nobuiltin"},
    "interface": TYPE_WORDS | {"declare", "dso_local", "zeroext", "signext", "sret"},
    "define": TYPE_WORDS | {"define", "dso_local", "zeroext", "signext", "sret"},
    "call": TYPE_WORDS | CONSTANT_WORDS | {"call", "zeroext", "signext", "sret"},
    "copy": TYPE_WORDS | {"call", "align", "false"},
    "global": TYPE_WORDS | {"private", "unnamed_addr", "dso_local", "external", "constant",
                            "global", "zeroinitializer", "null", "align", "Q"},
    "type": TYPE_WORDS | {"type"},
    "alloca": TYPE_WORDS | {"alloca", "align"},
    "load": TYPE_WORDS | {"load", "align"},
    "store": TYPE_WORDS | CONSTANT_WORDS | {"store", "align"},
    "getelementptr": TYPE_WORDS | {"getelementptr", "inbounds"},
    "unreachable": frozenset(("unreachable",)),
    "instruction": TYPE_WORDS | CONSTANT_WORDS | OPCODES | PREDICATES | {"to", "label"},
    "case": TYPE_WORDS | {"true", "false", "label"},
}

# The instructions that have a rule and a list of their own. `copy` is a call of a copy intrinsic
# or of `llvm.memset`.
RULED = ("alloca", "load", "store", "getelementptr", "unreachable", "call", "copy")

# The forms of a `getelementptr inbounds`, on a line with each name changed to `V`
# (`gen_gep_field`, `gen_gep_array` and `gen_gep_element`, src/fort/gen.ft).
GEP_SHAPES = (
    ("gep-field", re.compile(r"^  V = getelementptr inbounds V, ptr V, i32 0, i32 \d+$")),
    ("gep-array", re.compile(
        r"^  V = getelementptr inbounds \[\d+ x .+\], ptr V, i64 0, i64 (V|\d+)$")),
    ("gep-element", re.compile(r"^  V = getelementptr inbounds [^,]+, ptr V, i64 (V|\d+)$")),
)

# The forms of a global, on a normalized line: the bytes of a string (`emit_bytes`), the member
# table of an enum, a global or a constant of the program (`define_global`) and one of an
# interface (`gen_external`, src/fort/gen_data.ft).
GLOBALS = (
    ("bytes", re.compile(r"^V = private unnamed_addr constant \[\d+ x i8\] Q, align 1$")),
    ("table", re.compile(r"^V = private unnamed_addr constant \[\d+ x V\] \[.+\], align 8$")),
    ("defined", re.compile(r"^V = dso_local (?:global|constant) .+, align \d+$")),
    ("external", re.compile(r"^V = external dso_local (?:global|constant) [^,]+, align \d+$")),
)
TYPE_LINE = re.compile(r"^V = type \{ [^<>]+ \}$")
TARGET_LINE = "target triple = Q"

# A call of the copy intrinsic and of `llvm.memset` (`gen_memmove` and `gen_memset_zero`): the
# length is a literal, the byte of a fill is 0, and the last argument says that the call is not
# volatile. COPY_NAME gives the name of the intrinsic on a call that copies. MEMCPY_TEXT is the
# text that no line holds outside a global, and QUOTED_INTRINSIC is the start of the name of an
# intrinsic in quotes, which no line holds. The names of a fort module `llvm` start so also, and
# LLVM reads each of them as a name of its own (`llvm.assume`, the global `llvm.used`).
COPY_NAME = re.compile(r"^  call void @llvm\.(memcpy|memmove)\.")
MEMCPY_TEXT = "llvm.memcpy"
QUOTED_INTRINSIC = '@"llvm.'
COPY = re.compile(
    r"^  call void V\(ptr align \d+ V, ptr align \d+ V, i64 \d+, i1 false\)$")
FILL = re.compile(r"^  call void V\(ptr align \d+ V, i8 0, i64 \d+, i1 false\)$")

ALLOCA = re.compile(r"^  V = alloca [^,]+, align \d+$")
LOAD = re.compile(r"^  V = load (?!i1,)[^,]+, ptr V, align \d+$")
STORE = re.compile(r"^  store .+, ptr V, align \d+$")
DEFINE = re.compile(r"^define dso_local .*\) #([018]) \{$")
# A call (`gen_call_sig`): the result type, the call type before a function pointer and a
# variadic callee, the callee, the arguments, and the group of an `extern` callee.
CALL = re.compile(r"^  (?:V = )?call [^()]+ (?:\([^()]*\) )?V\(.*\)(?: #(\d+))?$")
OPCODE = re.compile(r"^  (?:V = )?([a-z_]+)(?: |$)")
CASE = re.compile(r"^    i(?:1|8|16|32|64) (?:-?\d+|true|false), label V$")
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
    "intrinsic": {"overflow", "saturating", "copy", "memset", "trap"},
    "declare": {"extern"},
    "global": {"bytes", "table", "defined"},
    "attributes": {0, 1, 3, 4, 5, 6, 7, 8},
    "define": {0, 1, 8},
    "call": {3},
}

# The control module. It holds each token of ZERO and of HEADER_ONLY in a form that is no token,
# and one line or more for each rule of the scan. The first column of a line says which text
# holds it: ` ` both texts, `-` the text with the problems only, `+` the clean text only.
# test_the_scan_reads_the_control gives the counts and the problems.
CONTROL = """
 target triple = "x86_64-unknown-linux-gnu"
-source_filename = "m.ft"
 %struct.m.exact = type { i32, [4 x i8], ptr, double }
-%struct.m.wide = type { i32, i128 }
-%struct.m.packed = type <{ i8, i64 }>
 @"m.nsw" = dso_local constant i32 0, align 4
 @"m.nuw" = dso_local global %struct.m.exact zeroinitializer, align 8
 @.str.0 = private unnamed_addr constant [22 x i8] c"exact undef ; poison\\22\\00", align 1
 @.e.0 = private unnamed_addr constant [1 x %m.e] [%m.e { i64 0, ptr @.str.0 }], align 8
 @"lib.fast" = external dso_local global i32, align 4
-@"m.tls" = dso_local thread_local global i32 0, align 4
-@"m.weak" = weak global i32 0, align 4
-@"m.addr" = dso_local global ptr getelementptr inbounds (i8, ptr @"m.nsw", i64 1), align 8
 define dso_local i32 @"m.fast"(i32 %nuw.in, ptr %undef.in) #0 {
 entry:
   %poison.0 = alloca i32, align 4
   %tmp0 = alloca [4 x i32], align 4
   store i32 %nuw.in, ptr %poison.0, align 4 ; nsw exact
-  store i32 1, ptr %poison.0
-  store ptr getelementptr inbounds (i8, ptr @"m.nsw", i64 1), ptr %tmp0, align 8
   %t0 = load i32, ptr %poison.0, align 4
-  %t1 = add nsw i32 %t0, 1
+  %t1 = add i32 %t0, 1
   %t2 = getelementptr inbounds [4 x i32], ptr %tmp0, i64 0, i64 3
   %t3 = getelementptr inbounds i32, ptr %undef.in, i64 %t1
   %t4 = getelementptr inbounds %struct.m.exact, ptr %undef.in, i32 0, i32 1
-  %t5 = getelementptr inbounds i8, ptr %undef.in, i32 4
-  %t6 = fptosi double 1.0 to i32
   %t7 = call i32 @llvm.fptosi.sat.i32.f64(double 1.0)
-  %t14 = trunc i32 %t0 to i7
-  %t15 = frem double 1.0, 0x3FF0000000000000
   %t16 = fcmp oeq double 1.500000e+00, 0x3FF0000000000000
   %t17 = call zeroext i8 (ptr, ...) %undef.in(ptr null, i1 true)
   call void @llvm.memmove.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %undef.in, i64 16, i1 false)
   call void @llvm.memset.p0.i64(ptr align 4 %tmp0, i8 0, i64 16, i1 false)
-  call void @llvm.memmove.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %undef.in, i64 %t0, i1 false)
-  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %undef.in, i64 16, i1 false)
-  call void @llvm.memset.p0.i64(ptr align 4 %tmp0, i8 1, i64 16, i1 false)
-  call void @"m.reached"(ptr align 8 %tmp0)
-  call fastcc void @"m.cc"(ptr null)
-  call void @abort() nounwind
-  %t18 = tail call i32 @"m.fast"(i32 1, ptr null)
   call void @abort() #3
   %t8 = icmp eq i32 %t1, 0
   br i1 %t8, label %fail0, label %bb1
 fail0:
   call void @"std.rt.fail_overflow"(ptr @.file.0, i32 1, i32 1)
   unreachable
 bb1:
-  %t9 = alloca i32, i32 %t1, align 4
-  %t10 = load i32, ptr %t9
-  %t13 = load i1, ptr %poison.0, align 1
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
-  switch i32 %x, label %other [
+  switch i32 %x, label %loop [
     i32 1, label %loop
     i32 2, label %three
-    i128 3, label %loop
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
-define dso_local void @"m.reached"() #2 {
-entry:
-  call void @abort() #9
-  br label %end
-end:
-  unreachable
-}
-define dso_local fastcc void @"m.cc"(ptr noalias align 16 %p) #0 {
-entry:
-  ret void
-}
-  ret void
 declare void @abort() nobuiltin
 declare zeroext i8 @getchar(i32 signext, ...) nobuiltin
 declare dso_local void @"lib.f"(ptr sret(%struct.m.exact), i32 zeroext) #0
-declare dso_local void @"lib.g"(ptr nocapture) #0
-declare i32 @strlen(ptr) noalias
-declare void @free(ptr nonnull) nobuiltin
-declare fastcc void @fast(ptr align 8) nobuiltin
 declare i32 @llvm.fptosi.sat.i32.f64(double) #4
-declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1) #5
-declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg) #5
 attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
 attributes #4 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
-attributes #9 = { mustprogress }
"""

# A change of one line of the clean control that puts a word at a position where the emitter
# writes none, as (the text of the clean control, the text in its place). Each one is a problem.
FOREIGN = (
    ("@abort() nobuiltin", "@abort(ptr noundef) nobuiltin"),
    ("@abort() nobuiltin", "@abort(ptr nonnull) nobuiltin"),
    ("@abort() nobuiltin", "@abort(ptr dereferenceable(8)) nobuiltin"),
    ("@abort() nobuiltin", "@abort(ptr byval(i32)) nobuiltin"),
    ("@abort() nobuiltin", "@abort(ptr noalias) nobuiltin"),
    ("@abort() nobuiltin", "@abort(ptr align 8) nobuiltin"),
    ("@abort() nobuiltin", "@abort(ptr sret(i32)) nobuiltin"),
    ("declare void @abort()", "declare fastcc void @abort()"),
    ("declare dso_local void", "declare dso_local noundef i32"),
    ("i32 zeroext) #0", "i32 zeroext) unnamed_addr #0"),
    ("define dso_local i32", "define dso_local fastcc i32"),
    ("define dso_local i32", "define dso_local noundef i32"),
    ("define dso_local i32", "define weak i32"),
    ("ptr %undef.in) #0 {", "ptr align 16 %undef.in) #0 {"),
    ("ptr %undef.in) #0 {", "ptr nofpclass(nan) %undef.in) #0 {"),
    ("ptr %undef.in) #0 {", "ptr %undef.in) #0 section \"x\" {"),
    ("call void @abort() #3", "call fastcc void @abort() #3"),
    ("call void @abort() #3", "call void @abort() #3 [ \"deopt\"() ]"),
    ("(ptr null, i1 true)", "(ptr nonnull null, i1 true)"),
    ("(ptr null, i1 true)", "(ptr null, i1 noundef true)"),
    ("call zeroext i8", "call inreg i8"),
    ("dso_local constant i32 0", "dso_local thread_local constant i32 0"),
    ("dso_local constant i32 0", "weak constant i32 0"),
    ("i32 0, align 4", "i32 0, section \"x\", align 64"),
    ("i32 0, align 4", "i32 ptrtoint (ptr @\"m.nuw\" to i32), align 4"),
    ("zeroinitializer, align 8", "undef, align 8"),
    ("external dso_local global i32", "extern_weak global i32"),
    ("private unnamed_addr constant [22", "linkonce_odr constant [22"),
    ("{ i32, [4 x i8], ptr, double }", "{ i32, [4 x i8], ptr addrspace(1), double }"),
    ("{ i32, [4 x i8], ptr, double }", "opaque"),
    ("%t0 = load i32,", "%t0 = load i1,"),
    ("%t0 = load i32,", "%t0 = load atomic i32,"),
    ("add i32 %t0, 1", "add nuw i32 %t0, 1"),
    ("icmp eq i32", "icmp samesign eq i32"),
    ("fcmp oeq double", "fcmp nnan oeq double"),
    ("alloca i32, align 4", "alloca inalloca i32, align 4"),
    ("alloca i32, align 4", "alloca i32, align 4, addrspace(1)"),
    ("store i32 %nuw.in,", "store volatile i32 %nuw.in,"),
    ("inbounds i32, ptr %undef.in", "inbounds nuw i32, ptr %undef.in"),
    ("  br label %exact", "  br label %exact, !llvm.loop !1"),
    ("    i32 2, label %two", "    i32 2, label %two fastcc"),
    ("triple = ", "triple fastcc = "),
)

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
    words = collections.Counter()
    in_function = False
    entry_block = False

    def hold(kind, line, head):
        """Each word of `line` is a word of PERMITTED[kind]. A token of ZERO has its own count."""
        for word in sorted(set(WORD.findall(line)) - PERMITTED[kind]):
            if word not in ZERO:
                problems.append("the word %s on a line of the kind %s: %s" % (word, kind, head))

    def shape_of(table, construct, line, head, what):
        found = [name for name, form in table if form.match(line)]
        if found:
            shapes[construct].add(found[0])
            counts[construct + " " + found[0]] += 1
            return found[0]
        problems.append(what + ": " + head)
        return None

    for i, line in enumerate(lines):
        if not line:
            continue
        head = raw[i]
        if MEMCPY_TEXT in head and not head.startswith("@"):
            counts["memcpy line"] += 1
            problems.append("a line with llvm.memcpy: " + head)
        if QUOTED_INTRINSIC in head:
            problems.append("the name of an intrinsic in quotes: " + head)
        if in_function and BLOCK_LABEL.match(head):
            entry_block = BLOCK_LABEL.match(head).group(1) == "entry"
            continue
        found = WORD.findall(line)
        words.update(found)
        if "!" in line:
            problems.append("a metadata attachment: " + head)
        if head.startswith(("attributes #", "declare ")):
            for word in found:
                if word in HEADER_ONLY:
                    counts["header " + word] += 1
        if head.startswith("attributes #"):
            m = ATTRIBUTES_LINE.match(head)
            if m and ATTRIBUTES.get(int(m.group(1))) == m.group(2):
                shapes["attributes"].add(int(m.group(1)))
            else:
                problems.append("an attribute group that the test does not know: " + head)
        elif head.startswith("declare "):
            what = "a declaration that the test does not know"
            if "@llvm." in head:
                shape_of(INTRINSICS, "intrinsic", head, head, what)
            else:
                kind = shape_of(DECLARE, "declare", head, head, what)
                if kind:
                    hold(kind, line, head)
        elif head.startswith("target "):
            if head.startswith("target datalayout"):
                problems.append("a data layout line: " + head)
            elif line != TARGET_LINE:
                problems.append("a line of no known kind: " + head)
        elif head.startswith("define "):
            in_function = True
            entry_block = False
            m = DEFINE.match(head)
            if m:
                shapes["define"].add(int(m.group(1)))
            else:
                problems.append("a definition with no group 0, 1 or 8: " + head)
            hold("define", line, head)
        elif head == "}" and in_function:
            in_function = False
        elif head.startswith("@"):
            counts["global"] += 1
            shape_of(GLOBALS, "global", line, head, "a global of no known form")
            hold("global", line, head)
        elif head.startswith("%"):
            if not TYPE_LINE.match(line):
                problems.append("a type of no known form: " + head)
            hold("type", line, head)
        elif not in_function:
            problems.append("a line of no known kind: " + head)
        elif head.startswith("    "):
            if not CASE.match(line):
                problems.append("a line of no known kind: " + head)
            hold("case", line, head)
        elif head == "  ]":
            pass
        elif not OPCODE.match(line):
            problems.append("a line of no known kind: " + head)
        else:
            opcode = OPCODE.match(line).group(1)
            if opcode == "alloca":
                counts["alloca"] += 1
                if not ALLOCA.match(line):
                    problems.append("an alloca with a count: " + head)
                elif not entry_block:
                    problems.append("an alloca outside the entry block: " + head)
            elif opcode == "load":
                counts["load"] += 1
                if not LOAD.match(line):
                    problems.append("a load of no known form: " + head)
            elif opcode == "store":
                counts["store"] += 1
                if not STORE.match(line):
                    problems.append("a store of no known form: " + head)
            elif opcode == "getelementptr":
                shape_of(GEP_SHAPES, "inbounds", line, head, "an inbounds of no known form")
            elif opcode == "unreachable":
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
                    problems.append(
                        "an unreachable that a path from entry reaches, line %d" % (i + 1))
                if shape:
                    shapes["unreachable"].add(shape)
                    counts["unreachable " + shape] += 1
            elif COPY_NAME.match(head):
                opcode = "copy"
                counts[COPY_NAME.match(head).group(1) + " call"] += 1
                if not COPY.match(line):
                    problems.append("a copy of no known form: " + head)
            elif head.startswith("  call void @llvm.memset."):
                opcode = "copy"
                counts["memset call"] += 1
                if not FILL.match(line):
                    problems.append("a fill of no known form: " + head)
            elif opcode == "call":
                m = CALL.match(line)
                if not m:
                    problems.append("a call of no known form: " + head)
                elif m.group(1):
                    group = int(m.group(1))
                    shapes["call"].add(group)
                    if group != 3:
                        problems.append("a call with the group %d: %s" % (group, head))
            hold(opcode if opcode in RULED else "instruction", line, head)
    for token in ZERO:
        if words[token]:
            counts[token] = words[token]
            problems.append("%d of the token %s" % (words[token], token))
    forms = sum(counts["inbounds " + name] for name, _ in GEP_SHAPES)
    if words["inbounds"] != forms:
        problems.append("%d inbounds outside the three forms" % (words["inbounds"] - forms))
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


def control(clean):
    """The text of CONTROL with the problems, or the clean text."""
    keep = " +" if clean else " -"
    return "\n".join(line[1:] for line in CONTROL.split("\n") if line and line[0] in keep) + "\n"


def kinds_of(problems):
    """The kind of each problem: its text with no line of the module and no line number."""
    return sorted(problem.split(":")[0].split(", line")[0] for problem in problems)


class Scan(unittest.TestCase):
    def test_the_scan_reads_the_control(self):
        counts, shapes, problems = scan(control(False))
        want = {
            "nsw": 1, "fptosi": 1, "nonnull": 1, "mustprogress": 1, "tail": 1, "noalias": 2,
            "inbounds": 6, "inbounds gep-array": 1, "inbounds gep-element": 1,
            "inbounds gep-field": 1,
            "unreachable": 8, "unreachable after-fail": 1, "unreachable after-trap": 1,
            "unreachable no-path": 4,
            "alloca": 3, "load": 3, "store": 3, "memmove call": 2, "memcpy call": 1,
            "memcpy line": 2,
            "memset call": 2,
            "global": 8, "global bytes": 1, "global table": 1, "global defined": 3,
            "global external": 1,
            "declare extern": 4, "declare interface": 2, "intrinsic saturating": 1,
            "header noalias": 0, "header nobuiltin": 4, "header willreturn": 1, "header memory": 1,
            "header nounwind": 2, "header nocapture": 1,
            "exact": 0, "undef": 0, "poison": 0, "nuw": 0, "fast": 0,
        }
        self.assertEqual({key: counts[key] for key in want}, want)
        self.assertEqual(shapes["define"], {0, 1})
        self.assertEqual(shapes["call"], {3, 9})
        self.assertEqual(shapes["attributes"], {0, 4})
        self.assertEqual(shapes["intrinsic"], {"saturating"})
        self.assertEqual(shapes["declare"], {"extern", "interface"})
        self.assertEqual(shapes["global"], {"bytes", "table", "defined", "external"})
        self.assertEqual(kinds_of(problems), [
            "1 of the token fptosi",
            "1 of the token mustprogress",
            "1 of the token nonnull",
            "1 of the token nsw",
            "1 of the token tail",
            "2 of the token noalias",
            "3 inbounds outside the three forms",
            "a call of no known form",
            "a call with the group 9",
            "a copy of no known form",
            "a declaration that the test does not know",
            "a declaration that the test does not know",
            "a declaration that the test does not know",
            "a definition with no group 0, 1 or 8",
            "a fill of no known form",
            "a global of no known form",
            "a global of no known form",
            "a line of no known kind",
            "a line of no known kind",
            "a line of no known kind",
            "a line with llvm.memcpy",
            "a line with llvm.memcpy",
            "a load of no known form",
            "a load of no known form",
            "a store of no known form",
            "a type of no known form",
            "an alloca with a count",
            "an attribute group that the test does not know",
            "an inbounds of no known form",
            "an unreachable that a path from entry reaches",
            "an unreachable that a path from entry reaches",
            "the word align on a line of the kind call",
            "the word align on a line of the kind define",
            "the word align on a line of the kind extern",
            "the word call on a line of the kind instruction",
            "the word fastcc on a line of the kind call",
            "the word fastcc on a line of the kind define",
            "the word fastcc on a line of the kind extern",
            "the word frem on a line of the kind instruction",
            "the word getelementptr on a line of the kind global",
            "the word getelementptr on a line of the kind store",
            "the word i128 on a line of the kind case",
            "the word i128 on a line of the kind type",
            "the word i7 on a line of the kind instruction",
            "the word inbounds on a line of the kind global",
            "the word inbounds on a line of the kind store",
            "the word nocapture on a line of the kind interface",
            "the word nounwind on a line of the kind call",
            "the word thread_local on a line of the kind global",
            "the word weak on a line of the kind global",
        ])

    def test_a_clean_module_has_no_problem(self):
        counts, shapes, problems = scan(control(True))
        self.assertEqual(problems, [])
        self.assertEqual(counts["unreachable"], 7)
        self.assertEqual(counts["unreachable no-path"], 5)
        self.assertEqual(counts["inbounds"], 3)
        self.assertEqual(shapes["call"], {3})

    def test_the_copy_intrinsic_is_memmove(self):
        tail = "(ptr nocapture writeonly, ptr nocapture readonly, i64, i1 immarg) #5\n"
        memmove = "declare void @llvm.memmove.p0.p0.i64" + tail
        clean = control(True)
        self.assertEqual(clean.count("@llvm.memmove."), 1)
        counts, shapes, problems = scan(clean + memmove)
        self.assertEqual(problems, [])
        self.assertEqual(shapes["intrinsic"], {"saturating", "copy"})
        self.assertEqual((counts["memcpy call"], counts["memmove call"]), (0, 1))
        self.assertEqual(counts["header nocapture"], 2)
        # The name `llvm.memcpy` with the text of `llvm.memmove`, and with its own text of LLVM.
        unknown = "a declaration that the test does not know"
        memcpy = "a line with llvm.memcpy"
        quoted = "the name of an intrinsic in quotes"
        self.assertEqual(kinds_of(scan(clean + "declare void @llvm.memcpy.p0.p0.i64" + tail)[2]),
                         [unknown, memcpy])
        self.assertEqual(
            kinds_of(scan(clean + "declare void @llvm.memcpy.p0.p0.i64(ptr noalias nocapture "
                          "writeonly, ptr noalias nocapture readonly, i64, i1 immarg) #5\n")[2]),
            ["2 of the token noalias", unknown, memcpy])
        self.assertEqual(
            kinds_of(scan(clean + memmove.replace("ptr noc", "ptr noalias noc"))[2]),
            ["2 of the token noalias", unknown])
        # A call of `llvm.memcpy` in the form of a copy, and a copy whose length is no literal.
        counts, _, problems = scan(clean.replace("@llvm.memmove.", "@llvm.memcpy.") + memmove)
        self.assertEqual(kinds_of(problems), [memcpy])
        self.assertEqual((counts["memcpy call"], counts["memmove call"]), (1, 0))
        self.assertEqual(counts["memcpy line"], 1)
        self.assertEqual(
            kinds_of(scan(clean.replace("i64 16, i1 false", "i64 %t0, i1 false", 1))[2]),
            ["a copy of no known form"])

    def test_no_line_outside_a_global_holds_the_text_of_memcpy(self):
        clean = control(True)
        memcpy = "a line with llvm.memcpy"
        quoted = "the name of an intrinsic in quotes"
        call = "  call void @llvm.memmove.p0.p0.i64(ptr align 4 %tmp0, ptr align 4 %undef.in,"
        self.assertEqual(clean.count(call), 1)
        # The name in quotes, which LLVM reads as the intrinsic: a call with no `align`, which
        # has the form of a call of a function of the program, and a declaration.
        counts, _, problems = scan(clean.replace(
            call, '  call void @"llvm.memcpy.p0.p0.i64"(ptr %tmp0, ptr %undef.in,'))
        self.assertEqual(kinds_of(problems), [memcpy, quoted])
        self.assertEqual((counts["memcpy line"], counts["memcpy call"]), (1, 0))
        counts, _, problems = scan(
            clean + 'declare void @"llvm.memcpy.p0.p0.i64"(ptr, ptr, i64, i1 immarg)\n')
        self.assertEqual(kinds_of(problems),
                         ["a declaration that the test does not know", memcpy, quoted])
        self.assertEqual(counts["memcpy line"], 1)
        # Each other form of a line with the text: another length type, the `inline` form, the
        # address of the intrinsic as an argument.
        for line in ("  call void @llvm.memcpy.p0.p0.i32(ptr %tmp0, ptr %undef.in, i32 8, "
                     "i1 false)",
                     "  call void @llvm.memcpy.inline.p0.p0.i64(ptr %tmp0, ptr %undef.in, i64 8, "
                     "i1 false)",
                     "  call void @\"m.reached\"(ptr @llvm.memcpy.p0.p0.i64)"):
            with self.subTest(line=line):
                text = clean.replace(call, line + "\n" + call)
                self.assertEqual(text.count(line), 1)
                counts, _, problems = scan(text)
                self.assertIn(memcpy, kinds_of(problems))
                self.assertEqual(counts["memcpy line"], 1)
        # The name of another intrinsic in quotes is a problem, and no line with `llvm.memcpy`.
        counts, _, problems = scan(clean.replace(
            call, '  call void @"llvm.memmove.p0.p0.i64"(ptr %tmp0, ptr %undef.in,'))
        self.assertEqual(kinds_of(problems), [quoted])
        self.assertEqual(counts["memcpy line"], 0)
        # The bytes of a string constant are no call: a global line does not count.
        counts, _, problems = scan(
            '@.str.9 = private unnamed_addr constant [26 x i8] c"call void @llvm.memcpy.p0(", '
            "align 1\n" + clean)
        self.assertEqual(problems, [])
        self.assertEqual(counts["memcpy line"], 0)

    def test_no_name_in_quotes_has_the_prefix_llvm(self):
        # A fort module with the name `llvm` gives each of its names the prefix (the census row
        # of the name of a fort function): a global, a definition, a declaration.
        clean = control(True)
        quoted = "the name of an intrinsic in quotes"
        for old, new in (('@"m.nuw" = dso_local global', '@"llvm.used" = dso_local global'),
                         ('@"lib.fast" = external', '@"llvm.used" = external'),
                         ('define dso_local void @"m.folded"(',
                          'define dso_local void @"llvm.assume"('),
                         ('declare dso_local void @"lib.f"(',
                          'declare dso_local void @"llvm.hello"(')):
            with self.subTest(new=new):
                self.assertEqual(clean.count(old), 1)
                self.assertEqual(kinds_of(scan(clean.replace(old, new))[2]), [quoted])
        # The same text with no `@"` before it is the name of a fort module, and no problem.
        old = '@"lib.fast" = external'
        self.assertEqual(scan(clean.replace(old, '@"lib.llvm.used" = external'))[2], [])

    def test_a_word_at_a_position_that_takes_none_is_a_problem(self):
        clean = control(True)
        for old, new in FOREIGN:
            with self.subTest(new=new):
                self.assertIn(old, clean)
                self.assertNotEqual(scan(clean.replace(old, new, 1))[2], [])

    def test_a_word_after_each_line_is_a_problem(self):
        lines = control(True).split("\n")
        held = 0
        for at, line in enumerate(lines):
            if not line or line in ("}", "  ]") or BLOCK_LABEL.match(line):
                continue
            held += 1
            changed = lines[:at] + [line.split(" ;")[0] + " noreturn"] + lines[at + 1:]
            with self.subTest(line=line):
                self.assertNotEqual(scan("\n".join(changed))[2], [])
        self.assertEqual(held, 51)

    def test_a_metadata_attachment_and_a_data_layout_are_problems(self):
        text = 'target datalayout = "e"\n' + control(True).replace(
            "ptr %poison.0, align 4\n", "ptr %poison.0, align 4, !tbaa !1\n", 1)
        kinds = set(kinds_of(scan(text)[2]))
        self.assertIn("a data layout line", kinds)
        self.assertIn("a metadata attachment", kinds)

    def test_an_alloca_outside_the_entry_block_is_a_problem(self):
        text = control(True).replace("bb2:\n", "bb2:\n  %late = alloca i32, align 4\n")
        self.assertEqual(kinds_of(scan(text)[2]), ["an alloca outside the entry block"])

    def test_a_line_outside_a_function_is_a_problem(self):
        for line in ("  ret void", "!0 = !{}", "module asm Q", "$c = comdat any", "}"):
            with self.subTest(line=line):
                kinds = kinds_of(scan(control(True) + line + "\n")[2])
                self.assertIn("a line of no known kind", kinds)


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
                        self.assertEqual(total["memcpy line"], 0)
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
