# fort toolchain

This document specifies the `fort` compiler's command line, build pipeline, build modes,
diagnostics, C runtime, code generation contract and test conventions for v1. It implements D14
together with D9.10, D10, D11, D12, D18 and the run-time side of D17. Where it disagrees with
`decisions.md` or `grammar.md`, those files win (D1.2).

Sections: 1 Command line; 2 Build pipeline; 3 Build modes; 4 Diagnostics; 5 The C runtime;
6 Code generation contract; 7 Testing; 8 Compiler architecture sketch; 9 Not in v1.

## 1. Command line

```sh
fort [options] entry.ft
```

Exactly one entry file is given; the first argument that does not start with `-` is the entry
file (D14.1). Options and the entry file may appear in any order.

| Option              | Meaning                                                    | Default    |
|---------------------|------------------------------------------------------------|------------|
| `-o <file>`         | output path                                                | see below  |
| `-S`                | stop after emitting the LLVM IR module (D19.1)             | off        |
| `-c`                | stop after compiling it to an object file                  | off        |
| `-I <dir>`          | add a search root after the entry directory; repeatable    | none       |
| `--std-dir <dir>`   | standard library directory                                 | see below  |
| `--release`         | release mode (section 3, D11.1)                            | checked    |
| `--no-bounds-check` | remove index and slice checks (D10.6); unsafe              | checks on  |
| `-l<lib>`           | passed to the linker as given; repeatable, in order        | none       |
| `--cc <path>`       | the clang that compiles and links the IR (D14.3)           | `clang`    |
| `--target <triple>` | passed to `--cc` as `--target=<triple>` (D14.1)            | see below  |
| `-Xcc <arg>`        | passed to `--cc` verbatim, after the arguments below       | none       |
| `--help`            | print the usage line and exit 0                            |            |
| `--version`         | print the compiler version and exit 0                      |            |

- `-o`, `-I`, `--std-dir`, `--cc`, `--target` and `-Xcc` take the following argument; `-l<lib>`
  is one argument. `-I` roots are searched in command-line order (D9.2) and `-Xcc` arguments are
  passed in command-line order. The last `-o`, `--cc` and `--target` win.
- `--cc` must name a clang: the compiler emits LLVM IR, not assembly or C (D14.1, D19.1). The
  default target triple is `x86_64-linux-gnu` (D14.1).
- The default output is `a.out`; with `-c` it is `<entry>.o` and with `-S` `<entry>.ll` (D14.1),
  where `<entry>` is the entry file's base name without `.ft`, placed in the current directory as
  `cc` does.
- The default standard library directory is `$FORT_STD_DIR` when set, else `std` relative to
  the directory containing the `fort` binary.
- `-S` and `-c` together stop at the IR. With `-S`, `-l`, `--cc`, `--target` and `-Xcc` are
  unused.
- `--release` and `--no-bounds-check` are independent and may be combined.
- The entry file's directory is always a root and the current directory never is (D9.2).

Exit status (D14.1):

| Status | Meaning                                                                          |
|--------|----------------------------------------------------------------------------------|
| 0      | success                                                                          |
| 1      | at least one compile error was reported (section 4)                              |
| 2      | usage error, unreadable entry file, internal error, or failure of `--cc`         |

Usage errors, internal errors and failures of `--cc` are reported as `fort: error: <message>`
on stderr, for example `fort: error: cannot read 'x.ft': No such file or directory` or
`fort: error: cc failed with status 1`. `fort` with no arguments prints one usage line and exits
with 2; `--help` prints the same line and exits 0.

```sh
fort main.ft -o main                          # build ./main in checked mode
fort -S main.ft                               # write main.ll and stop
fort -c main.ft                               # write main.o and stop
clang --target=x86_64-linux-gnu -o main main.o "$FORT_STD_DIR/fort_rt.o"   # link -c by hand
fort --release -o main main.ft                # release mode
fort --release --no-bounds-check -o bench main.ft
fort -I lib -I vendor -lm main.ft             # extra roots, link libm
fort --cc clang-18 --target x86_64-linux-gnu -Xcc -fuse-ld=lld main.ft
FORT_STD_DIR=/opt/fort/std fort main.ft
```

The only environment variables read are `FORT_STD_DIR` (D14.1) and `TMPDIR`, which locates the
temporary directory for the intermediate IR file (D19.1).

## 2. Build pipeline

Compilation is whole-program (D9.10):

1. Read the entry file and derive its module path and root (exit 2 if unreadable, 1 if the base
   name is not a valid module name).
2. Parse it; resolve each import (module-system.md 2 and 3); parse each newly reached module
   until the closure is complete; reject cycles and duplicate identities (exit 1).
3. Check every module in dependency order, imported modules first (exit 1).
4. Emit one LLVM IR module for the closure to `<tmp>/<entry>.ll` (D19.1), or to the `-S` output
   and stop.
5. Run `<cc>` over that module once: it compiles and links in one invocation (D14.3), exit 2 on
   failure. The line is

   ```sh
   clang --target=x86_64-linux-gnu -O1 -fPIE -pie -Wno-override-module \
       -o <out> <tmp>/<entry>.ll <std-dir>/fort_rt.o <-l options> <-Xcc args>
   ```

   with `-O2` in place of `-O1` under `--release` (D14.3), and `-c` before `-o`, no `-pie`, no
   runtime object and no `-l` for `-c`. That clang finds the cross sysroot, its `Scrt1.o`,
   `crti.o` and `crtn.o` and `x86_64-linux-gnu-ld` by itself, so no `--sysroot`,
   `--gcc-toolchain` or `-fuse-ld` is needed; `-Wno-override-module` silences the warning about
   the module's own target triple, and `-x ir` must not be passed because `-x` is sticky and
   would also treat `fort_rt.o` as IR.
