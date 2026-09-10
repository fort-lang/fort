# fort: project overview

fort is a systems programming language that takes C as its foundation and makes a small number of
targeted changes to remove whole classes of bugs while keeping C's directness: constants by
default, arrays and slices that know their length, checked arithmetic, no implicit conversions,
no fallthrough, no `goto`, a real module system. It is explicitly not trying to be Rust or C++.
A C programmer should be productive in an afternoon, and the compiler should stay small enough for
one person to understand.

## Status

The v1 language design is complete. Every open question from the original notes has a recorded
decision, and the specification documents in this directory agree with the decision log. The
next phase is the implementation strategy and the compiler itself.

| Phase                     | State    |
|---------------------------|----------|
| 1. Language design (v1)   | complete |
| 2. Implementation plan    | next     |
| 3. Compiler in C          | later    |
| 4. Standard library       | later    |
| 5. Self-hosting           | later    |

## Documents

Read them in this order. Two are normative and win over the rest.

| Document             | Role                                                                  |
|----------------------|-----------------------------------------------------------------------|
| `decisions.md`       | **Normative.** Numbered decision log (`D5.3`); the source of truth.   |
| `grammar.md`         | **Normative.** Complete EBNF and the parser's disambiguation rules.   |
| `core-language.md`   | Lexical structure, declarations, expressions, statements, functions,  |
|                      | builtins.                                                             |
| `type-system.md`     | Every type, mutability levels, conversions, untyped constants, layout.|
| `memory-model.md`    | Stack and heap, `new`/`del`, slices and strings, runtime checks, the  |
|                      | runtime-error contract, undefined behavior.                           |
| `module-system.md`   | Files and imports, name resolution, mangling, C FFI, calling          |
|                      | convention, entry point.                                              |
| `stdlib.md`          | The v1 standard library, module by module, with signatures.           |
| `toolchain.md`       | The `fort` command, build modes, diagnostics, the C runtime, code     |
|                      | generation contract, test conventions.                                |

Seed tests that exercise every feature live under `test/lang/` in the format defined in
`toolchain.md`; they are the first tests the compiler has to pass.

## Design principles

1. **Familiarity first.** C syntax and semantics wherever the change buys nothing.
2. **Explicit over implicit.** No conversions, no promotions, no truthiness, no hidden
   allocation, no fallthrough. `cast`, `mut` and `new` are written out.
3. **Safety without complexity.** Bounds-checked arrays and slices, checked arithmetic, mandatory
   initialization, no pointer arithmetic, exhaustive enum switches.
4. **Manual control.** Explicit `new`/`del`, C-compatible struct layout, a raw-pointer escape
   hatch (`p[lo..hi]`) for foreign memory.
5. **Minimal runtime.** A few hundred lines of C: allocation, checks, printing, process start.

## What fort changes relative to C

| Area          | C                                  | fort                                       |
|---------------|------------------------------------|--------------------------------------------|
| Mutability    | mutable by default, `const`        | immutable by default, `mut` (D5)           |
| Conversions   | implicit promotions and narrowing  | none; `cast(x, T)` (D3.14)                 |
| Arrays        | decay to pointers, no length       | `T[N]` values and `T[]` slices with `.len` |
| Strings       | `char*` with NUL                   | `string`: immutable `{ptr, len}` (D3.7)    |
| Overflow      | undefined for signed               | trap in checked builds, wrap in release    |
| Bounds        | unchecked                          | always checked (D10.6)                     |
| Initialization| optional                           | mandatory; `new` zeroes (D7.1, D10.2)      |
| Null          | `0`/`NULL`                         | `null` keyword, pointers only (D10.5)      |
| Switch        | fallthrough                        | none; `case a, b:`; exhaustive enums (D7.6)|
| Cleanup       | manual on every path               | `defer` (D7.8)                             |
| Control flow  | `goto`, optional braces            | no `goto`; braces required (D7.4)          |
| Modules       | headers and `#include`             | `import a::b;`, one module per file (D9)   |
| Functions     | `int f(int)`                       | `fn i32 f(i32)`; function-pointer types    |
|               |                                    | read the same (D8.1)                       |
| Enums         | integer constants                  | typed, scoped `Color.Red` (D3.9)           |
| Bool          | `int`                              | `bool`, `true`, `false` (D3.3)             |

## Deliberately not in v1

Generics, unions, tagged unions, methods, closures, variadics, overloading, visibility modifiers,
type aliases, separate compilation, conditional compilation, labeled `break`. The full list with
the idioms that replace each is decision D15.

## Success criteria

1. A C programmer can read the whole specification in a day and write real code the same day.
2. The common C bug classes (buffer overflows, uninitialized reads, silent overflow, switch
   fallthrough, sign and width confusion) are compile errors or runtime errors, not silent.
3. Performance within 10% of equivalent C for typical systems code, bounds checks included.
4. Real systems software, including the fort compiler itself, can be written in it.
5. The compiler fits in one head: whole-program, no IR beyond the AST, text assembly out.

## Influences

C for the base; Rust for immutability by default and the absence of implicit conversions; Go for
the module model, untyped constants and `defer`; Zig for checked arithmetic with explicit wrapping
operators and no hidden allocation.
