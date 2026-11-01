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
the file path relative to a search root (section 2) with `/` replaced by `.` and `.ft` dropped.
The last segment is the short name, which an import binds by default.

| Module path    | File                     |
|----------------|--------------------------|
| `main`         | `<root>/main.ft`         |
| `std.io`       | `<std>/io.ft`            |
| `std.str`      | `<std>/str.ft`           |
| `util.strings` | `<root>/util/strings.ft` |
| `geom.vec`     | `<root>/geom/vec.ft`     |

- The extension is `.ft` (D1.1). A file with any other extension is never a module.
- Every segment is an identifier (D2.3) that is neither a keyword nor a reserved word (D2.4); a
  file or directory named otherwise is unreachable by any import. This is why the standard
  library's string module is `std.str`, not `std.string` (D9.1).
- A directory is not a module: `util.strings` says nothing about `<root>/util.ft`, which would
  be the unrelated module `util`. Paths are case-sensitive like the file names they map to.

## 2. Search roots and the entry file

The entry file is the one `.ft` file named on the `fort` command line (D14.1). Its module path is
its base name without `.ft`: `main.ft` is the module `main`, `src/app.ft` is the module `app`.
The base name need not satisfy the segment rule of section 1 (D9.1): the entry file is named on the
command line, not reached by an import path, so `007_case.ft` and `my-app.ft` are legal entries
whose modules are `007_case` and `my-app`. The one character it may not contain is `.`, which
would let the entry's symbols collide with another module's (section 7): `my.app.ft` would be the
module `my.app`, whose `main` is the `my.app.main` that `my/app.ft` already emits. Every other
character reaches the symbol unchanged (section 7) and so cannot spell a path, whose every segment
is an identifier, `my:app.ft` emitting the symbol `my:app.main` that nothing else can. A module
whose name is not an identifier cannot be imported, since no import path spells it.

Search roots, in order (D9.2):

1. the directory containing the entry file;
2. each `-I <dir>` directory, in command-line order;
3. the standard library directory (`--std-dir`, else `$FORT_STD_DIR`, else `../std` relative to
   the compiler binary; see `toolchain.md`).

The first segment `std` is reserved for the standard library. A path beginning with `std` is
looked up only in the standard library directory, `std` mapping to that directory itself
(`std.io` is `<std>/io.ft`); a path that does not begin with `std` is never looked up there. A
file `std/x.ft` under any other root is unreachable.

- In a library build, a used library owns each path that starts with its name, and the standard
  library directory is not a root (section 10.2, D9.11).
- The current working directory is never a root. `fort src/main.ft` run from the project
  directory makes `src/` a root, not `.`; a module in `./lib/` needs `-I .` and is then
  `lib.name`.
- Import paths are root-relative, never file-relative: `util/a.ft` imports its sibling as
  `util.b`.
- For one reading of a path (section 3), the first root in order that contains the file wins.
- A module's identity is the real path of its file, after resolving symbolic links and `..`.
  Reaching one file through two module paths, for example through `-I .` combined with the entry
  directory, is an error (section 13).

## 3. Import forms and resolution

Imports appear at the top of a file, before any declaration (D9.3; `grammar.md` section 2). An
`import` after a declaration is a parse error. The order of imports is irrelevant. An import can
also occur in a module-level import selector (D21.3). The parser classifies the complete `$if`
chain after it parses both branches. A chain with at least one recursive import leaf contains only
imports, nested import selectors, or empty branches. It occurs in the import section. A chain with
no import leaf is a declaration selector and occurs in the declaration section. A mixed chain is a
syntax error at its opening `$if`.

| Form                                     | Binds                           | Use               |
|------------------------------------------|---------------------------------|-------------------|
| `import std.io;`                         | `io` to the module `std.io`     | `io.close(fd)`    |
| `import std.io as sysio;`                | `sysio` to the module `std.io`  | `sysio.close(fd)` |
| `import std.str.cmp;`                    | `cmp` to that declaration       | `cmp(a, b)`       |
| `import std.str.cmp as compare;`         | `compare` to that declaration   | `compare(a, b)`   |
| `import std.str.{find, cmp as compare};` | `find` and `compare` separately | `find(s, c)`      |

Resolution of `import a.b.c;` (D9.3). The grammar does not know whether `c` is a module or a
declaration; the loader tries two readings, of which exactly one must succeed:

| Reading | Condition                                        | Result                          |
|---------|--------------------------------------------------|---------------------------------|
| module  | a file `a/b/c.ft` exists under some root         | `c` bound to the module `a.b.c` |
| symbol  | `a/b.ft` exists under some root and declares `c` | `c` bound to that declaration   |

- If both readings succeed the import is ambiguous and an error, whichever roots the two files
  live under.
- If neither succeeds the import is an error: "not found" when no file exists for either
  reading, "has no declaration" when `a/b.ft` exists but lacks `c` (section 13).
- At most one trailing segment names a declaration, so a one-segment path (`import math;`) has
  only the module reading.

The braced form `import a.b.{s1, s2 as t};` is sugar for `import a.b.s1; import a.b.s2 as
t;` with the symbol reading forced: `a.b` must be a module file and every item one of its
declarations. `import std.{io, str};` is an error because `std` is not a module file.

Bindings:

- The bound name, the last segment or the `as` name, enters the module namespace (section 5). A
  name already declared or already bound in the file is a duplicate-binding error.
- Importable declarations are functions, `extern` functions, structs, enums, constants and
  globals. The import bindings of another module are not importable; there is no re-export.
- Enum members are not declarations. `import m.color.red;` fails with "module 'm.color.red'
  not found" because neither `m/color/red.ft` nor `m/color.ft` is a file. Write
  `import m.color;` and use `color.red`.
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
| function value | `m.f`                                      | `fn (i32, i32) i32 op = math.add;` |

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
  name; the shadowed name is inaccessible in that scope. After `import std.io;` a parameter
  named `io` is legal, and `io` in its scope names the parameter, not the module.
