# fort module system

This document specifies modules, imports, name resolution, symbol naming, the C foreign function
interface and the compilation model of fort v1. It implements D9 together with the parts of D3,
D7, D8, D11, D13, D14 and D17 that touch modules. Where it disagrees with `decisions.md` or
`grammar.md`, those files win (D1.2).

Sections: 1 Modules and files; 2 Search roots and the entry file; 3 Import forms and
resolution; 4 Qualified access; 5 Namespaces, lookup and shadowing; 6 Import graph and export
rules; 7 Symbol names; 8 C foreign function interface; 9 Calling convention; 10 Compilation
model; 11 Entry point and program start; 12 Worked examples; 13 Module diagnostics; 14 Not in v1.

## 1. Modules and files

One source file is one module (D9.1). A file contains no module declaration; its module path is
the file path relative to a search root (section 2) with `/` replaced by `::` and `.ft` dropped.
The last segment is the short name, which an import binds by default.

| Module path     | File                     |
|-----------------|--------------------------|
| `main`          | `<root>/main.ft`         |
| `std::io`       | `<std>/io.ft`            |
| `std::str`      | `<std>/str.ft`           |
| `util::strings` | `<root>/util/strings.ft` |
| `geom::vec`     | `<root>/geom/vec.ft`     |

- The extension is `.ft` (D1.1). A file with any other extension is never a module.
- Every segment is an identifier (D2.3) that is neither a keyword nor a reserved word (D2.4); a
  file or directory named otherwise is unreachable by any import. This is why the standard
  library's string module is `std::str`, not `std::string` (D9.1).
- A directory is not a module: `util::strings` says nothing about `<root>/util.ft`, which would
  be the unrelated module `util`. Paths are case-sensitive like the file names they map to.

## 2. Search roots and the entry file

The entry file is the one `.ft` file named on the `fort` command line (D14.1). Its module path is
its base name without `.ft`: `main.ft` is the module `main`, `src/app.ft` is the module `app`.
The base name need not satisfy the segment rule of section 1 (D9.1): the entry file is named on the
command line, not reached by an import path, so `007_case.ft` and `my-app.ft` are legal entries
whose modules are `007_case` and `my-app`. The two characters it may not contain are `.` and `:`,
which would let the entry's symbols collide with another module's (section 7): `my.app.ft` would be
the module `my.app`, whose `main` is the `my.app.main` a module `my::app` already emits, and
`my:app.ft` would emit `myapp.main`, which is module `myapp`'s. Every other character reaches the
symbol unchanged and so cannot spell a path. A module whose name is not an identifier cannot be
imported, since no import path spells it.

Search roots, in order (D9.2):

1. the directory containing the entry file;
2. each `-I <dir>` directory, in command-line order;
3. the standard library directory (`--std-dir`, else `$FORT_STD_DIR`, else `../std` relative to
   the compiler binary; see `toolchain.md`).

The first segment `std` is reserved for the standard library. A path beginning with `std` is
looked up only in the standard library directory, `std` mapping to that directory itself
(`std::io` is `<std>/io.ft`); a path that does not begin with `std` is never looked up there. A
file `std/x.ft` under any other root is unreachable.

- The current working directory is never a root. `fort src/main.ft` run from the project
  directory makes `src/` a root, not `.`; a module in `./lib/` needs `-I .` and is then
  `lib::name`.
- Import paths are root-relative, never file-relative: `util/a.ft` imports its sibling as
  `util::b`.
- For one reading of a path (section 3), the first root in order that contains the file wins.
- A module's identity is the real path of its file, after resolving symbolic links and `..`.
  Reaching one file through two module paths, for example through `-I .` combined with the entry
  directory, is an error (section 13).

## 3. Import forms and resolution

Imports appear at the top of a file, before any declaration (D9.3; `grammar.md` section 2). An
`import` after a declaration is a parse error. The order of imports is irrelevant.

