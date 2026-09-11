# test/ir

Hand-written LLVM IR modules (LLVM 18, opaque pointers) in the form the compiler emits, one per
program. `test/pipeline_test.sh` verifies each module with `opt -passes=verify`, compiles and
links it with `<build-dir>/std/fort_rt.o` and runs it under qemu (toolchain.md 2). The code
generation contract (toolchain.md 6, D19.1) quotes both files as its worked examples, byte for
byte, so a change here is a change there, and every rule about the module's form (the section
order, the always-emitted named types, the fixed order of the declarations, the attribute-group
indices) is stated there, not here.

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
`fort_rt_fail_bounds` (index, length, file, line, column) and is followed by `unreachable`. The
program prints `before`, then the D11.4 line
`abort.ft:12:13: runtime error: index 5 out of range for length 3`, and dies with SIGABRT.

`colons.ll` is

```fort
fn i32 main() { println("colon"); return 0; }
```

in `my:app.ft`, whose module path is therefore `my:app` and whose `main` is the symbol
`my:app.main` (D9.1, D9.7). It is `hello.ll` with that one name changed, and it exists because
nothing before the link can check that the byte survives: LLVM quotes a name its unquoted
identifier syntax does not admit, the assembler quotes the label in turn, and the ELF symbol is
the name itself. The pipeline test reads it back out of the symbol table with `nm`.

`floats.ll` is

```fort
fn i32 main() {
    println(0.1);
    println(1e17);
    println(-0.0);
    println(f64.from_bits(0x7FF0000000000000));   // inf
    println(f64.from_bits(0x7FF8000000000000));   // nan
    println(cast(0.1, f32));
    println(cast(16777217.0, f32));
    return 0;
}
```

in `floats.ft`: an `f64` argument is a `double` and an `f32` argument a `float`, each in its own
type, because the digits printed depend on it (D18.1). Float constants are LLVM hex literals,
exact by construction; a `float` one carries the value widened to `double`, so `0.1f` is
`0x3FB99999A0000000` and the unrepresentable `16777217.0` is the `f32` it rounds to,
`0x4170000000000000`. The program prints `0.1`, `1e+17`, `-0.0`, `inf`, `nan`, `0.1` and
`16777216.0`, one per line, and exits 0 (D11.7, D18.3).