- A module-level declaration or import binding may shadow a universe name: a module declaring
  `fn print(string s) void` loses the builtin `print` throughout its body.
- Enum members are not in the namespace (D3.9): `red` is not a name, `color.red` is.
- Universe functions other than `move` yield no value (D12.2); none can be used as a value,
  imported or qualified.

## 6. Import graph and export rules

The import relation must be acyclic (D9.5). The loader detects a cycle while walking the closure
from the entry module and reports it at the import that closes it. A module importing itself is a
cycle of length one. Whole-program compilation could tolerate cycles; the rule stays because it
keeps dependencies one-directional and module order a topological order.

Only a selected import adds an edge to this graph (D21.3). An inactive import causes no file probe,
source read, module identity, binding, duplicate-binding, or cycle operation.

- Two struct types that refer to each other, through pointers or spans (D3.8), must be declared
  in the same module.
- Two functions in different modules cannot call each other, and a module cannot both provide
  types to another module and call into it. Move one side, or pass a function pointer down from
  the importing module.

Everything at module level is exported (D9.6); `pub` and `priv` are reserved words (D2.4) and
naming conventions for helpers are not enforced. `extern` declarations are per-module: every
module that calls a C function declares it, and the same C symbol may be declared in several
modules provided the signatures are identical (D9.8); differing signatures are an error
(section 13). `std.libc` (D13.2) collects the common libc prototypes so most modules import
them instead, and `std.rt` is the runtime, an ordinary fort module the library calls like any
other (`stdlib.md` 3).

## 7. Symbol names

| Entity                          | IR name                      | Example                |
|---------------------------------|------------------------------|------------------------|
| function in module `a.b`        | `a.b.name`                   | `std.io.close`         |
| constant or global in `a.b`     | `a.b.NAME`                   | `main.TABLE`           |
| `main` of the entry module      | `<entry>.main`               | `main.main`            |
| program entry, compiler-emitted | `main`                       | `main`                 |
| runtime function in `std.rt`    | `std.rt.<name>`              | `std.rt.print_i64`     |
| `extern fn`                     | the declared name, unmangled | `write`                |
| struct, enum, import binding    | none                         |                        |

The module path, a `.` and the declaration name is injective: a module path is already
`.`-separated (D9.1), so the mangling copies it across unchanged.
`.` is legal in ELF and Mach-O symbols and
cannot occur in an identifier, and every segment is an identifier (D9.7). Splitting a symbol on
its dots therefore recovers the segments it was built from, the last being the declaration name.
The entry module is the one whose path need not be a segment (section 2, D9.1), and `.` is barred
from its base name for exactly this reason: it is the one character the splitting reads. A
double-underscore scheme is not injective (`a__b` is also one identifier). Fort symbols never
collide with C symbols because C identifiers cannot contain `.`; the only undotted symbols the
compiler emits are the `main` of D11.6 and `extern` names. `main` is reserved for that
definition: an `extern` declaring the name is an error (section 13), because nothing can check a
declared signature against a definition the compiler writes itself, and a mismatch would
otherwise be a silent call through the wrong type (D9.7). The reserved `main` is the C
entry point and not the entry module's `fn i32 main`, whose symbol is `<entry>.main`. The runtime
is fort, so the compiler reaches it by the dotted names of this table (D13.1) and the standard
library reaches it with an `import` like any other module.
In the generated LLVM IR a name is quoted when LLVM's unquoted identifier syntax does not admit
it (`@"std.io.close"`), with a `"`, a `\` or a non-printable byte inside it written `\XX`, which
changes the spelling only: LLVM reads the escape back to the byte.
Mach-O adds a leading `_` to each external object symbol, outside the IR name.
The names in the table stay injective on both targets (D9.7, `toolchain.md` 6 item 4).

## 8. C foreign function interface

### 8.1 Declaration and allowed types

```fort
extern fn write(i32 fd, u8* buf, u64 n) i64;
extern fn strlen(char* s) u64;
extern fn exit(i32 status) noreturn;
extern fn malloc(u64 n) void mut* own;
extern fn free(void* own p) void;
```

`extern fn` declares a C function with the selected target C ABI (D9.8; `grammar.md` section 3). It
is top-level only, has no body, and its symbol is the declared name. Parameter names are required
by the grammar and otherwise unused. An `extern` function is called like any function and may be
`noreturn` (D8.5), but its name is not a value. A fixed extern uses a fixed LLVM call form, and a C
extern with `...` uses a variadic form, on both targets (D9.8). An indirect call cannot take that
form from an unnamed callee (D3.10).
A fort function can wrap a direct C call and act as a function pointer.
Both targets mark extern call sites and extern declarations `nobuiltin`.
The declaration mark keeps a failed allocation observable when the caller reads no storage.

A fixed `fn (P) R` in an extern signature is allowed when its own signature is extern-legal,
result type included, since C calls through it with the same convention (D9.8, D9.9): the rule
reaches a function pointer nested inside another and one in result position.

| Allowed in an extern signature                  | Not allowed                          |
|-------------------------------------------------|--------------------------------------|
| `i8 i16 i32 i64 u8 u16 u32 u64`, `f32 f64`      | `T@` spans, `string`, fixed arrays   |
| `bool`, `char`, enums (passed as `i32`)         | structs by value                     |
| `T*`, `T mut*` for any `T`, `void*`             | variable tails in function pointers |
| `own` on any of those pointers (D17.13)         | `own` spans and strings (D9.8)       |
| fixed `fn (P) R` whose signature is extern-legal |                                      |
| return type `void` or `noreturn`                |                                      |

Structs cross the boundary through pointers only. Because struct layout is C layout (D3.8, D9.9),
a `stat mut* buf` parameter is exactly a C `struct stat *`.

`own` may qualify a pointer or `void*` in an extern signature (D17.13). It is erased, so the
declaration names the same C function with or without it, and it records the C side's
convention on the fort side: `void mut* own malloc(u64 n)` says the caller must free the result
and may write the storage (D3.11), so the cast in
`u8 mut* own p = cast(malloc(n), u8 mut* own);` types the owned block, its target and its
source saying `own` (D3.14), and a plain `u8 mut* p = malloc(n);` is refused as a leaking
temporary (D17.8); `free(void* own p)` says the callee frees, so an `own` lvalue is passed as
`free(cast(move(p), void* own))` and is `null` afterwards (D17.5). A C function that stores or
frees nothing takes plain `T*`. Because `own` is part of type identity (D17.1), two modules that
declare one C symbol with and without it have conflicting declarations (D9.8, section 13):

Two declarations are compared as types and by their variable-tail marks (D9.8).
Parameter names do not affect identity. The check runs in the
checker, over the whole closure in the dependency order of D9.10, where the types exist. So two
spellings of one type agree -- `color` in the module that declares the enum and `shade.color` in
another are one type (D9.4) -- and so do `char` and `u8`, which are one C type at the boundary
(D3.2). A binding-level `mut` is not part of a function type (D3.10) and is not a difference
either. What does differ is what the types differ in: `own` (D17.13), the mutability of any level
below the binding (D3.12), `noreturn` (D8.5), the parameter count, two integer types of one width
that differ in signedness -- `i32` against `u32` is a difference here, where C's `int` and
`unsigned int` are two types and either module may write either name, although the two emit the
same IR -- and two nominal types of one spelling, since a struct or an enum is identified by the
declaration it comes from (D3.8, D3.9).
Two modules that each declare their own `color` and both declare `paint(color c)` therefore
conflict although every enum crosses as `i32`: the program has no one signature for that C
symbol. No spelling makes those two agree, so the note the compiler prints there is to share one
type, or to stop declaring the symbol twice -- one module declares it and exports a fort function
that the others import. That note stands whenever *either* declaration names a struct or an enum and
not only when both do: a module that writes the `i32` an enum crosses as draws it too, importing the
enum being the same fix:

```fort
// shade.ft
enum color { red, green }
extern fn paint(color c) void;                  // the enum's own module spells it `color`
fn paint_red() void { paint(color.red); }       // reachable from elsewhere through this
```

```fort
// main.ft
import shade;
extern fn paint(shade.color c) void;            // the only other spelling there is; one type
```

```fort
extern fn malloc(u64 n) void mut* own;
extern fn free(void* own p) void;
extern fn strdup(char* s) char mut* own;        // C documents: the caller frees