| Form                                       | Binds                           | Use              |
|--------------------------------------------|---------------------------------|------------------|
| `import std::io;`                          | `io` to the module `std::io`    | `io.close(fd)`   |
| `import std::io as sysio;`                 | `sysio` to the module `std::io` | `sysio.close(fd)`|
| `import std::str::cmp;`                    | `cmp` to that declaration       | `cmp(a, b)`      |
| `import std::str::cmp as compare;`         | `compare` to that declaration   | `compare(a, b)`  |
| `import std::str::{find, cmp as compare};` | `find` and `compare` separately | `find(s, c)`     |

Resolution of `import a::b::c;` (D9.3). The grammar does not know whether `c` is a module or a
declaration; the loader tries two readings, of which exactly one must succeed:

| Reading | Condition                                        | Result                            |
|---------|--------------------------------------------------|-----------------------------------|
| module  | a file `a/b/c.ft` exists under some root         | `c` bound to the module `a::b::c` |
| symbol  | `a/b.ft` exists under some root and declares `c` | `c` bound to that declaration     |

- If both readings succeed the import is ambiguous and an error, whichever roots the two files
  live under.
- If neither succeeds the import is an error: "not found" when no file exists for either
  reading, "has no declaration" when `a/b.ft` exists but lacks `c` (section 13).
- At most one trailing segment names a declaration, so a one-segment path (`import math;`) has
  only the module reading.

The braced form `import a::b::{s1, s2 as t};` is sugar for `import a::b::s1; import a::b::s2 as
t;` with the symbol reading forced: `a::b` must be a module file and every item one of its
declarations. `import std::{io, str};` is an error because `std` is not a module file.

Bindings:

- The bound name, the last segment or the `as` name, enters the module namespace (section 5). A
  name already declared or already bound in the file is a duplicate-binding error.
- Importable declarations are functions, `extern` functions, structs, enums, constants and
  globals. The import bindings of another module are not importable; there is no re-export.
- Enum members are not declarations. `import m::color::red;` fails with "module 'm::color::red'
  not found" because neither `m/color/red.ft` nor `m/color.ft` is a file. Write
  `import m::color;` and use `color.red`.
- One module may be imported under several names, and together with some of its declarations;
  the bindings name the same entities.
- There is no wildcard import. An unused import is not diagnosed (no warnings, D14.2).

## 4. Qualified access

A module binding `m` is used only as the left operand of `.` (D9.4). The parser produces the same
postfix `.` as for field access (`grammar.md` section 6); the checker resolves `m` first and,
finding a module, looks the name up in that module's namespace.

| Position       | Form                                       | Example                           |
|----------------|--------------------------------------------|-----------------------------------|
| call           | `m.f(args)`                                | `io.close(fd)`                    |
| value          | `m.C`, `m.g`                               | `math.PI`, `inp.frame += 1;`      |
| type           | `m.T`                                      | `math.vector v = {1.0, 2.0};`     |
| type operand   | `sizeof(m.T)`, `cast(p, m.T*)`, `new(m.T)` |                                   |
| struct literal | `m.T{...}`                                 | `math.vector{1.0, 2.0}`           |
| enum member    | `m.E.Member`                               | `inp.key.left`, or a `case` label |
| function value | `m.f`                                      | `fn i32(i32, i32) op = math.add;` |

- A type position admits exactly one dot (`qualified_name`, `grammar.md` section 4). `m` alone
  is an error in both value and type positions.
- Qualified access sees the declarations of `m`, not the imports of `m`.
- `m.g = e;` and `m.g++;` are allowed when `g` is a `mut` global (D7.10); assigning to `m.C` is
  an error like any assignment to an immutable lvalue (D5.7).

## 5. Namespaces, lookup and shadowing

Each module has one namespace holding its functions, `extern` functions, structs, enums,
constants, globals and import bindings (D7.9). Any two of these with the same name collide,
whatever their kinds: a struct `node` and a function `node` cannot coexist.

Lookup of an unqualified name proceeds (D7.9):

1. block scopes, innermost outward: locals and parameters, a local being visible only after its
   own declaration;
2. the module namespace of the current file;
3. the universe: `del`, `move`, `assert`, `panic`, `print`, `println`, `eprint`, `eprintln`,
   `fprint`, `fprintln` (D12.2).

