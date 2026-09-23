# test/ir

Hand-written LLVM IR modules (LLVM 18, opaque pointers) in the form the compiler emits, one per
program. `test/pipeline_test.sh` verifies each module with `opt -passes=verify`, compiles and
links it with the target clang and runs it under qemu (toolchain.md 2). The link takes the module
alone: one module holds the whole program, the runtime included (D9.10, D13.1), so each file here
defines the handful of `std.rt` entry points it calls, over `write` and `abort` from the C
library. The code generation contract (toolchain.md 6, D19.1) quotes `hello.ll` and `abort.ll` as
its worked examples, byte for byte, so a change here is a change there, and every rule about the
module's form (the section order, the always-emitted named types, the fixed order of the
declarations, the attribute-group indices) is stated there, not here.

What these files are not: the module the compiler emits for the program beside each one. That
module also holds every definition of `std.rt` and of `std.libc`, some three thousand lines of
it, because every closure holds the runtime (D9.10). The five definitions here stand for it. The
emitter's own output is held against its expected text by `bootstrap/test/gen_module_test.c`, where
the runtime is out of the closure and the calls into it stand alone.

`hello.ll` is

```fort
fn main() i32 { println("hello, world!"); return 0; }
```

in `main.ft`: two calls into the runtime, `fort_entry`, which forwards the result of `main.main`,
and the `main(argc, argv)` of D11.6, which calls `std.rt.args_init`, `std.rt.args`, `fort_entry`
and `std.rt.flush_all` and returns the status masked to one byte.

`abort.ll` is

```fort
fn main() i32 {
    println("before");
    i32[3] a = {};
    i64 mut i = 5;
    return a[i];
}
```

in `abort.ft`, whose module path is therefore `abort` and whose `main` is the symbol
`abort.main` (D9.1, D9.7), with the `[` of `a[i]` at line 12, column 13. The locals are
allocas in the entry
block, the array is zeroed with `llvm.memset`, and the bounds check of D6.8 is an `icmp uge`
against the length (one unsigned compare catches a negative index too) branching to a failure
block at the end of the function, which calls
`std.rt.fail_bounds` (index, length, file, line, column) and is followed by `unreachable`. The
program prints `before`, then the D11.4 line
`abort.ft:12:13: runtime error: index 5 out of range for length 3`, and dies with SIGABRT.

It is also where the attribute group `#8` of toolchain.md 6 item 14 is carried end to end: the
definition of `std.rt.fail_bounds` is `cold noreturn nounwind` plus what every fort definition
carries, and its call to the C library's `abort` is followed by the `llvm.trap` of item 20, which
nothing before the link checks.

`colons.ll` is

```fort
fn main() i32 { println("colon"); return 0; }
```

in `my:app.ft`, whose module path is therefore `my:app` and whose `main` is the symbol
`my:app.main` (D9.1, D9.7). It is `hello.ll` with that one name changed, and it exists because
nothing before the link can check that the byte survives: LLVM quotes a name its unquoted
identifier syntax does not admit, the assembler quotes the label in turn, and the ELF symbol is
the name itself. The pipeline test reads it back out of the symbol table with `nm`.

There is no float module here. `std.rt` holds the two float printers of D18.1, folded in by
T-132, and a hand-written module that exercised them returns with that work.