char mut* own copy = strdup("abc".ptr);         // owned through the declared own result
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
| `mode_t`                       | `u32` on Linux; `u16` on Mac              |
| `_Bool`                        | `bool`                                    |
| `float`, `double`              | `f32`, `f64`                              |
| `char*`, `const char*`         | `char*` (or `u8*` for binary data)        |
| `T*` written to by C           | `T mut*`                                  |
| `const T*`                     | `T*`                                      |
| `void*`, `const void*`         | `void*`                                   |
| `T*` result the caller must free | `T mut* own` (D17.13)                   |
| `void*` from an allocator      | `void mut* own` (D3.11, D17.13)           |
| `T*` parameter that C frees    | `T* own` or `void* own` (D17.13)          |
| `R (*)(A, B)`                  | `fn (A, B) R`                              |
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

Fort definitions and function-pointer types have no variable tails (D8.3).
A C extern may mark a final variable tail after at least one fixed parameter:

```fort
extern fn printf(char* fmt, ...) i32;
i32 n = printf("%d\n".ptr, cast(c, i32));
```

The call must supply the fixed prefix. It can then supply zero or more variable arguments.
If it supplies fewer fixed arguments, the diagnostic says
`'printf' takes at least 1 argument, 0 given` for the declaration above.
The compiler compares the fixed prefix and the `...` mark across declarations of one C symbol.
Different calls to one declared C symbol may supply different variable-tail counts and types.
An extern link name or alias remains deferred (D15).

C promotes `float` to `double` and narrow integers to `int` for a variable argument.
Fort never makes an implicit promotion. A caller casts `f32` to `f64` and narrow values to `i32`.
An enum also needs `cast(value, i32)`. Direct variable-tail types may be `i32`, `u32`, `i64`,
`u64`, `f64`, a pointer or a function pointer (D9.8).
An owning pointer lvalue lends in a variable tail. An owning rvalue would leak and is an error.
The compiler checks these types. It does not check a C format string's expected argument types.
Section 13 gives the invalid-tail and owning-rvalue diagnostics.

A fixed extern declaration uses a fixed LLVM call form on both targets.
A variadic call on Linux sets the System V vector-register count in `al`.
Apple arm64 puts variable C arguments on the stack after the fixed prefix.
A declaration of a variadic C function must use `...` on both targets; a fixed form is incorrect.
`toolchain.md` 6 item 8 specifies both LLVM forms.

### 8.5 Fort functions as C callbacks

A fort function is a valid C callback exactly when its signature is extern-legal (D9.9). Its
symbol is dotted (`main.by_value`), which C cannot spell, but a callback is passed by value:

```fort
extern fn qsort(i32 mut* base, u64 n, u64 size, fn (i32*, i32*) i32 cmp) void;

fn by_value(i32* a, i32* b) i32 {
    return *a < *b ? -1 : (*a > *b ? 1 : 0);
}
```

An `i32 mut@ xs` is sorted with `qsort(xs.ptr, xs.len, sizeof(i32), by_value);`. C calls
`by_value` with pointers to two elements of the argument `base` (A7 of `ownership-design.md` 5.1).
A function taking a span, string, struct or fixed array is not extern-legal and cannot be passed
to C. The library declares no `qsort`. One C symbol has one signature in a closure (D9.8), so a
program declares it with the element type that it sorts, and a comparator of `void*` reads no
element, because no cast gives a type to a `void*` (D3.14). `std.sort` sorts typed spans in fort
(`stdlib.md` 2.10).

### 8.6 Spans and strings