- A local or parameter may not reuse the name of an enclosing local or parameter.
- A local or parameter may shadow a module-level name, import bindings included, or a universe
  name; the shadowed name is inaccessible in that scope. After `import std::io;` a parameter
  named `io` is legal, and `io` in its scope names the parameter, not the module.
- A module-level declaration or import binding may shadow a universe name: a module declaring
  `fn void print(string s)` loses the builtin `print` throughout its body.
- Enum members are not in the namespace (D3.9): `red` is not a name, `color.red` is.
- Universe functions other than `move` yield no value (D12.2); none can be used as a value,
  imported or qualified.

## 6. Import graph and export rules

The import relation must be acyclic (D9.5). The loader detects a cycle while walking the closure
from the entry module and reports it at the import that closes it. A module importing itself is a
cycle of length one. Whole-program compilation could tolerate cycles; the rule stays because it
keeps dependencies one-directional and module order a topological order.

- Two struct types that refer to each other, through pointers or spans (D3.8), must be declared
  in the same module.
- Two functions in different modules cannot call each other, and a module cannot both provide
  types to another module and call into it. Move one side, or pass a function pointer down from
  the importing module.

Everything at module level is exported (D9.6); `pub` and `priv` are reserved words (D2.4) and
naming conventions for helpers are not enforced. `extern` declarations are per-module: every
module that calls a C function declares it, and the same C symbol may be declared in several
modules provided the signatures are identical (D9.8); differing signatures are an error
(section 13). `std::libc` (D13.2) collects the common libc prototypes so most modules import
them instead.

## 7. Symbol names

| Entity                          | ELF symbol                   | Example             |
|---------------------------------|------------------------------|---------------------|
| function in module `a::b`       | `a.b.name`                   | `std.io.close`      |
| constant or global in `a::b`    | `a.b.NAME`                   | `main.TABLE`        |
| `main` of the entry module      | `<entry>.main`               | `main.main`         |
| program entry, compiler-emitted | `fort_entry`                 | `fort_entry`        |
| runtime                         | `fort_rt_<name>`             | `fort_rt_print_i64` |
| `extern fn`                     | the declared name, unmangled | `write`             |
| struct, enum, import binding    | none                         |                     |

The module path joined with `.`, then `.` and the declaration name, is injective: `.` is legal in
ELF symbols and cannot occur in an identifier, and every segment is an identifier (D9.7). The
entry module is the one whose path need not be (section 2, D9.1), and `.` and `:` are barred from
its base name for exactly this reason: they are the two characters the mangling reads. A
double-underscore scheme is not injective (`a__b` is also one identifier). Fort symbols never
collide with C symbols because C identifiers cannot contain `.`; the only undotted symbols the
compiler emits are `fort_entry` (D11.6), runtime references and `extern` names. `fort_entry` is
reserved for that definition: an `extern` declaring the name is an error (section 13), because
nothing can check a declared signature against a definition the compiler writes itself, and a
mismatch would otherwise be a silent call through the wrong type (D9.7). The standard
library reaches the runtime through ordinary `extern fn fort_rt_...` declarations (D13.1).
In the generated LLVM IR a name is quoted when LLVM's unquoted identifier syntax does not admit
it (`@"std.io.close"`), with a `"`, a `\` or a non-printable byte inside it written `\XX`, which
changes the spelling only: LLVM reads the escape back to the byte, so the ELF symbol is the one in
the table (D9.7, `toolchain.md` 6 item 4).

## 8. C foreign function interface

### 8.1 Declaration and allowed types

```fort
extern fn i64 write(i32 fd, void* buf, u64 n);
extern fn u64 strlen(char* s);
extern fn noreturn exit(i32 status);
extern fn void* own malloc(u64 n);
extern fn void free(void* own p);
```

`extern fn` declares a C function with the System V x86-64 ABI (D9.8; `grammar.md` section 3). It
is top-level only, has no body, and its symbol is the declared name. Parameter names are required
by the grammar and otherwise unused. An `extern` function is called like any function, is usable
as a function-pointer value, and may be `noreturn` (D8.5).

| Allowed in an extern signature                  | Not allowed                          |
|-------------------------------------------------|--------------------------------------|
| `i8 i16 i32 i64 u8 u16 u32 u64`, `f32 f64`      | `T@` spans, `string`, fixed arrays   |
| `bool`, `char`, enums (passed as `i32`)         | structs by value                     |
| `T*`, `T mut*` for any `T`, `void*`             | variadic parameters                  |
| `own` on any of those pointers (D17.13)         | `own` spans and strings (D9.8)       |
| `fn R(P...)` whose signature is extern-legal    |                                      |
| return type `void` or `noreturn`                |                                      |

Structs cross the boundary through pointers only. Because struct layout is C layout (D3.8, D9.9),
a `stat mut* buf` parameter is exactly a C `struct stat *`.

`own` may qualify a pointer or `void*` in an extern signature (D17.13). It is erased, so the
declaration names the same C function with or without it, and it records the C side's
convention on the fort side: `void* own malloc(u64 n)` says the caller must free the result
(`void*` has no target level, so no `mut` after `void`, D3.11), so the cast in
`u8 mut* own p = cast(malloc(n), u8 mut* own);` types the owned block, its target saying `own`
(D3.14), and a plain `u8 mut* p = malloc(n);` is refused as a leaking temporary (D17.8);
`free(void* own p)` says the callee frees, so an `own` lvalue is passed as
`free(cast(move(p), void* own))` and is `null` afterwards (D17.5). A C function that stores or
frees nothing takes plain `T*`. Because `own` is part of type identity (D17.1), two modules that
declare one C symbol with and without it have conflicting declarations (D9.8, section 13):

```fort
extern fn void* own malloc(u64 n);
extern fn void free(void* own p);
extern fn char mut* own strdup(char* s);        // C documents: the caller frees