6. Remove the temporary directory.

- The temporary directory comes from `mkdtemp` under `$TMPDIR` (default `/tmp`) and is removed
  whether or not `--cc` succeeded.
- `--cc` is invoked with exactly the arguments shown, in that order; `-fPIE -pie` and the
  position-independent code the IR compiles to make the output a position-independent executable
  (D14.3, D16).
- Every emitted module passes `opt -passes=verify` (D19.1). `test/ir/*.ll` are hand-written
  modules in the form the compiler emits and `test/pipeline_test.sh` runs this pipeline over
  them; the language-test harness verifies the module of every test that compiles
  (`run_tests.py --verify-ir`, section 7.3).
- An object from `-c` contains the whole program except the runtime; linking it needs
  `<std-dir>/fort_rt.o` and nothing else (D9.10).

Where things live: `<std-dir>/*.ft` holds the standard library modules of D13.2 (`std::sys`,
`std::libc`, `std::mem`, `std::io`, `std::str`, `std::strbuf`, `std::vec`, `std::strmap`,
`std::math`) as source, compiled with every program that imports them; `<std-dir>/fort_rt.o` is
the C runtime object, built from `runtime/fort_rt.c` by the compiler's own build; `<bindir>/fort` is
the compiler, and `<bindir>/std` its fallback `--std-dir`. Only modules in the import closure
are read (module-system.md 10).

## 3. Build modes

Two modes (D11.1); `--no-bounds-check` is an orthogonal switch (D10.6). The compiler itself
optimizes nothing in v1 and emits the same shape of IR in both modes; the only difference besides
the checks below is that `--cc` compiles the module with `-O2` instead of `-O1` (D14.3).

| Check                                              | checked | `--release` | `--no-bounds-check` |
|----------------------------------------------------|---------|-------------|---------------------|
| signed overflow: `+ - *`, unary `-`, `++ --`, `+= -= *=` | trap | wrap    | unchanged           |
| unsigned overflow, same operators                  | trap    | wrap        | unchanged           |
| `+% -% *%`, `+%= -%= *%=` (D11.2)                  | wrap    | wrap        | unchanged           |
| shift count negative or `>=` width (D11.1)         | trap    | masked      | unchanged           |
| `/ %` by zero, `MIN / -1`, `MIN % -1` (D6.13)      | trap    | trap        | unchanged           |
| index out of range (D6.8)                          | trap    | trap        | removed             |
| slice bounds `0 <= lo <= hi <= len` (D6.9)         | trap    | trap        | removed             |
| `new`: negative count, size overflow, no memory    | trap    | trap        | unchanged           |
| store over a live `own` value (D17.11)             | trap    | no check    | unchanged           |
| `assert(cond)` (D12.2)                             | trap    | trap        | unchanged           |
| `panic(msg)`                                       | trap    | trap        | unchanged           |
| constant index or fold out of range (D4.4, D6.8)   | compile error in every mode                 |
| `cast` (D3.14)                                     | never traps in any mode                     |

"Trap" is the runtime error contract of D11.4: flush, one line on stderr, `abort()`. "Wrap" is
two's complement; "masked" means `count & (width - 1)`; "no check" means the store happens and
the allocation the old value designated leaks (D17.11). Programs must not rely on wrapping or
trapping for correctness (D11.1); the wrapping operators exist for code that needs wrap-around in
both modes. `p[lo..hi]` on a raw pointer is never checked (D6.9), and the undefined behaviors of
D10.7 are undefined in every mode.

## 4. Diagnostics

Compile-time diagnostics (D14.2) are written to stderr, one per line:

```sh
<file>:<line>:<col>: error: <message>
<file>:<line>:<col>: note: <message>
```