Spans and strings never cross the boundary whole (D9.8, D13.4). Pass `.ptr` and `.len`: `s.ptr`
of a `string` is `char*`; `xs.ptr` of a `T@` is `T*`, or `T mut*` for `T mut@`. A string
literal is NUL-terminated (D3.7) and so is every element of `args` (D8.6); a string obtained as a
span or read from a file is not. A C function expecting a terminator gets a copy: allocate
`char mut@ own tmp = new(char, s.len + 1);` under a `defer del(tmp);`, copy the characters, and
pass `tmp.ptr`; the last element is already `'\0'` (D10.2). Memory received from C as `T*`
becomes a span with `p[0..n]` (D6.9), unchecked and borrowed; a `char*` becomes a `string`
with `cast(p[0..n], string)` (D3.14). When C hands the memory over for good, its `extern`
declares the result `own` (D17.13). The pointer is then the owner, `p[0..n]` is a view of it,
and `del(p)` frees it (D17.3); memory from `new` may likewise be freed by C `free` and memory
from `malloc` by `del` (D10.3). A span expression is always a view and a cast never adds `own`
(D3.14), so no cast turns C's block into an `own` span. There is no strict-aliasing rule
(D10.7): memory may be read through any pointer type reached by `cast`. A fort wrapper around a
C function that fills a buffer and reports its length takes the out-parameter shape
`u8 mut@ own mut* out`, a borrowed pointer to an `own` slot (D3.6, D13.5, D17.2). It copies the
bytes into a span from `new`, frees C's block, and moves the span into the slot, since `.ptr`
and `.len` are never assignable (D6.7). The wrapper `del`s the slot first, because the entry
value of `*out` is unknown to it (D17.11); the caller initializes the slot to `{}`, so that the
`del` frees nothing.

```fort
extern fn c_read_all(u64 mut* n) u8 mut* own;   // C documents: the caller frees

fn read_all(u8 mut@ own mut* out) bool {
    u64 mut n = 0;
    u8 mut* own p = c_read_all(&n);
    if (p == null) { return false; }
    u8 mut@ own bytes = new(u8, n);
    mem.copy(bytes, p[0..n]);                   // p[0..n] is a view of C's block
    del(p);                                     // new/del and malloc/free interchange (D10.3)
    del(*out);                                  // the entry value of *out is unknown here
    *out = move(bytes);                         // the caller dels *out later
    return true;
}

fn wrong(u8 mut@ own mut* out) void {
    u64 mut n = 0;
    u8 mut* own p = c_read_all(&n);
    *out = p[0..n];                             // error: a view cannot be stored in an own slot
    *out = cast(p[0..n], u8 mut@ own);          // error: a cast never adds own (D3.14)
    del(p);
}
```

### 8.7 Complete example