char mut* own copy = strdup("abc".ptr);         // adopted through the declared own result
char mut* alias = copy;                         // lends (D17.4)
free(cast(move(copy), void* own));              // copy == null afterwards; alias dangles
char mut* leak = strdup("abc".ptr);             // error: owning temporary would leak (D17.8)
free(cast(alias, void*));                       // error: a view cannot pass to an own parameter
```

### 8.2 C type mapping

| C type                         | fort type                                 |
|--------------------------------|-------------------------------------------|
| `char`, `signed char`          | `char` for text, `i8` for numbers         |
| `unsigned char`, `uint8_t`     | `u8`                                      |
| `short`, `unsigned short`      | `i16`, `u16`                              |
| `int`, `unsigned int`          | `i32`, `u32`                              |
| `long`, `long long`, `ssize_t`, `off_t` | `i64`                            |
| `unsigned long`, `size_t`      | `u64`                                     |
| `mode_t`                       | `u32`                                     |
| `_Bool`                        | `bool`                                    |
| `float`, `double`              | `f32`, `f64`                              |
| `char*`, `const char*`         | `char*` (or `u8*` for binary data)        |
| `T*` written to by C           | `T mut*`                                  |
| `const T*`                     | `T*`                                      |
| `void*`, `const void*`         | `void*`                                   |
| `T*` result the caller must free | `T mut* own` (D17.13)                   |
| `void*` from an allocator      | `void* own`, never `mut` (D17.13)         |
| `T*` parameter that C frees    | `T* own` or `void* own` (D17.13)          |
| `R (*)(A, B)`                  | `fn R(A, B)`                              |
| C `enum`                       | `i32`, or a fort enum (passed as `i32`)   |

A C `const` on the pointee becomes the absence of `mut`, and a documented "caller frees" or
"callee frees" becomes `own` on the result or the parameter (D17.13); a pointer that C merely
reads through, keeps without freeing, or that points into something larger stays borrowed.
Nothing finer is expressible, and nothing finer is needed at the boundary.

### 8.3 Narrow values and `bool`

Values narrower than 32 bits are normalized on both sides of the boundary (D9.8): `u8`, `u16`,
`bool` and `char` parameters and results carry `zeroext` and `i8` and `i16` carry `signext` in
the declaration and at the call site (`toolchain.md` 6 item 7), so the caller extends the
argument, the callee extends the result, and upper bits left by C are never observed. A fort
function that C calls back into carries the same attributes on its own parameters. `bool`
crosses as a single 0 or 1.

### 8.4 Variadic C functions

Fort has no variadics (D8.3) and an extern signature cannot declare one. A variadic C function is
declared with a fixed prototype for the arguments actually passed, such as
`extern fn i32 printf(char* fmt, i64 n, f64 x);`. This is safe because System V passes fixed and
variadic arguments identically and tells a variadic callee how many vector registers were used;
the compiler declares and calls every extern function through a variadic LLVM function type, so
that count is always passed (D9.8, `toolchain.md` 6 item 8). Each argument shape needs its own
prototype under its own fort name, since one module cannot declare `printf` twice (D7.9).
Integer promotions are the caller's business: a `char` bound for an `int` slot is widened with
`cast(c, i32)`.

### 8.5 Fort functions as C callbacks

A fort function is a valid C callback exactly when its signature is extern-legal (D9.9). Its
symbol is dotted (`main.by_value`), which C cannot spell, but a callback is passed by value:

```fort
extern fn void qsort(void* base, u64 n, u64 size, fn i32(void*, void*) cmp);

