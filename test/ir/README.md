# test/ir

Hand-written LLVM IR modules (LLVM 18, opaque pointers) in the form the compiler emits, one per
program. `test/pipeline_test.sh` verifies each module with `opt -passes=verify`, compiles and
links it with `<build-dir>/std/fort_rt.o` and runs it under qemu (toolchain.md 2). D19.1 names
them the reference for the form of a module until the code generation contract (toolchain.md 6)
is rewritten against them: no comments, no `source_filename`, no datalayout and no module flags,
the `target triple` first, then the named types (`%fort.slice` and `%fort.enum_member`, always
emitted), the globals, the function definitions, the private data, the declarations and the
attribute groups. Only referenced declarations appear, in a fixed order: `extern` C functions in
first-use order, then the runtime entry points in the order of toolchain.md 5.1, then the
intrinsics. Attribute group `#0` is a fort definition, `#2` a failure entry point and `#6` an
intrinsic.

`hello.ll` is

```fort
fn i32 main() { println("hello, world!"); return 0; }
```

in `main.ft`: two runtime calls and `fort_entry`, which forwards the result of `main.main`.

`abort.ll` is

```fort
fn i32 main() {
    println("before");
    i32[3] a = {};
    mut i64 i = 5;
    return a[i];
}
```

in `abort.ft`, with the `[` of `a[i]` at line 12, column 14. The locals are allocas in the entry
block, the array is zeroed with `llvm.memset`, and the bounds check of D6.8 is an `icmp uge`
against the length (one unsigned compare catches a negative index too) branching to a failure
block at the end of the function, which calls
`fort_rt_fail_bounds` (index, length, file, line, column) and is followed by `unreachable`. The
program prints `before`, then the D11.4 line
`abort.ft:12:14: runtime error: index 5 out of range for length 3`, and dies with SIGABRT.