```fort
// hello.ft
extern fn write(i32 fd, u8* buf, u64 n) i64;
extern fn strlen(char* s) u64;

fn put(string s) void {
    write(1, cast(s.ptr, u8*), s.len);
}

fn main(string@ args) i32 {
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

The internal convention (D9.9) has target C scalar registers and one fort aggregate rule.
Linux x86-64 uses these scalar registers:

- Integers, `bool`, `char`, enums, pointers and function pointers: `rdi rsi rdx rcx r8 r9`, then
  the stack; returned in `rax`.
- `f32` and `f64`: `xmm0` to `xmm7`, then the stack; returned in `xmm0`.
- Structs, fixed arrays, spans and `string`: passed as a hidden pointer to a caller-made copy,
  occupying the next integer slot; returned into a caller-provided buffer whose address is passed
  in `rdi` ahead of every other argument and echoed in `rax`.

Linux's fort aggregate rule differs from System V:

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

Mac arm64 passes scalar integers and pointers in `x0` to `x7` and scalar floats in `v0` to `v7`.
Scalar results use `x0` or `v0`. A fort aggregate argument uses a pointer in the next `x` register.
A fort aggregate result uses `sret(%T)` in `x8`. The call site must also mark `sret(%T)`.
Mac C variadic tails use stack slots; fixed C extern calls use ordinary scalar registers (8.4).

Because of these differences an aggregate never appears in an extern signature (D9.8), and the
compiler never needs target C aggregate classification (D16). Layout is unaffected: structs,
fixed arrays and span headers (`ptr` at offset 0, `len` at offset 8) have C layout, so any
aggregate can be shared with C through a pointer. Callee-saved registers, stack alignment and the
rest of the convention follow the selected C ABI.
`toolchain.md` states the code generation contract.

## 10. Compilation model

By default, fort v1 compiles a whole program at once (D9.10). A build that names `--lib` or
`--use` is a library build instead (sections 10.1 to 10.4, D9.11). A whole-program build has these
steps:

1. The entry file is parsed. Its import selectors are evaluated before an import path is resolved.
   The parser retains each branch. Only selected imports are resolved (sections 2 and 3; D21.3).
2. Every selected imported module is parsed in turn until the import closure is complete; cycles and
   duplicate identities are errors here. `std.rt` is a root of the closure beside the entry file
   and is loaded whether or not anything imports it (D9.10, D13.1); being in the closure does not
   bind its name, so a module that wants to call it writes `import std.rt;` like any other
   importer.
3. Modules are type-checked in dependency order, an imported module before its importers.
4. One LLVM IR module is emitted for the entire closure (D19.1).
5. `--cc` compiles and links that module in one invocation (D14.3); it holds the whole program,
   the runtime included (D13.1).

`fort --check` runs steps 1 to 3 and stops (D20.1): every module of the closure is checked in
dependency order, as in a build, but nothing is emitted and no `--cc` runs. The file it is given
is the root of the closure rather than the entry point of a program, so it need not define `main`
(D8.6); every other rule of this section holds, the search roots and the cycle rule included. A
module the root does not reach is still never read, except `std.rt`, which is a root of every
closure in both modes (D9.10), so checking a library means checking a file that imports it.

A module outside the closure is never read, so an error in an unimported standard library module
is never reported. A whole-program build has no interface file, no object for each module, no
module cache, no incremental rebuild, no parallel compilation of modules and no `--module-path`.
A change to any file recompiles the program.

### 10.1 Libraries

A library is a named directory tree of modules (D9.11). `fort --lib <name> <root>` builds the
library `<name>` from the directory `<root>` into the artifact `<name>.flib/` (`toolchain.md`
2.1). A later build names the artifact with `--use <name>=<path>`.

- The name is one module-path segment: an identifier that is neither a keyword nor a reserved
  word (section 1, D9.1). The library `std` is the standard library, so its modules are the
  `std` paths of section 2.
- The modules of the library are the regular files with the extension `.ft` below `<root>`, at
  any depth, whose directory names and base name are all segments. The file `<root>/a/b.ft` is
  the module `<name>.a.b`. The compiler ignores every other file and directory, so the cache
  directory `.fort-cache` and the artifact `std.flib` are never modules. It follows no symbolic
  link, to a file or to a directory.
- A root with no module is an error (`toolchain.md` 1).
- Every import spells the module of `<root>/a/b.ft` as `<name>.a.b`, in a module of the library
  and in a module outside it (section 10.2). No short form exists. The user ruled "long spelling
  only" on 2026-10-23. `std` imports its modules this way, as `std.libc`. The compiler, the
  library `fort` with the root `src/fort`, imports its modules as `fort.ast` and
  `fort.owner.prover`, and a whole-program build reaches them through the root `src`.
  `src/lsp` imports its own modules as `lsp.text` and the compiler module as `fort.flt`.
- A library has no module whose path is `<name>` alone, as `std` is no module (section 3).

| File under the root `lib/geom` | Module of the library `geom` |
|--------------------------------|------------------------------|
| `lib/geom/vec.ft`              | `geom.vec`                   |
| `lib/geom/shape/circle.ft`     | `geom.shape.circle`          |
| `lib/geom/007.ft`              | none: `007` is not a segment |
| `lib/geom/my-dir/x.ft`         | none: `my-dir` is not a segment |

The **unit** of a library build is the set of modules that the build compiles to one object.
Under `--lib` the unit is every module of the library. Under `--use` without `--lib`, the unit is
the **entry unit**: the modules of the closure that are in no used library. The entry module is
always in the entry unit. Each unit is compiled once by one invocation of `fort`. A build tool
runs one invocation for each library, dependencies first, and then one for the program. One
invocation does one step and never builds a used library; the user ruled this on 2026-10-23.

### 10.2 Resolution in a library build

A library build changes where a path resolves (D9.2, D9.11). The two readings of section 3 and
their diagnostics do not change.

1. A path whose first segment is the name of a used library resolves in the interface file of
   that library and nowhere else. The module reading succeeds when the interface file has a
   section for the module `a.b.c`. The symbol reading succeeds when it has a section for `a.b`
   that declares `c`. The search roots of section 2 are not searched for the path.
2. Under `--lib <name>`, a path whose first segment is `<name>` resolves only among the modules
   that the walk of section 10.1 lists, by the two readings of section 3 over that list. The
   module reading succeeds when the list holds the module `<name>.a.b`. The symbol reading
   succeeds when the list holds `<name>.a` and that module declares `b`. The compiler opens no
   other file for such a path: a symbolic link, and a file that only a case-insensitive file
   system matches, give no module. A path that neither reading finds is
   `module '<name>.a.b' not found`, with `note: looked for <root>/a/b.ft` (section 13).
3. Under `--lib`, a path whose first segment names neither the library nor a used library is the
   compile error `module 'util.strings' is in no library`, at the import (section 13).
4. Under `--use` without `--lib`, any other path resolves in the entry directory and then in each
   `-I` directory (section 2).
5. A library build has no standard library directory. A `std` path therefore resolves only in a
   library named `std`, by item 1 or item 2. A library build that is neither `--lib std` nor a
   build with `--use std=<path>` is a usage error (`toolchain.md` 1), because every closure holds
   `std.rt` (section 10, D9.10).

The roots of a `--lib` closure are `std.rt` and then each module of the library, in byte order of
the module path. Its dependency order is the post-order of that walk, as section 10 gives it for
one entry. The identity of a module of a used library is the name of the library and the module
path; such a module has no file of its own, so the same-file rule of section 2 does not apply to
it. The import relation stays acyclic (D9.5): the imports of an interface section are edges of
the graph like any other import.

Under `--check`, `--fir` and `-S`, a build with `--use` reads interface files as above. An
interface module is a module of the closure in every pass, and the identifier index (D20.3)
records its declarations at their positions in the interface file.

### 10.3 The interface file

`interface.ft` is fort text that declares the whole library (D9.11). A dependent reads it with
the parser and the checker of every other module. The file is a sequence of lines, and each line
ends with one newline byte. For a library `geom` with one module, `lib/geom/vec.ft`:

```fort
import std.math;

struct vector { f64 x; f64 y; }

i32 DIM = 1 + 1;
vector ORIGIN = {0.0, 0.0};
vector UNIT_X = {1.0, 0.0};
i32 mut calls = 7;

fn add(vector a, vector b) vector {
    calls += 1;
    return vector{a.x + b.x, a.y + b.y};
}

fn largest(vector v) f64 {
    return math.max_f64(v.x, v.y);
}
```

the interface file is:

```fort
//! fort interface 1
//! library geom
//! module geom.vec
import std.math as _0;
i32 DIM = 2;
vector ORIGIN = {};
vector UNIT_X = {.x = 1.0e0, .y = 0.0e0};
fn add(vector a, vector b) vector;
i32 mut calls = 0;
fn largest(vector v) f64;
struct vector { f64 x; f64 y; }
```

**Lines.** Line 1 is `//! fort interface 1`, where 1 is the format version. Line 2 is
`//! library <name>`. Each module then has a section that starts with the line
`//! module <path>`, in byte order of the module path. A section ends before the next `//! module`
line or at the end of the file. The text of a section is one `interface_module` of `grammar.md`
section 2. A position in the file is its line and column in the whole file, so a diagnostic or an
index record at an interface declaration names `<path>/interface.ft` and that position.