fn i32 by_value(void* a, void* b) {
    i32 x = *cast(a, i32*);
    i32 y = *cast(b, i32*);
    return x < y ? -1 : (x > y ? 1 : 0);
}
```

An `i32 mut@ xs` is sorted with `qsort(cast(xs.ptr, void*), xs.len, sizeof(i32), by_value);`. A
function taking a span, string, struct or fixed array is not extern-legal and cannot be passed
to C.

### 8.6 Spans and strings

Spans and strings never cross the boundary whole (D9.8, D13.4). Pass `.ptr` and `.len`: `s.ptr`
of a `string` is `char*`; `xs.ptr` of a `T@` is `T*`, or `T mut*` for `T mut@`. A string
literal is NUL-terminated (D3.7) and so is every element of `args` (D8.6); a string obtained as a
span or read from a file is not. A C function expecting a terminator gets a copy: allocate
`char mut@ own tmp = new(char, s.len + 1);` under a `defer del(tmp);`, copy the characters, and
pass `tmp.ptr`; the last element is already `'\0'` (D10.2). Memory received from C as `T*`
becomes a span with `p[0..n]` (D6.9), unchecked and borrowed; a `char*` becomes a `string`
with `cast(p[0..n], string)` (D3.14). When C hands the memory over for good, the span is
adopted with a `cast` that adds `own`, `cast(p[0..n], u8 mut@ own)`, and is then freed with
`del` (D17.3); memory from `new` may likewise be freed by C `free` and memory from `malloc` by
`del` (D10.3). There is no strict-aliasing rule (D10.7): memory may be read through any
pointer type reached by `cast`. A fort wrapper around a C function that fills a buffer and
reports its length takes the out-parameter shape `u8 mut@ own mut* out`, a borrowed pointer to
an `own` slot (D3.6, D13.5, D17.2), and stores the adopted span through it, since `.ptr` and
`.len` are never assignable (D6.7); the caller initializes the slot to `{}` so that the store
passes the overwrite check (D17.11).

```fort
extern fn u8 mut* c_read_all(u64 mut* n);       // C documents: the caller frees

fn bool read_all(u8 mut@ own mut* out) {
    u64 mut n = 0;
    u8 mut* p = c_read_all(&n);
    if (p == null) { return false; }
    *out = cast(p[0..n], u8 mut@ own);          // adopt; the caller dels *out
    return true;
}

fn void wrong(u8 mut@ own mut* out) {
    u64 mut n = 0;
    u8 mut* p = c_read_all(&n);
    *out = p[0..n];                             // error: a view cannot be stored in an own slot
}
```

### 8.7 Complete example

```fort
// hello.ft
extern fn i64 write(i32 fd, void* buf, u64 n);
extern fn u64 strlen(char* s);

fn void put(string s) {
    write(1, cast(s.ptr, void*), s.len);
}