- `<file>` is the path the compiler opened: the entry file as given on the command line, an
  imported module as `<root>/<relative path>` with the root as given (`-I lib` gives
  `lib/util.ft`; the entry directory is the entry path's directory part, or nothing).
- `<line>` is 1-based; `<col>` is the 1-based column of the offending token's first byte, a tab
  counting as one column. A `note:` follows the `error:` it belongs to, with its own position.
- An error without a position in the file (a missing `main`, an entry base name that is not a
  valid module name) uses `1:1` (D14.2).
- Parsing stops at a module's first syntax error; checking reports every error in a module.
  Modules are processed in dependency order and processing stops after the first module with
  errors, so one module's errors appear together (D14.2).
- The compiler emits no warnings (D14.2): unused imports, unused variables and statements after
  a terminating statement are not diagnosed.

```sh
main.ft:7:5: error: cannot assign to immutable 'x'
main.ft:3:9: note: 'x' declared here
util.ft:12:23: error: expected ';'
```

Runtime diagnostics (D11.4) use the same position syntax, followed by `abort()`, so the shell
reports status 134:

```sh
main.ft:12:14: runtime error: index 5 out of range for length 3
main.ft:16:9: runtime error: overwriting owned value
main.ft:20:5: panic: queue empty
main.ft:31:5: assertion failed: n > 0
```

The position of a runtime error is the operator token of the failing operation (`[`, `+`, `-`,
`*`, `/`, `%`, `<<`, `>>`, `++`, `--`, a compound-assignment operator, unary `-`, the `=` of an
assignment for the overwrite check of D17.11) or the builtin name for `new`, `assert` and
`panic`. The assertion text is the verbatim source text of the argument. Runtime messages are
listed in section 5.

## 5. The C runtime

The runtime is `runtime/fort_rt.c`, compiled to `<std-dir>/fort_rt.o`; it is C and permanent
(D13.1). It owns process start and exit (D11.6), heap allocation (D10.2, D10.3), the
runtime-error and panic paths (D11.4, including the ownership overwrite check of D17.11),
formatting and buffering for the print family (D11.5, D11.7, D12.2, D18), and the program arguments
for `std::sys`. The compiler emits calls to the entry points below; the standard library
declares the ones it needs with `extern fn`
(module-system.md 7).

### 5.1 Entry points

`loc` abbreviates `const char* file, uint32_t line, uint32_t col`. Every `fort_rt_fail_*`
function, `fort_rt_panic`, `fort_rt_assert_fail` and `fort_rt_exit` is `_Noreturn`.

```c
// Types shared with generated code.
struct fort_string { const char* ptr; uint64_t len; };    // fort string, D3.7
struct fort_slice  { void* ptr; uint64_t len; };          // fort T[], D3.5
struct fort_rt_enum_member { int32_t value; const char* name; };

// Allocation (D10.2, D10.3). fort_rt_new returns zeroed storage for count elements
// of elem_size bytes, at least one byte so the result is never null (new(T[0]) is
// non-null); an overflowing product or a failed calloc is a runtime error at loc.
// fort_rt_del is free(p); a null p is a no-op. Ownership (D17) is erased: the
// runtime sees plain pointers, and the compiler zeroes a del or move operand
// itself (section 6, item 14).
void* fort_rt_new(uint64_t elem_size, uint64_t count, loc);
void  fort_rt_del(void* p);

// Failures (D11.4): flush every buffer, write one line to stderr, abort().
// Values arrive sign-extended to 64 bits; hi is len for e[lo..]; type is the
// NUL-terminated name of the shifted operand's type; text is the NUL-terminated
// source text of the assert argument. fail_div_overflow is MIN / -1 and MIN % -1;
// fail_alloc_count is new(T[n]) with a negative signed n; fail_overwrite is an
// assignment to an own reference-typed lvalue whose current value is not zero
// (D17.11), emitted in checked builds only.
void fort_rt_fail_bounds(int64_t index, uint64_t len, loc);
void fort_rt_fail_slice(int64_t lo, int64_t hi, uint64_t len, loc);
void fort_rt_fail_overflow(loc);
void fort_rt_fail_shift(int64_t count, const char* type, loc);
void fort_rt_fail_div_zero(loc);
void fort_rt_fail_div_overflow(loc);
void fort_rt_fail_alloc_count(int64_t n, loc);
void fort_rt_fail_overwrite(loc);
void fort_rt_panic(const char* ptr, uint64_t len, loc);
void fort_rt_assert_fail(const char* text, loc);

// Printing (D11.5, D11.7, D12.2): format one value per D11.7 and append it to the
// buffer of fd. A float arrives in its own type and prints with the shortest digits
// that round-trip in that type, which the runtime obtains from the C library and
// lays out itself (D18); the two entry points are the only float ones (D18.4).
// fort_rt_flush writes out one buffer (io.close and io.flush call it);
// fort_rt_flush_all writes out every buffer, at exit and before every failure.
void fort_rt_print_i64(int32_t fd, int64_t v);
void fort_rt_print_u64(int32_t fd, uint64_t v);
void fort_rt_print_f32(int32_t fd, float v);
void fort_rt_print_f64(int32_t fd, double v);
void fort_rt_print_bool(int32_t fd, uint8_t v);
void fort_rt_print_char(int32_t fd, uint8_t c);
void fort_rt_print_ptr(int32_t fd, const void* p);
void fort_rt_print_str(int32_t fd, const char* ptr, uint64_t len);
void fort_rt_print_enum(int32_t fd, int32_t v, const struct fort_rt_enum_member* m,
                        uint64_t n);
void fort_rt_flush(int32_t fd);
void fort_rt_flush_all(void);

// Process (D11.6, D8.6). main calls fort_rt_args_init, which builds the args
// slice from argv (one string per argument, NUL-terminated since it is the argv
// byte sequence itself), then fort_entry, then fort_rt_flush_all, and returns
// status & 0xFF. fort_entry is emitted by the compiler (module-system.md 11).
// The args slice lives for the whole process and std::libc declares
// fort_rt_args_ptr and fort_rt_args_len for sys.args(); fort_rt_args_init is
// called by main only and exists so the native runtime object, built without
// main, can be tested. fort_rt_exit flushes every buffer, then
// exit(status & 0xFF); std::libc declares it for sys.exit.
int main(int argc, char** argv);
int32_t fort_entry(const struct fort_slice* args);
void fort_rt_args_init(int argc, char** argv);
const struct fort_string* fort_rt_args_ptr(void);
uint64_t fort_rt_args_len(void);
void fort_rt_exit(int32_t status);
```

The float printers are the one place where the runtime uses the C library to format a value.
`fort_rt_print_f64` asks `snprintf("%.*e", ...)` for one significant digit, then two, and so on,
and keeps the first length whose text `strtod` reads back as the value; 17 digits for `f64` and
9 for `f32` (`strtof`) always read back, so the search ends. `printf` returns the nearest decimal
of the length asked for, which is not always the one to keep: for a normal power of two above the
minimum normal the values that read back as it reach half an ulp above and only a quarter below,
the binade below being coarser, so the nearest decimal can fall short of that interval while the
next one up falls inside it, and the runtime tries that neighbour before lengthening. The
neighbour below never needs trying, the gap below a float never being wider than the gap above.
This asks two things of the C library that the standard permits but does not require and glibc
provides: a correctly rounded `printf` (ties to even) and a correctly rounded `strtod`, down to
the subnormals, where it also reports `ERANGE`, which the runtime ignores because only the value
matters. The runtime lays the digits out itself. A rewrite of the runtime in fort must reproduce
the text D18.2 fixes; it need not reproduce this search.

This list is complete (D11.6): the standard library declares no other `fort_rt_*` symbol. It
declares `fort_rt_args_ptr`, `fort_rt_args_len`, `fort_rt_flush`, `fort_rt_flush_all` and
`fort_rt_exit` in `std::libc` (`stdlib.md` 3) and reaches `errno` through libc's
`__errno_location`.

### 5.2 Messages

Each failure writes exactly one line, after `fort_rt_flush_all`, then calls `abort()` (D11.4):

| Entry point                 | Line after `<file>:<line>:<col>: ` (D11.4)                 |
|-----------------------------|------------------------------------------------------------|
| `fort_rt_fail_bounds`       | `runtime error: index 5 out of range for length 3`         |
| `fort_rt_fail_slice`        | `runtime error: slice bounds 2..7 out of range for length 3` |
| `fort_rt_fail_overflow`     | `runtime error: integer overflow`                          |
| `fort_rt_fail_shift`        | `runtime error: shift count 64 out of range for i64`       |
| `fort_rt_fail_div_zero`     | `runtime error: division by zero`                          |
| `fort_rt_fail_div_overflow` | `runtime error: division overflow`                         |
| `fort_rt_fail_alloc_count`  | `runtime error: negative allocation count -1`              |
| `fort_rt_new` (overflow)    | `runtime error: allocation size overflow`                  |
| `fort_rt_new` (no memory)   | `runtime error: out of memory`                             |
| `fort_rt_fail_overwrite`    | `runtime error: overwriting owned value`                   |
| `fort_rt_panic`             | `panic: <message bytes>`                                   |
| `fort_rt_assert_fail`       | `assertion failed: <expression text>`                      |

`<file>` is as in section 4. Numbers in messages are decimal; the index, the slice bounds and
the allocation count are printed as signed values. Falling off the end of a
`noreturn` function executes a bare trap instruction (D8.5): the process dies with SIGILL and no
message.

### 5.3 Buffering

| fd    | Used by                              | Policy (D11.5)                                   |
|-------|--------------------------------------|--------------------------------------------------|
| 1     | `print`, `println`, `fprint(1, ...)` | buffered; flushed when full, at exit, on failure |
| 2     | `eprint`, `eprintln`                 | unbuffered; every call writes immediately        |
| other | `fprint(fd, ...)`, `fprintln`        | one buffer per descriptor, same policy as 1      |

`fprint(1, ...)` shares the stdout buffer with `print` (D11.5). The runtime tells descriptors
apart by number alone, so `fprint(2, ...)` behaves as `eprint`. An `extern` write to a
descriptor bypasses the buffers; a program that mixes the two on one descriptor flushes first
(`io.flush`, D11.5). A write error on any descriptor is ignored; the bytes are dropped. The
buffer size is the runtime's choice.

## 6. Code generation contract

This section is normative for the compiler. The assembly it emits must satisfy every item.

1. **Syntax.** GNU assembler, AT&T syntax, one `.s` file per program (D14.3). The file begins
   with `.text` and ends with `.section .note.GNU-stack,"",@progbits` so the stack is
   non-executable.
2. **Sections.** Functions in `.text`, aligned with `.p2align 4`. String literals (`.asciz`, one
   NUL beyond `len`, D3.7), enum name tables and constants (`Type NAME = init;`, D7.10) in
   `.rodata`; a constant whose initializer contains an address (`&global`, a function name) goes in
   `.data.rel.ro` instead so the loader can relocate it. `mut` globals with a nonzero
   initializer in `.data`, all-zero ones in `.bss`. Data is aligned with `.balign` to its type's
   alignment (D3.1, D3.8): scalars to their size; pointers, function pointers, slices and strings
   to 8; arrays to their element; structs to their most-aligned field.
3. **Symbols.** Fort functions, constants and globals use the dotted names of D9.7 and are
   emitted with `.globl`, `.type name, @function` or `@object`, and `.size`. `fort_entry` is
   `.globl`. Every compiler-generated label starts with `.L`: `.Lstr<N>` (literals),
   `.Lfile<N>` (one NUL-terminated file path per module, as printed by section 4), `.Lfail<N>`
   (failure stubs), `.Lenum.<module.path.name>` (enum tables), and ordinary control-flow labels.
   `extern` symbols are used unmangled (D9.8).
4. **Position independence** (D14.3, D16). Data is addressed RIP-relative (`lea sym(%rip)`,
   `mov sym(%rip), %reg`). Calls to `extern` and runtime functions are `call name@PLT`; their
   addresses, when used as values, come from `mov name@GOTPCREL(%rip), %reg`. Fort-to-fort calls
   are direct (`call a.b.f`) and a fort function's address is `lea a.b.f(%rip), %reg`. Absolute
   addresses appear only in data (`.quad a.b.f`) in `.data` or `.data.rel.ro`.
5. **Registers and stack** (D9.9). Scalar arguments and results follow System V: integer class in
   `rdi rsi rdx rcx r8 r9` and `rax`, floats in `xmm0` to `xmm7` and `xmm0`, further arguments on
   the stack right to left in 8-byte slots. `rbx`, `rbp`, `r12` to `r15` are preserved. `rsp` is a
   multiple of 16 immediately before every `call`. Every function establishes a frame with
   `push %rbp; mov %rsp, %rbp`. The red zone is not used.
6. **Aggregates** (D9.9). A struct, fixed array, slice or `string` argument is copied by the
   caller into its own frame and its address is passed in the integer slot the argument occupies.
   An aggregate result is written into caller-provided storage whose address is passed in `rdi`
   before all other arguments and returned in `rax`. Slice and string headers are 16 bytes,
   `ptr` at offset 0 and `len` at offset 8; enums are 4 bytes; `bool` and `char` are 1 byte.
7. **Extern calls** (D9.8). Before every `call` to a non-fort symbol, `al` holds the number of
   vector registers used by the call (zero when no float argument is passed), which is what a
   variadic callee reads. Narrow arguments are extended before the call (item 8) and a narrow
   return value is re-extended from `al` or `ax` after it.
8. **Normalization.** A `bool`, `char`, `u8`, `u16`, `i8` or `i16` value held in a register is
   always extended to 32 bits: `movzbl`/`movzwl` for the unsigned ones, `bool` and `char`,
   `movsbl`/`movswl` for `i8`/`i16`. Extension happens on every load from memory, on function
   entry for each narrow parameter (so C callbacks are covered), and after an extern call.
   Stores use the narrow width. `bool` is stored as 0 or 1; comparisons produce it with `setcc`
   followed by `movzbl`.
9. **Stack probing** (D10.8). A frame larger than 4096 bytes is established by touching one byte
   in every page of the frame, from the highest address down, before any other store into it.
10. **Checks and failure stubs.** Every runtime check is a compare-and-branch to an out-of-line
    stub placed after the function body. A stub loads the arguments of section 5.1, loads
    `.Lfile<N>` into `rdi` and the line and column into `esi` and `edx` (or the following
    registers when the entry point takes values first), calls the entry point through `@PLT`,
    and is followed by `ud2`. One stub per check site; the line and column are the position rule
    of section 4.
11. **Integer checks** (D11.1, D11.3). In checked mode: signed `add`/`sub`/`imul`/`neg` are
    followed by `jo`; unsigned `add`/`sub` by `jc`; unsigned `mul` by `jo` or `jc`; overflow is
    detected at the operand's width. A shift count is compared unsigned against the width and
    branches to the stub when not below it; in release mode it is masked with `and` by
    `width - 1`. Division tests the divisor for zero (`fort_rt_fail_div_zero`) and, for signed
    types, `-1` against a dividend equal to the minimum (`fort_rt_fail_div_overflow`), in both
    modes, then uses `cqo`/`cdq` and `idiv`, or `xor %edx, %edx` and `div`.
12. **Bounds checks** (D6.8, D6.9). The index is sign-extended (signed types) or zero-extended
    (unsigned types) to 64 bits, then one unsigned compare against the length branches to the
    stub when not below it. Slicing checks `hi <= len` and `lo <= hi` with unsigned compares.
    Fixed-array lengths are immediates. `--no-bounds-check` omits exactly these compares.
13. **`new` and `del`** (D10.2, D10.3, D17.9). `new(T)` calls `fort_rt_new(sizeof(T), 1, loc)`
    and yields `rax`; `new(T[n])` first tests a signed `n` with `test`/`js` into a
    `fort_rt_fail_alloc_count` stub, then calls `fort_rt_new(sizeof(T), n, loc)` and builds the
    header `{rax, n}`. `del(x)` calls `fort_rt_del` with the pointer, or the slice's `ptr`, and
    then, when `x` is an lvalue, stores the zero value into `x`: eight zero bytes for a pointer
    or `void*`, sixteen for a slice or `string`. On an rvalue operand nothing is stored.
14. **Ownership** (D17). `own` is erased: an `own` type has the representation, alignment,
    argument class (item 5, item 6) and normalization of the same type without `own`, and
    neither the emitted code nor the runtime carries any ownership information. `move(lv)`
    loads the operand's value as the expression result and stores the zero value into `lv`
    (the whole zeroed value for an owning aggregate, D17.6), whatever the build mode.
    In checked mode only, an assignment whose target is an lvalue of `own` reference type
    (pointer, `void*`, slice or `string`, not an owning aggregate) loads the target's pointer
    word (offset 0 for a slice or `string`) after the right-hand side has been evaluated and
    immediately before the store, tests it, and branches to a `fort_rt_fail_overwrite` stub when
    it is non-zero (D17.11); the stub's position is the `=` token. Release mode emits the plain
    store, and `--no-bounds-check` does not affect the check. Declarations, `move`, `del` and
    assignments of owning aggregates never emit it.
15. **Builtins** (D12.2). `print`, `println`, `eprint`, `eprintln`, `fprint`, `fprintln`
    evaluate `fd` (1, 2, or the first argument, once) and then each argument left to right,
    calling one entry point per argument: `i8 i16 i32 i64` sign-extended to `fort_rt_print_i64`;
    `u8 u16 u32 u64` zero-extended to `fort_rt_print_u64`; `f32`/`f64` to `_f32`/`_f64`; `bool`,
    `char` to `_bool`, `_char`; enums with the address and length of `.Lenum.<path.name>` to
    `_enum`; pointers, `void*` and function pointers to `_ptr`; `string` as `ptr`, `len` to
    `_str`. `println` and friends end with `fort_rt_print_char(fd, 10)`. `assert(cond)` branches
    on `cond` to a `fort_rt_assert_fail` stub whose text is a `.Lstr<N>`; it is emitted in both
    modes. `panic(msg)` calls `fort_rt_panic` and is followed by `ud2`.
16. **`noreturn`** (D8.5). `ud2` follows the body of a `noreturn` function and every call to one;
    reaching it raises SIGILL with no message.
17. **Enum tables.** `.Lenum.<path.name>` is an array of `fort_rt_enum_member` (16 bytes each:
    `.long value`, `.zero 4`, `.quad .Lstr<N>`), one entry per member in declaration order.
18. **`fort_entry`.** Emitted in the entry module: copies the 16-byte slice it receives into its
    frame, calls `<entry>.main` with the copy's address in `rdi` when `main` takes `args`, or
    with no arguments otherwise, and returns `main`'s `eax`.

A checked-mode `fn i32 add(i32 a, i32 b) { return a + b; }` in `main.ft` (the `+` at line 2,
column 14) is emitted as:

```asm
        .text
        .globl  main.add
        .type   main.add, @function
        .p2align 4
main.add:
        push    %rbp
        mov     %rsp, %rbp
        mov     %edi, %eax
        add     %esi, %eax
        jo      .Lfail0
        pop     %rbp
        ret
.Lfail0:
        lea     .Lfile0(%rip), %rdi
        mov     $2, %esi
        mov     $14, %edx
        xor     %eax, %eax
        call    fort_rt_fail_overflow@PLT
        ud2
        .size   main.add, .-main.add

        .section .rodata
.Lfile0:
        .asciz  "main.ft"

        .section .note.GNU-stack,"",@progbits
```

## 7. Testing

### 7.1 Layout (D14.4)

```sh
test/
  test.h                       C unit-test macros
  common.h                     TEST_UNUSED and shared helpers, provided by the implementation
  <component>_test.c           one C suite per compiler component (lexer_test.c, ...)
  lang/
    run_tests.py               runs every language test below
    run_tests_test.py          the harness's own unit tests
    xfail.txt                  tests the compiler cannot pass yet
    bootstrap-unsupported.txt  tests the C bootstrap must reject
    run/<area>/NNN_name.ft     compile, run, compare
    fail/<area>/NNN_name.ft    must not compile, with annotated errors
    run/modules/<name>/main.ft multi-file run test; the directory is the root
    fail/modules/<name>/main.ft multi-file fail test, same rule
    ffi/*.c                    C helpers for `link:` directives
    programs/*.ft              larger programs, treated as run tests
```

`<area>` is one of `lexical constants operators casts mutability declarations control switch
defer functions structs enums arrays slices strings pointers globals builtins errors modes modules
ffi stdlib ownership`: `errors` holds the `abort` tests of the runtime checks (the overwrite
check of D17.11 included, with its `--release` twin under `modes`), `modes` the `--release`
tests, `stdlib` the tests of the standard library once it exists, and `ownership` the run tests
of `own`, `move`, lending and `del` and the fail tests of every rule of D17. `NNN` is a three-digit
sequence number and `name` a short snake-case description. Paths in `link:` are relative to
`test/lang/`.

### 7.2 Directives (D14.5)

All directives are `//!` lines at the top of the file, except `error`, which annotates a line:

| Directive                       | Meaning                                                  |
|---------------------------------|----------------------------------------------------------|
| `//! run` or `//! fail`         | required, first line                                     |
| `//! flags: --release`          | extra compiler flags                                     |
| `//! args: a b c`               | program arguments, split on spaces                       |
| `//! link: ffi/helpers.c`       | C helper to link, relative to `test/lang`; repeatable    |
| `//! stdin:` then `//< ` lines  | standard input, one line per `//< ` line                 |
| `//! stdout:` then `//| ` lines | expected stdout, compared exactly, trailing spaces included |
| `//! exit: N`                   | expected exit status, default 0                          |
| `//! abort`                     | expect termination by SIGABRT                            |
| `//! stderr: <substring>`       | `<substring>` must appear in stderr; repeatable          |
| `//! error: <substring>`        | `fail` tests only, at the end of the offending line      |
| `//! error-any: <substring>`    | `fail` tests only, at the top                            |

The expected output is each `//| ` line's text after the marker followed by a newline; a bare
`//|` is an empty line. Output without a final newline cannot be expressed, so tests end their
output with `println`. Every `stderr:` substring must appear in stderr.

In a `fail` test every `//! error:` line must produce a diagnostic on that line containing the
substring, and no unannotated diagnostic may occur; `//! error-any:` requires some diagnostic to
contain the substring and is for errors without a useful line, such as circular imports.

### 7.3 What the harness does

`test/lang/run_tests.py [options] [filter...]` (Python 3, standard library only) runs the
compiler named by `--fort` (default `$FORT`, else `build/debug/fort`) with `--std-dir` from
`--std-dir` (default `$FORT_STD_DIR`, else `std` beside the compiler) and `--cc` from `--cc`
(default `clang`, which must be a clang as `--cc` is, D14.1). `--target` (default
`x86_64-linux-gnu`) is the triple the harness passes to `--cc` as `--target=<triple>` when it
links a test's C helpers itself. `--verify-ir` runs `fort -S -o prog.ll <test>` for every test
whose compilation succeeds and verifies the module with `<opt> -passes=verify -disable-output`
(D19.1); `--opt` names that program (default `opt-18`). A module the verifier rejects is a FAIL;
an `opt` that cannot be launched, times out or dies by a signal is an ERROR, like a compiler exit
2 (D14.1). `--runner` names a command that runs the programs when binfmt does not, `-j` the
number of parallel tests, `--timeout` the seconds per step, `-v` prints the commands and outputs
of failures and `--keep` keeps the temporary directories. The
compiler runs with `test/lang` as its working directory (D14.4). For each test, in a fresh
temporary directory that is also `TMPDIR`, with `LC_ALL=C`, `QEMU_LD_PREFIX` set unless
inherited and core dumps disabled:

| Directive    | Harness action                                                              |
|--------------|-----------------------------------------------------------------------------|
| `run`        | `fort <flags> -o prog <test>` must exit 0; run `prog`; compare its output    |
| `fail`       | `fort <flags> -o prog <test>` must exit 1 with only annotated errors        |
| `flags:`     | appended to the `fort` command line                                         |
| `args:`      | appended to the program's command line                                      |
| `link:`      | `fort -c`, then `cc -o prog prog.o <helpers> <std-dir>/fort_rt.o`           |
| `stdin:`     | the `//< ` lines, each with a newline, are the program's stdin; else empty   |
| `stdout:`    | the program's stdout must equal the `//| ` lines; with no directive, empty   |
| `exit:`      | the program's exit status must equal `N`                                    |
| `abort`      | the program must die with SIGABRT (status 134 from the shell)               |
| `stderr:`    | each substring must occur in the program's (`run`) or compiler's (`fail`) stderr |
| `error:`     | an `error:` line with that file and line must contain the substring         |
| `error-any:` | some `error:` line must contain the substring                               |

`<test>` is the test file, or `main.ft` in a multi-file test; `-o` names a file in the temporary
directory even for a `fail` test, so a compiler that wrongly succeeds never writes `a.out` into
`test/lang`. Before the `stderr:` substrings of a `run` test are looked for, the lines qemu-user
adds when a signal kills the program (`qemu: uncaught target signal 6 (Abort) - core dumped`)
are dropped, since native execution prints nothing there. For `error:` the harness also fails
the test when the compiler reports an `error:` for a line that carries no annotation; a
diagnostic matched by an `error-any:` counts as annotated, and further diagnostics on an
annotated line are accepted. In multi-file tests, directives are read from `main.ft`,
`//! error:` annotations from every `.ft` file in the directory (D14.4), and no `-I` is passed
because the directory is the root (D9.2). A compiler exit status other than 0 or 1 (2 is a
usage, toolchain or internal error, D14.1), a compiler crash, a compiler timeout, a failure of
the harness's own `link:` step and a program that cannot be started are `ERROR`, not a verdict
about the test; a program that times out is a `FAIL`.

Two expectation files beside the harness list path prefixes of tests (relative to `test/lang`,
`#` comments allowed). `xfail.txt` names the tests the compiler cannot pass yet: a listed test
that fails or errors is `XFAIL`, a listed test that passes is `XPASS` and fails the run, so the
list shrinks in the commit that makes tests pass. `bootstrap-unsupported.txt` names the tests
that use features the C bootstrap deliberately lacks (floats, multi-dimensional arrays,
`do`-`while`, `?:`, function-pointer types and values); each is judged as a `fail` test whose
only expectation is a diagnostic containing `not supported by the bootstrap compiler`, whatever
its own kind, and `--no-unsupported` (for the self-hosted compiler) judges them normally.
`--xfail` and `--unsupported` name other lists; `--no-xfail` ignores the first.

The harness prints one `PASS`, `FAIL`, `XFAIL`, `XPASS` or `ERROR` line per test with the
reason where there is one, then a summary, and exits with 1 if any test is `FAIL`, `XPASS` or
`ERROR`; each `filter` selects the tests whose path contains it. `--list` prints the selected
tests and their count. `--lint` validates the corpus without a compiler and fails on: a first
line other than `//! run` or `//! fail` or one that does not match the directory; an unknown,
malformed, duplicated or empty directive; `exit` together with `abort`; a run-only directive in
a `fail` test or `error`/`error-any` in a `run` test; a `link:` file that does not exist; a
`//<` or `//|` not followed by a space or outside its block; a directive after the header or in
a sibling module; a `fail` test with neither `error:` nor `error-any:`; an unknown area, a
badly named test, a stray file, a directory test outside `modules`, and a gap or duplicate in
the `NNN` numbering of an area; and an expectation-list entry that matches no test. A run
performs the same checks first and stops when they fail, and fails when the filters select no
test.

### 7.4 Examples

The examples are abbreviated; the seed corpus holds full versions in the areas named. A run
test under `test/lang/run/arrays/`:

```fort
//! run
//! stdout:
//| 3 2
fn i32 main() {
    i32[3] a = {1, 2, 3};
    println(a[2], " ", a[1]);
    return 0;
}
```

A fail test, `test/lang/fail/mutability/001_assign_immutable.ft`:

```fort
//! fail
fn i32 main() {
    i32 x = 1;
    x = 2;                       //! error: immutable
    return x;
}
```

An abort test with `stderr:` under `test/lang/run/errors/`; `before` is flushed by the failure
path (D11.4):

```fort
//! run
//! abort
//! stderr: runtime error: index 5 out of range for length 3
//! stdout:
//| before
fn i32 main() {
    own mut i32[] xs = new(i32[3]);
    defer del(xs);
    println("before");
    xs[5] = 1;
    return 0;
}
```

An ownership fail test under `test/lang/fail/ownership/`, one annotated line per rule (D17.5,
D17.8, D17.9):

```fort
//! fail
struct node { i32 v; }

fn i32 main() {
    own mut node* a = new(node);
    own mut node* b = a;         //! error: move
    mut node* c = new(node);     //! error: would leak
    node* view = a;
    del(view);                   //! error: own
    del(a);
    return 0;
}
```

A multi-file test, `test/lang/run/modules/<name>/main.ft` with `util.ft` beside it:

```fort
//! run
//! stdout:
//| 7
import util;
import util::twice;

fn i32 main() {
    println(util.inc(twice(3)));
    return 0;
}
```

```fort
// util.ft
fn i32 twice(i32 x) {
    return x * 2;
}

fn i32 inc(i32 x) {
    return x + 1;
}
```

A release-mode test under `test/lang/run/modes/`, whose checked-mode twin uses `//! abort` and
`//! stderr: runtime error: integer overflow` instead:

```fort
//! run
//! flags: --release
//! stdout:
//| -2147483648
fn i32 main() {
    mut i32 x = 2147483647;
    x += 1;
    println(x);
    return 0;
}
```

An FFI test under `test/lang/run/ffi/`, with its helper in `test/lang/ffi/helpers.c`:

```fort
//! run
//! link: ffi/helpers.c
//! stdout:
//| 12
extern fn i32 helper_add(i32 a, i32 b);

fn i32 main() {
    println(helper_add(5, 7));
    return 0;
}
```

```c
int32_t helper_add(int32_t a, int32_t b) { return a + b; }
```

`args:` and `exit:` are exercised by a `main(string[] args)` that returns `cast(args.len, i32)`
under `//! args: one two` and `//! exit: 3`.

### 7.5 Compiler unit tests in C

Each `test/<component>_test.c` is one suite built against the compiler's sources and `test.h`:
tests are defined with `TEST(name, body)`, registered with `TEST_RUN`, and the suite exits through
`TEST_EXIT` with status 0 (ok), 1 (a failed assertion) or 2 (a test error). The first command-line
argument, when present, is a name prefix selecting tests. `TEST_ASSERT_*` macros log file, line,
expression, actual and expected values and return `TEST_RESULT_FAIL`. A body is one macro
argument, so a comma outside parentheses inside it must be parenthesized.

```c
#include "test.h"
#include "lexer.h"

TEST(lexes_hex_literal, {
    lexer_t lx;
    lexer_init(&lx, "0x7F");
    token_t t = lexer_next(&lx);
    TEST_ASSERT_EQ_INT32(t.kind, TOK_INT);
    TEST_ASSERT_EQ_INT64(t.value, 127);
})

int main(int argc, char** argv) {
    TEST_INIT("lexer", argc, argv);
    TEST_RUN(lexes_hex_literal);
    TEST_EXIT();
}
```

The names `lexer_t`, `lexer_init`, `lexer_next`, `token_t` and `TOK_INT` are chosen in the
implementation phase (section 8). Unit tests cover what language tests cannot observe directly:
the token stream, the parser's speculative rewinds, constant folding at the edges of the
`[-2^63, 2^64 - 1]` range (D4.4), the mutability-drop check (D5.4) and option parsing.

### 7.6 Coverage target (D14.6)

The corpus aims at about three lines of test for each line of compiler source: `wc -l` over
`test/*.c`, `test/lang/**/*.ft` and `test/lang/ffi/*.c` against `wc -l` over `src/bootstrap/*.c`,
`src/bootstrap/*.h` and `src/fort/*.ft`, the runtime and standard library excluded from both
sides. The seed tests of the design phase establish the format with one example per area; the
full corpus is sized as follows,
in files:

| Area                    | run | fail |
|-------------------------|-----|------|
| lexer and literals      | 30  | 30   |
| operators and constants | 50  | 35   |
| casts                   | 25  | 20   |
| mutability              | 20  | 40   |
| control flow            | 45  | 25   |
| functions               | 30  | 25   |
| structs                 | 30  | 20   |
| enums                   | 20  | 15   |
| arrays                  | 30  | 15   |
| slices and `new`/`del`  | 35  | 20   |
| strings                 | 30  | 10   |
| pointers                | 25  | 20   |
| defer                   | 20  | 10   |
| modules                 | 30  | 30   |
| FFI                     | 20  | 10   |
| globals                 | 15  | 10   |
| builtins                | 30  | 10   |
| stdlib                  | 45  | 5    |
| ownership               | 15  | 20   |
| programs                | 20  | 0    |
| total                   | 565 | 370  |

Every operator, keyword and builtin appears in at least one run test and, where it can be
misused, one fail test (decisions.md checklist). Every runtime check has an `abort` test in
checked mode and, for the checks that release mode removes, a release-mode twin.

## 8. Compiler architecture sketch

This section is not normative. It records the intended shape so that the other sections are
implementable; the design is to be planned in the implementation phase.

- **Language and dependencies.** C11, POSIX, no external libraries; one binary `fort`. The
  repository holds `src/` (compiler), `runtime/fort_rt.c` (runtime), `std/*.ft` (standard library),
  `test/` (section 7) and a build script producing `build/fort` and `build/std/` with the library
  sources and `fort_rt.o`.
- **Driver.** Parses options (section 1), owns the module table keyed by real path, runs the
  passes below, invokes `--cc` over the emitted module (D14.3) and maps failures to exit
  statuses.
- **Lexer.** A complete token array per file (kind, position, literal value) applying D2; the
  array makes each speculative parse of `grammar.md` section 7 a saved and restored index.
- **Parser.** Recursive descent over the token array, one AST per module, with the nesting
  limit of D2.11; imports go back to the driver, which loads modules until the closure is
  complete and acyclic.
- **Checker.** Two-phase top-level resolution per D7.10: phase one enters every module-level
  name; phase two resolves types, signatures, constant values and struct sizes lazily with cycle
  detection, so declaration order never matters; then each body is checked against D3 to D8 and
  D17 and the AST is annotated with types, constant values, lvalue mutability, resolved symbols
  and, on each assignment, whether the target is an `own` lvalue that needs the overwrite check.
- **Codegen.** One forward pass over the annotated AST appending text to a single LLVM IR
  module (D19.1, section 6), with no intermediate representation of its own, no libLLVM and no
  register allocation: locals are `alloca`s in the entry block, intermediates are SSA
  temporaries, and deferred statements are expanded statically at each exit (D7.8).
- **Memory.** Arenas per compilation; nothing is freed before exit.

## 9. Not in v1

Deferred by D15 and the design reviews: debugger support (no DWARF, no `.loc`; the frame pointer
and the symbol names in section 6 are what a debugger gets), an optimizer and `-O` levels,
warnings, separate compilation and incremental builds, a package manager, documentation
generation, cross-compilation and any target other than x86-64 Linux, `--help` text beyond the
usage line, and conditional compilation. The idioms that replace the deferred language features
are listed with each item in D15.