**Imports.** A section first holds one line `import <path> as <binding>;`, where `<path>` is the
full module path, such as `geom.shape` for a module of the same library, for each module that
the source module selects with an import (D9.3, D21.3) and for each module that a value of the
section names, in byte order of the module path. A symbol import of the source module gives the
module that declares the symbol. Each module has one line, whatever number of bindings the source
gave it. The binding of the k-th line, k from 0, is the prefix p followed by k in decimal. The
prefix p is the shortest run of `_` such that no declaration name of the module starts with p and
a decimal digit. So the imports of a section give the same closure as the imports of its source.

**Declarations.** After its imports, a section holds each declaration of the module that `$if`
selects (D21.2), one on each line, in byte order of the name. The names of one module are
distinct (section 5), so this order is total. Each declaration has one of these forms:

| Source declaration    | Interface line                                    |
|-----------------------|---------------------------------------------------|
| function with a body  | `fn <name>(<type> <param>, ...) <result>;`        |
| `extern fn`           | `extern fn <name>(<type> <param>, ...) <result>;` |
| struct                | `struct <name> { <type> <field>; ... }`           |
| enum                  | `enum <name> { <member> = <value>, ... }`         |
| constant              | `<type> <name> = <value>;`                        |
| `mut` global          | `<type> <name> = <zero value>;`                   |

- A parameter, a field and an enum member keep the name and the order of the source. An `extern`
  keeps its `...` (D9.8). An enum member has its value in decimal, a negative value with `-`.
- A type keeps its markers and its suffixes (D5.3, D17.2). A struct or an enum of another module
  is `<binding>.<name>`. A struct or an enum of the same module is its name alone.
- The type of a `mut` global holds its `mut` as the source writes it (D7.10). Its value is the
  zero value of its type: `0` for an integer, `0.0e0` for a float, `false`, `'\x00'`, `null` for a
  pointer or a function pointer, and `{}` for an enum, a struct, an array, a span or a `string`.
  No dependent reads the initializer of a `mut` global (D4.6), so a change of it does not change
  the interface hash.
- No `$if`, no `$cfg` and no function body remain.

**Values.** The value of a constant is its folded value (D4.4, D4.6, D7.10), in this form:

- An integer is in decimal, with `-` before a negative value: `-9223372036854775808`.
- A float is the shortest decimal that reads back to its `f64` value (D18.2). An `f32` value is
  first converted to `f64` exactly, so the second rounding of D4.4 changes nothing. The form is
  `<d>.<digits>e<exponent>`: one digit, a `.`, the other digits or `0`, then `e` and the decimal
  exponent with `-` when it is negative. So 0.5 is `5.0e-1` and 100.0 is `1.0e2`. A negative value,
  negative zero included, has `-` before the digits. No constant is infinite or NaN (D4.4).
- A `bool` is `true` or `false`. A `char` is a char literal. A `string` is a string literal, or
  `{}` when its pointer is null. In a literal, the bytes 0x20 to 0x7E stay as they are, except `"`,
  `'` and `\`, which are written `\"`, `\'` and `\\`. Every other byte is `\xHH`, with uppercase hex
  digits (D2.8).
- A pointer is `null` or `&<declaration>`, and a function pointer is `null` or a function name. A
  declaration of another module is `<binding>.<name>`.
- An enum value is `<enum>.<member>` when a member has the value. Else it is
  `cast(<decimal>, <enum>)` (D3.9, D4.6).
- A span is `{}`, because no other span value is constant (D4.6).
- A struct or an array is `{}` when each of its leaves has the zero value above. A negative zero
  float is not the zero value. Else a struct is
  `{.<field> = <value>, ...}` with every field in declaration order, and an array is
  `{<value>, ...}` with every element.

**Spacing.** One space stands between two tokens, with these exceptions. No space stands before
`,`, `;`, `)` or `]`, or before a `*`, `@` or `[` that is a type suffix. No space stands after
`(`, `&`, a unary `-` or the `[` of a suffix, or after the `.` of a designator. No space stands
on either side of the `.` of a qualified name or an enum member, `_0.color.red`. No space stands
between a name or `cast` and the `(` after it, but the `fn` of a function type has a space after
it, `fn (i32, i32) i32`. An initializer brace has no space inside it, `{1, 2}`, and the body of a
struct or an enum has one, `{ f64 x; }`.

**The bodiless declaration.** `fn name(T p) R;` is legal only in an interface file
(`grammar.md` section 3). It declares a fort function of its module with the type
`fn (T) R` (D3.10). The function uses the internal calling convention (D9.9), and its symbol is
`<module>.<name>` (D9.7). The object of the library defines it. A call, a function value and an
import of it are the same as for a function with a body. It has no body, so D8.4 does not apply to
it, `--fir` writes nothing for it, and `--fir-stats` does not count it (D14.1).

**Determinism.** The bytes of the file are a function of the sources of the library, the
interface files that the build reads, the target and the configuration map (D9.11). They do not
depend on the build mode, the paths of the sources or the order in which the walk finds files.
The IR text of a unit is byte-deterministic as well (D19.5). An object and `lib.a` need not be,
because an archiver can write a time stamp.

### 10.4 Linkage between units

Each unit is one LLVM IR module (D19.1, D9.11). It follows the contract of `toolchain.md` 6, with
these additions for the interface modules of its closure:

- The unit defines the functions, constants and globals of its own modules, as a whole-program
  build defines them.
- The named types section holds the named type of each struct of the closure, interface modules
  included, in dependency order and then in declaration order.
- In the globals section, each constant and global of an interface module stands at the place
  of its definition as an external global: `@"m.X" = external dso_local constant <type>, align
  <n>` for a constant and `@"m.g" = external dso_local global <type>, align <n>` for a `mut`
  global. A dependent reads a constant as a whole-program build reads it. It folds the value
  from the interface file where a whole-program build folds the constant, as in a constant
  expression (D4.6). It reads the external global where a whole-program build reads the global:
  for `&`, and for an aggregate constant that it indexes, reads a field of or copies.