fn i32 main(string@ args) {
    string greeting = "hello from fort\n";
    put(greeting);
    put("program: ");
    put(args[0]);
    put("\n");
    u64 n = strlen(greeting.ptr);
    println("strlen: ", n, " len: ", greeting.len);
    return 0;
}
```

`write` returns `i64` (`ssize_t`), discarded by the call statement (D7.3). `s.ptr` is `char*` and
needs `cast` to `void*` (D3.14). `strlen` sees the literal's terminator and returns 16, equal to
`.len`. Direct `write` calls bypass the stdout buffer (D11.5), so mixing them with `print`
reorders output unless the buffer is flushed first.

## 9. Calling convention

The internal convention (D9.9) is System V x86-64 for scalars and one rule for aggregates:

- Integers, `bool`, `char`, enums, pointers and function pointers: `rdi rsi rdx rcx r8 r9`, then
  the stack; returned in `rax`.
- `f32` and `f64`: `xmm0` to `xmm7`, then the stack; returned in `xmm0`.
- Structs, fixed arrays, spans and `string`: passed as a hidden pointer to a caller-made copy,
  occupying the next integer slot; returned into a caller-provided buffer whose address is passed
  in `rdi` ahead of every other argument and echoed in `rax`.

Differences from System V for aggregates:

| Case                         | System V                            | fort v1                  |
|------------------------------|-------------------------------------|--------------------------|
| struct of at most 16 bytes   | split into up to two registers      | pointer to a copy        |
| struct larger than 16 bytes  | copied onto the stack by the caller | pointer to a copy        |
| span or `string`             | two integer registers               | pointer to a copy        |
| aggregate return             | registers or `rdi` result pointer   | always the `rdi` pointer |

In LLVM IR (`toolchain.md` 6 item 7) an aggregate argument is a plain `ptr` parameter, never
`byval`, and an aggregate result is a leading `ptr sret(%T)` parameter on a function returning
`void`; a span or `string` is one pointer and is never split into two scalars. Scalar
parameters and results carry `zeroext` or `signext` when they are narrower than 32 bits (D9.9).

Because of these differences an aggregate never appears in an extern signature (D9.8), and the
compiler never needs System V aggregate classification (D16). Layout is unaffected: structs,
fixed arrays and span headers (`ptr` at offset 0, `len` at offset 8) have C layout, so any
aggregate can be shared with C through a pointer. Callee-saved registers, stack alignment and the
rest of the convention are System V; `toolchain.md` states the code generation contract.

## 10. Compilation model

Fort v1 compiles a whole program at once (D9.10):

1. The entry file is parsed and its imports are resolved (sections 2 and 3).
2. Every imported module is parsed in turn until the import closure is complete; cycles and
   duplicate identities are errors here.
3. Modules are type-checked in dependency order, an imported module before its importers.
4. One LLVM IR module is emitted for the entire closure (D19.1).
5. `--cc` compiles that module and links it with the runtime object in one invocation (D14.3).

`fort --check` runs steps 1 to 3 and stops (D20.1): every module of the closure is checked in
dependency order, as in a build, but nothing is emitted and no `--cc` runs. The file it is given
is the root of the closure rather than the entry point of a program, so it need not define `main`
(D8.6); every other rule of this section holds, the search roots and the cycle rule included. A
module the root does not reach is still never read, so checking a library means checking a file
that imports it.

A module outside the closure is never read, so an error in an unimported standard library module
is never reported. There is no separate compilation: no interface files, no per-module objects, no
module cache, no incremental rebuild, no parallel compilation of modules and no `--module-path`
(D15). A change to any file recompiles the program.

## 11. Entry point and program start

The entry module must define `fn i32 main()` or `fn i32 main(string@ args)` (D8.6). A `main`
returning `void` or taking other parameters is an error. `main` in any other module is an ordinary
function.

Start-up (D11.6): the C runtime owns `main(argc, argv)`. It builds a `string@` of `argc` strings
whose bytes are the `argv` entries, each NUL-terminated, calls the compiler-emitted `fort_entry`
with that span, flushes every output buffer (D11.5) and exits with `status & 0xFF`. `fort_entry`
is generated in the entry module: it receives the span by hidden pointer (section 9) and calls
`<entry>.main`, copying the span into its own frame and passing that copy when `main` declares
the parameter, and taking neither the copy nor an argument when it does not (`toolchain.md` 6
item 22). `args[0]` is the program
name. The runtime keeps the span for the life of the process and exposes it through
`fort_rt_args_ptr()` and `fort_rt_args_len()`, declared in `std::libc` (`stdlib.md` 3) so that
`sys.args()` works in modules whose `main` takes no parameter. `sys.exit` (D13.2) is the other
normal exit; a runtime error exits through `abort()` (D11.4).

## 12. Worked examples

### 12.1 A math module

```fort
// math.ft
f64 PI = 3.141592653589793;

fn i32 add(i32 a, i32 b) {
    return a + b;
}

fn i32 multiply(i32 a, i32 b) {
    return a * b;
}

struct vector {
    f64 x;
    f64 y;
}

fn f64 dot(vector a, vector b) {
    return a.x * b.x + a.y * b.y;
}
```

### 12.2 Using it

```fort
// main.ft
import math;
import math::{add, multiply as mul};

fn i32 main() {
    i32 sum = add(5, 3);
    i32 product = mul(4, 7);
    math.vector v = math.vector{1.0, 2.0};
    math.vector w = {3.0, 4.0};
    println(sum, " ", product, " ", math.dot(v, w));
    return 0;
}
```

Output: `8 28 11.0` (D11.7). `math.vector w = {3.0, 4.0};` is a declaration because `math.vector`
parses as a type followed by an identifier (`grammar.md` section 7). Both files sit in one
directory; `fort main.ft -o main` builds the program.

### 12.3 A list module

```fort
// list.ft
struct node {
    i32 value;
    node mut* own next;
}

struct list {
    node mut* own head;
    u64 size;
}

fn list list_create() {
    return list{};
}

fn void list_push(list mut* l, i32 value) {
    node mut* own n = new(node);
    n->value = value;
    n->next = move(l->head);
    l->head = move(n);
    l->size += 1;
}

fn bool list_pop(list mut* l, i32 mut* out) {
    if (l->head == null) {
        return false;
    }
    node mut* own n = move(l->head);
    *out = n->value;
    l->head = move(n->next);
    l->size -= 1;
    del(n);
    return true;
}

fn void list_free(list mut* l) {
    i32 mut unused = 0;
    while (list_pop(l, &unused)) {
    }
}
```

`node` and `list` refer to each other through pointers and live in one module (section 6).
`node mut* own next` as a field type marks the pointee mutable (D5.5) and the node as owned by
its predecessor (D17.2); `list` is therefore an owning aggregate and is passed as
`list mut* l`, which makes the pointee writable in the callee (D5.3, D17.7). `new(node)`
yields zeroed, owned memory (D10.2, D17.3); `move` transfers the head into the new node's
`next` and the node into `head`, each store landing in a field the preceding `move` emptied, so
the checked build's overwrite check passes (D17.5, D17.11); `del(n)` frees the popped node and
leaves `n` null (D17.9). A user writes `import list;`, `list.list mut l = list.list_create();`,
`list.list_push(&l, 7);` and `defer list.list_free(&l);`; `&l` on a `list.list mut` is a
`list.list mut*` (D5.8); a local named `list` would shadow the binding (section 5), hence `l`.
`list.list copy = l;` is an error, because copying an owning value requires `move` (D17.7).

### 12.4 A multi-module application

```sh
game/
  main.ft
  input.ft
  render.ft
  geom/
    vec.ft
```

```fort
// geom/vec.ft
struct vec2 {
    f64 x;
    f64 y;
}

fn vec2 vec_add(vec2 a, vec2 b) {
    return vec2{a.x + b.x, a.y + b.y};
}
```

```fort
// input.ft
enum key { none, left, right, quit }

i32 mut frame = 0;

fn key poll() {
    frame += 1;
    switch (frame) {
    case 1: return key.left;
    case 2: return key.right;
    default: return key.quit;
    }
}
```

```fort
// render.ft
import geom::vec;
import geom::vec::vec2;

struct window {
    i32 width;
    i32 height;
    vec2 origin;
}

fn void draw(window* win, vec2 pos) {
    vec2 p = vec.vec_add(win->origin, pos);
    println("draw at ", p.x, ",", p.y, " in ", win->width, "x", win->height);
}
```

```fort
// main.ft
import geom::vec::vec2;
import render;
import render::window;
import input as inp;