- The declarations section starts with one `declare dso_local <result> @"m.f"(<parameters>) #0`
  for each bodiless function of each interface module, in dependency order and then in
  declaration order. The parameters and the result carry the attributes of D9.9 (`sret(%T)`,
  `zeroext`, `signext`) and no names, as the definition in the library carries them. Every
  declaration takes `#0`, the attribute group of a fort definition (`toolchain.md` 6 item 7). The
  definition takes `#0`, or `#1` or `#8` when the function is `noreturn` (`toolchain.md` 6 items
  14 and 20), so the declaration of a `noreturn` function has no `noreturn` attribute. The code
  after a call does not depend on it: a call of a `noreturn` function is followed by the trap of
  item 20, or by `unreachable` in a failure block of item 14. The unit emits `#0` when a
  `declare` uses it. Then the declarations of a whole-program module follow.
- The counters of `@.str.<N>` and `@.file.<N>` start at 0 in each unit. This private data, and
  each enum table `@.enum.<name>`, is `private` (`toolchain.md` 6 item 4), so two units never
  share a name.
- Only the entry unit emits `main` (D9.7, D11.6). Under `--lib`, D8.6 does not apply, and a
  `main` of a library module is an ordinary function.

All symbols stay distinct across units. A used library owns each path that starts with its name
(section 10.2), so the entry unit holds no module of that library, and the symbols of section 7
stay injective.

**Link order.** The link line names the object of the entry unit and then the `lib.a` of each
used library (`toolchain.md` 2.1). A library comes before each library that it uses. Where this
rule leaves a choice, the name that is first in byte order comes first. So the compiler picks the
libraries one at a time, and each pick is the first name in byte order among the remaining
libraries that no remaining library uses. `std`, which each other library uses, comes last.

**Extern declarations across units.** The identity rule of D9.8 holds across units. A build
checks the extern declarations of its closure, as section 13 gives it. It then loads each module
of each used library that is not in its closure, from the interface file, and checks each extern
declaration of those modules against the closure and against each other. These modules add
nothing to the IR module, the FIR or the index. A conflict stands at the later declaration in this
order: the closure in dependency order, then the other modules, library by library in the reverse
of the link order and module by module in byte order of the path. The object of a library holds
all of its modules, so an extern of a module that the program never imports is still linked.

**The runtime.** Every closure holds `std.rt` (D9.10), and the library `std` holds it. In a
library build, the program links `std.rt` from the archive of `std` (D13.1).

**Behaviour.** A program that a library build makes writes the same output and exits with the
same status as its whole-program build. The one exception is a path in a runtime error message
(D11.4), which is the path that the build of the library opened. Clang cannot inline a function
across two units, so the speed can differ.

## 11. Entry point and program start

The entry module must define `fn main() i32` or `fn main(string@ args) i32` (D8.6). A `main`
returning `void` or taking other parameters is an error. `main` in any other module is an ordinary
function.

Start-up (D11.6): the compiler emits `main(argc, argv)` in the entry module (D9.7). That `main`
calls `std.rt.args_init`, which builds a `string@` of `argc` strings whose bytes are the `argv`
entries, each NUL-terminated, then `std.rt.args`, which writes that span into the frame of the
emitted `main`. The emitted `main` then calls `<entry>.main`: with the span by hidden pointer
(section 9) when `main` declares the parameter, and with no argument when it does not. That span is
the caller-made copy, so no second copy exists. It then calls `std.rt.flush_all` (D11.5) and returns
`status & 0xFF` (`toolchain.md` 6 item 22). `args[0]` is the program name. The runtime keeps the
span for the life of the process and hands it out through `std.rt.args()` (`stdlib.md` 3), so that
`sys.args()` works in modules whose `main` takes no parameter. `sys.exit` (D13.2) is the other
normal exit; a runtime error aborts (D11.4).

## 12. Worked examples

### 12.1 A math module

```fort
// math.ft
f64 PI = 3.141592653589793;

fn add(i32 a, i32 b) i32 {
    return a + b;
}

fn multiply(i32 a, i32 b) i32 {
    return a * b;
}

struct vector {
    f64 x;
    f64 y;
}

fn dot(vector a, vector b) f64 {
    return a.x * b.x + a.y * b.y;
}
```

### 12.2 Using it

```fort
// main.ft
import math;
import math.{add, multiply as mul};

fn main() i32 {
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

fn list_create() list {
    return list{};
}

fn list_push(list mut* l, i32 value) void {
    node mut* own n = new(node);
    n->value = value;
    n->next = move(l->head);
    l->head = move(n);
    l->size += 1;
}

fn list_pop(list mut* l, i32 mut* out) bool {
    if (l->head == null) {
        return false;
    }
    node mut* own n = move(l->head);
    l->head = move(n->next);
    *out = n->value;
    l->size -= 1;
    del(n);
    return true;
}

fn list_free(list mut* l) void {
    i32 mut unused = 0;
    while (list_pop(l, &unused)) {
    }
}
```

`node` and `list` refer to each other through pointers and live in one module (section 6).
`node mut* own next` as a field type marks the pointee mutable (D5.5) and the node as owned by its
predecessor (D17.2); `list` is therefore an owning aggregate and is passed as `list mut* l`, which
makes the pointee writable in the callee (D5.3, D17.7). `new(node)` yields zeroed, owned memory
(D10.2, D17.3); `move` transfers the head into the new node's `next` and the node into `head`. The
store into `n->next` lands in a field that `new` zeroed. `n` is fresh, so that store keeps the fact
that `l->head` is empty, and `l->head = move(n)` passes. In `list_pop`, `n` comes from `l->head` and
is not fresh, and a store through `out` can change any field that another pointer reaches, so
`list_pop` stores `*out` after its owning store (D17.5, D17.11). `del(n)` frees the popped node and
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