fn i32 main() {
    window w = {800, 600, {0.0, 0.0}};
    vec2 mut pos = {0.0, 0.0};
    bool mut running = true;
    while (running) {
        switch (inp.poll()) {
        case inp.key.left:
            pos.x -= 1.0;
        case inp.key.right:
            pos.x += 1.0;
        case inp.key.quit:
            running = false;
        case inp.key.none:
        }
        render.draw(&w, pos);
    }
    return 0;
}
```

`fort main.ft -o game` from any directory builds it; `game/` is the root because it contains the
entry file, so `geom::vec` is `game/geom/vec.ft`. Output:

```sh
draw at -1.0,0.0 in 800x600
draw at 0.0,0.0 in 800x600
draw at 0.0,0.0 in 800x600
```

Shown: a nested module path; one module imported both as a binding and by symbol; a type from
another module as a field, a parameter and a nested brace initializer (D6.5); an exhaustive enum
`switch` through a module binding (D7.7); a `mut` global; a terminating `switch` (D8.4).

## 13. Module diagnostics

All diagnostics follow D14.2: `<file>:<line>:<col>: error: <message>`, optionally followed by
`note:` lines. The position is the `import` keyword unless stated; file-level errors use `1:1`.

| Situation                          | Message                                                  |
|------------------------------------|----------------------------------------------------------|
| no file for either reading         | `module 'util::strings' not found`                       |
| symbol reading, name absent        | `module 'util' has no declaration named 'strngs'`        |
| both readings find a file          | `ambiguous import 'a::b::c': a/b/c.ft and a/b.ft exist`  |
| importing an import binding        | `cannot import 'x': it is an import of module 'a::b'`    |
| cycle (at the closing import)      | `circular import: 'main' imports 'util' imports 'main'`  |
| one file, two paths                | `module 'util::x' is the same file as module 'x'`        |
| module file that cannot be read    | `cannot read 'lib/util.ft'`                              |
| module-level name reused           | `redeclaration of 'add'`                                 |
| local reusing an enclosing local   | `'i' shadows an enclosing local` (or `a parameter`)      |
| same extern, different signatures  | `conflicting declarations of extern 'write'`             |
| `extern` declaring `fort_entry`    | `'fort_entry' is reserved: the compiler emits it`        |
| same extern, `own` differs (D17.1) | `conflicting declarations of extern 'free'`              |
| import after a declaration         | `imports must precede declarations`                      |
| module binding as a value or type  | `'io' is a module, not a value` (or `not a type`)        |
| `m.x` with no such declaration     | `module 'std::io' has no declaration named 'x'`          |
| entry module without a valid `main`| `entry module 'main' must define 'fn i32 main()'`        |
| entry base name with `.` or `:`    | `entry file name 'my.app' cannot contain '.'`            |
| aggregate in an extern signature   | `extern signature cannot use type 'i32@'`                |

The missing-`main` row is the one diagnostic a build reports and `fort --check` does not: under
`--check` the root is a module under inspection and D8.6 is not applied (D20.1). Every other row
is reported the same way in both modes, and `--check --json` reports them as the document of
`toolchain.md` 4.1, with the same positions.

Notes accompany some of these: "not found" lists `note: looked for <path>` once per root and
reading; the ambiguous case gives the full paths in the message and the same-file case adds
`note: both name <real path>`; a redeclaration points at the
earlier one with `note: previous declaration of 'add' here`; the missing-`main` message continues
`or 'fn i32 main(string@ args)'`. Tests pin these with `//! error: <substring>` on the offending
line, or `//! error-any:` for the cycle case, where the closing import depends on walk order
(D14.5).

## 14. Not in v1

Deferred by D15, with the v1 idiom: visibility modifiers (idiom: naming conventions, since
everything is exported); separate compilation, interface files and a module cache (idiom:
whole-program builds); conditional compilation (idiom: a C shim behind `extern`). Also absent:
wildcard imports, re-exports, nested module declarations inside a file, importing enum members,
a module-dependency tool and generated module documentation.