fn vec_add(vec2 a, vec2 b) vec2 {
    return vec2{a.x + b.x, a.y + b.y};
}
```

```fort
// input.ft
enum key { none, left, right, quit }

i32 mut frame = 0;

fn poll() key {
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
import geom.vec;
import geom.vec.vec2;

struct window {
    i32 width;
    i32 height;
    vec2 origin;
}

fn draw(window* win, vec2 pos) void {
    vec2 p = vec.vec_add(win->origin, pos);
    println("draw at ", p.x, ",", p.y, " in ", win->width, "x", win->height);
}
```

```fort
// main.ft
import geom.vec.vec2;
import render;
import render.window;
import input as inp;

fn main() i32 {
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
entry file, so `geom.vec` is `game/geom/vec.ft`. Output:

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

| Situation                           | Message                                                 |
|-------------------------------------|---------------------------------------------------------|
| no file for either reading          | `module 'util.strings' not found`                       |
| symbol reading, name absent         | `module 'util' has no declaration named 'strngs'`       |
| both readings find a file           | `ambiguous import 'a.b.c': a/b/c.ft and a/b.ft exist`   |
| importing an import binding         | `cannot import 'x': it is an import of module 'a.b'`    |
| cycle (at the closing import)       | `circular import: 'main' imports 'util' imports 'main'` |
| one file, two paths                 | `module 'util.x' is the same file as module 'x'`        |
| module file that cannot be read     | `cannot read 'lib/util.ft'`                             |
| module-level name reused            | `redeclaration of 'add'`                                |
| local reusing an enclosing local    | `'i' shadows an enclosing local` (or `a parameter`)     |
| same extern, different signatures   | `conflicting declarations of extern 'write'`            |
| same extern, variable mark differs   | `conflicting declarations of extern 'printf'`           |
| `extern` declaring `main`           | `'main' is reserved: the compiler emits it`             |
| same extern, `own` differs (D17.1)  | `conflicting declarations of extern 'free'`             |
| import after a declaration          | `an import comes before every declaration`              |
| mixed `$if`                         | see below                                               |
| path separator written `::`         | `a module path is separated by '.', not '::'`           |
| path separator written `..`         | `a module path is separated by '.', not '..'`           |
| module binding as a value or type   | `'io' is a module, not a value` (or `not a type`)       |
| `m.x` with no such declaration      | `module 'std.io' has no declaration named 'x'`          |
| entry module without a valid `main` | `entry module 'main' must define 'fn main() i32'`       |
| entry base name with a `.`          | `entry file name 'my.app' cannot contain '.'`           |
| aggregate in an extern signature    | `extern signature cannot use type 'i32@'`               |
| invalid C variable-tail type        | `C variable tail cannot use type 'f32'`                 |
| owning C variable-tail rvalue       | `owning temporary would leak`                           |
| `--lib` import in no library        | `module 'util.strings' is in no library`                |
| `$cfg` in an interface file (D9.11) | `an interface file holds no '$cfg'`, at the `$cfg`      |

The missing-`main` row is the one diagnostic a build reports and `fort --check` does not: under
`--check` the root is a module under inspection and D8.6 is not applied (D20.1). Every other row
is reported the same way in both modes, and `--check --json` reports them as the document of
`toolchain.md` 4.1, with the same positions.

The mixed `$if` message is `a module $if cannot contain both imports and declarations`. It stands
at the opening `$if` (D21.3).

Against the interface file of a used library, an import of an import binding gives
`module '<m>' has no declaration named '<n>'` and not `cannot import '<n>'`, because section 10.3
binds each import of a section as `<p><k>` and keeps no import name of the source.

Notes accompany some of these: "not found" lists `note: looked for <path>` once per root and
reading, and `note: looked for <module> in library '<name>'` once per reading for a path of a used
library (section 10.2); the ambiguous case gives the full paths in the message and the
same-file case adds `note: both name <real path>`; a redeclaration points at the earlier one
with `note: previous declaration of 'add' here`; the missing-`main` message continues
`or 'fn main(string@ args) i32'`. Every conflicting-extern row names the difference in the same
words: `: the result type differs`, `: the number of parameters differs`, `: parameter N differs` or
`: the variable-tail mark differs`. A conflict between two modules is reported
at the later declaration of the dependency order, on the piece that carries the difference, with
`note: previous declaration of 'write' here` at the earlier one; when either of the two types names
a struct or an enum, a second note says that such a type is its declaration and not its spelling and
gives the two ways out, since no rewording reaches one (section 8.1). Either and not both: an enum
against the `i32` it crosses as draws that note too. The runtime is fort and occupies no C name
(D13.1, D9.7), so no `extern` declaration can conflict with it and there is no rule about one:
`extern fn fort_rt_del(void* p) void;` declares an ordinary C symbol and answers to the rows above
like any other. What the runtime does bring is `std.libc`, which is in every closure behind it
(D9.10), so a program declaring a libc symbol itself is held against `std.libc`'s declaration of it
whether or not it imports the library. The row it draws is the ordinary `conflicting declarations of
extern 'write'`, and `std.libc` is the earlier declaration in it because `std.rt` is loaded as a
root of the closure and so precedes the program in the dependency order this diagnostic reports
against (D9.10): the error therefore lands on the program's line and names the library's, which also
moves it in a program whose two modules disagree, from the first of them to the second. Tests pin
these with `//! error: <substring>` on the offending line, or `//! error-any:` for the cycle case,
where the closing import depends on walk order (D14.5).

## 14. Not in v1

Deferred by D15, with the v1 idiom: visibility modifiers (idiom: naming conventions, since
everything is exported); conditional compilation (idiom: a C shim behind `extern`). Also absent:
wildcard imports, re-exports, nested module declarations inside a file, importing enum members,
a module-dependency tool, a build of a used library by the build that uses it (idiom: one `fort`
invocation for each library, section 10.1) and generated module documentation.
