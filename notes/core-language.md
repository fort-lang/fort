# fort core language

## 1. Introduction and notation

This document is the reference for the core of fort v1: lexical structure, declarations and
mutability, expressions, statements, functions and builtins. `notes/decisions.md` is the decision
log and `notes/grammar.md` is the grammar; both are normative, and where this document disagrees
with either, this document has the bug. Rules cite decisions as `(D5.3)`. Types are summarized
in section 4 and specified in `type-system.md`; memory and runtime checks are in
`memory-model.md`; modules, imports and `extern` are in `module-system.md`. In examples, a
trailing `// error: ...` comment marks a compile error; runtime errors say so explicitly.

## 2. Lexical structure

### 2.1 Source text and comments (D2.1, D2.2)

Source files are UTF-8 with the extension `.ft`; an optional leading byte-order mark is skipped.
Whitespace is space, tab, `\n` and `\r`; it separates tokens and is otherwise ignored. Non-ASCII
bytes are permitted only inside string literals and comments. `//` starts a comment that ends at
the end of the line; `/*` starts a comment that ends at the first `*/`. Block comments do not
nest (`/* a /* b */ c` leaves `c` outside the comment), and an unterminated block comment is an
error.

### 2.2 Identifiers, keywords and reserved words (D2.3, D2.4)

An identifier matches `[A-Za-z_][A-Za-z0-9_]*`, is case-sensitive and has no length limit. `_`
is an ordinary identifier. Keywords and reserved words cannot be identifiers. The keywords are
`as bool break case cast char continue default defer do else enum extern f32 f64 false fn for
i8 i16 i32 i64 if import mut new noreturn null return sizeof string struct switch true u8 u16
u32 u64 void while`. Reserved for future use, usable nowhere: `async await const match pub priv
trait type union yield`. `del`, `assert`, `panic` and the print family are universe-scope
functions (section 8), not keywords.

```fort
i32 const = 1;            // error: 'const' is a reserved word
```

### 2.3 Integer and float literals (D2.5, D2.6, D4.4)

Integer literals are decimal `123`, hex `0x7F` (digits `0`-`9`, `a`-`f`, `A`-`F`), octal `0o17`
and binary `0b1010`; the prefixes are lowercase. `_` may appear between two digits and nowhere
else (`1_000_000`, `0xFF_FF`). A decimal literal other than `0` may not start with `0`; there
is no C-style octal. There are no suffixes. A literal is an untyped constant (5.3) whose value
must lie in `[-2^63, 2^64 - 1]`. Float literals are digits, `.`, digits, with an optional
exponent (`1.0`, `2.5e-3`, `6.02E23`), or digits with an exponent (`1e10`); `_` follows the
integer rule in each digit group. `1.` and `.5` are not literals, and `1..5` lexes as `1`, `..`,
`5`. There are no suffixes: a literal becomes `f32` or `f64` from context (5.3).

```fort
i32 b = 017;              // error: decimal literal may not start with '0'
i32 c = 1__0;             // error: '_' must stand between two digits
i32 d = 10u;              // error: integer literals have no suffix
f64 x = 1.;               // error: '1.' is not a float literal
f32 y = 1.0f;             // error: float literals have no suffix
```

### 2.4 Char and string literals (D2.7, D2.8, D2.9, D3.7)

A char literal is `'x'` where `x` is one printable ASCII character (0x20 to 0x7E) other than `'`
or `\`, or one escape. A string literal is `"..."` holding any bytes except `"`, `\` and a raw
newline, plus escapes. The escapes, valid in both, are `\n` (0x0A), `\t` (0x09), `\r` (0x0D),
`\0` (0x00), `\\`, `\'`, `\"` and `\xHH` with exactly two hex digits; anything else after `\` is
an error. A string literal has type `string` and is stored in read-only memory with a trailing
NUL that `.len` does not count. A raw newline inside a string is an error, adjacent literals do
not concatenate, and there are no raw strings.

```fort
char a = 'é';             // error: non-ASCII byte in char literal; use a string
char b = 'ab';            // error: char literal holds exactly one character
char c = '\q';            // error: unknown escape '\q'
char d = '\x4';           // error: '\x' needs exactly two hex digits
string s = "a" "b";       // error: expected ';' (literals do not concatenate)
```

### 2.5 Operators, punctuation and nesting (D2.10, D2.11)

The tokens are `+ - * / % +% -% *% = += -= *= /= %= +%= -%= *%= &= |= ^= <<= >>= == != < <= > >=
&& || ! & | ^ ~ << >> ++ -- ? : :: . -> .. ( ) [ ] { } , ;`. The lexer takes the longest match
(`+%=` before `+%` before `+`). `%` is never a prefix operator, so `a+%b` is unambiguously
`a +% b`. `<<` and `>>` are single tokens. Nesting of blocks, parentheses, brackets, braces and
type suffixes deeper than 256 is a compile error, so a recursive-descent compiler written in
fort never needs an unbounded stack.

## 3. Declarations and mutability

### 3.1 Variable declarations (D7.1)

`Type name = init;` declares an immutable binding and `mut Type name = init;` a mutable one
(3.3 says what `mut` covers). One declarator per declaration. The initializer is mandatory;
there is no definite-assignment analysis, and `= {}` zero-initializes any aggregate, slice or
string (5.11). A declaration is recognized as a type-looking prefix followed by an identifier
(`grammar.md`, Disambiguation). A name is in scope from the end of its declaration to the end
of the enclosing block.

```fort
i32 x;                    // error: declaration requires an initializer
i32 a = 1, b = 2;         // error: one declarator per declaration
i32 y = y;                // error: unknown name 'y'
```

### 3.2 Storage levels (D5.1, D5.2)

Everything is immutable unless marked `mut`: variables, parameters, the targets of pointers and
the elements of slices (D5.1). To say exactly what a `mut` covers, a declared type is read as a
chain of storage levels (D5.2). Level 0 is the binding's own storage. Each `*` and each `[]`
suffix introduces one more level, the storage reached through that indirection, numbered from
the binding inward: level 1 is reached through the outermost indirection, level 2 through the
next, and so on. By the reading order of D3.6 (section 4), the outermost indirection is a `*`
written after the array group if there is one (`u8[]*` is a pointer to a slice: level 1 holds
the header, level 2 the bytes), otherwise the first `[]` (`Node*[]` is a slice of pointers,
`i32[][]` a slice of slices), otherwise the last `*` (`Node**` is a pointer to a `Node*`). Fixed
arrays and structs add no level: their elements and fields share the storage of the value that
contains them. `string` has a single level, its characters never being mutable, and so does
`void*`. Every level has its own mutability bit; the type of `*p`, `p->f` and `s[i]` is `p`'s
or `s`'s chain from level 1 inward (D5.7).

### 3.3 Placement of `mut` (D5.3)

A `mut` before the base type marks every level mutable, including the binding: read it as
"fully mutable". A `mut` immediately after a `*` or `[]` suffix marks mutable exactly the
storage that holds the pointer or slice header that suffix builds, the level just outside the
one it introduces: read it as "this level only"; after the outermost suffix that storage is the
binding, level 0. `mut` never follows a fixed-array suffix.

| Declaration              | rebind `p = ...` | write through `*p`, `p->f`, `p[i]` |
|--------------------------|------------------|------------------------------------|
| `Node* p`                | no               | no                                 |
| `Node* mut p`            | yes              | no                                 |
| `mut Node* p`            | yes              | yes                                |
| `i32[] s`                | no               | elements: no                       |
| `i32[] mut s`            | yes              | elements: no                       |
| `mut i32[] s`            | yes              | elements: yes                      |
| `Node* mut[] t`          | no               | slots: yes, pointees: no           |
| `Node* mut* pp`          | no               | `*pp`: yes, `**pp`: no             |
| `mut string s`           | yes              | never                              |
| `mut Point q`            | yes (and fields) | not applicable                     |

```fort
mut Node m = {};
Node n = {};
Node* p = &n;
Node* mut q = &n;
mut Node* r = &m;
p = &m;                   // error: cannot assign to immutable 'p'
q = &m;                   // ok
q->value = 1;             // error: cannot write through immutable pointer 'q'
r->value = 1;             // ok; the common cursor 'mut Node* cur' can do both
fn bool read_file(string path, mut u8[]* out) { ... }   // *out and its bytes writable
fn bool peek(string path, u8[] mut* out) { ... }        // *out rebindable, bytes immutable
```

### 3.4 Dropping mutability (D5.4, D3.14)

Dropping mutability is the only implicit conversion. It applies wherever a value meets an
expected type: initialization, assignment, argument passing and `return`. Level 0 of the
receiving binding is unconstrained. For a level `k >= 1`, mutability may be dropped only if every
level from 1 to `k - 1` is immutable in the target type. Adding mutability at any level requires
`cast` (5.9).

```fort
mut Node*[] a = new(Node*[1]);   // slots (level 1) and pointees (level 2) mutable
Node*[] b = a;                   // ok: drops level 1 and level 2
Node* mut[] c = a;               // error: cannot drop level 2 behind mutable level 1
```

The third line is refused because `c[0]` would be a mutable slot holding a `Node*`, so
`Node k = {}; c[0] = &k;` would store the address of the immutable `k`, and `a[0]->value = 1`
would then write to `k` through `a`, which still sees a mutable node. The check is a short
recursion over the two type chains and closes the C hole of converting `T**` to `const T**`.

### 3.5 Struct fields, parameters and return types (D5.5, D5.6)

A field's own storage is exactly as mutable as the struct value that contains it: level 0 of a
field comes from the access path, not from the field type. A leading `mut` in a field type
therefore describes only the levels behind the field's indirections, and a `mut` that would
apply to the field's own slot is an error. A `mut` that would apply to level 0 of a return type
is an error for the same reason. Parameters follow 3.3; at level 0 `mut` makes the callee's
local copy assignable and is not part of the function's type (7.2).

```fort
struct Node {
    i32 value;
    mut Node* next;       // ok: the node reached through 'next' is mutable
    Node* mut prev;       // error: 'mut' on a field's own storage
    mut i32 count;        // error: 'mut' on a field's own storage
}
fn mut i32 f() { return 1; }                // error: 'mut' on a return type without indirection
fn void h(Node* p) { p = p->next; }         // error: cannot assign to immutable parameter 'p'
fn void k(Node* mut p) { p = p->next; }     // ok: level 0 of 'p' is mutable
```

### 3.6 Mutability of lvalues (D5.7, D6.7, D5.8, D5.9)

A variable or parameter is mutable when declared with a level-0 `mut`; `*p` and `p->f` when
level 1 of `p`'s type is `mut`; `e.f` and `e[i]` on a fixed array when `e` is mutable; `s[i]` on
a slice when level 1 of `s`'s type is `mut`; `str[i]` on a string never; `.len` and `.ptr` are
never lvalues. Assignment, compound assignment, `++`, `--` and `&e` producing a `mut T*` require
a mutable lvalue. `&e` has type `T*` whose level 1 is the mutability of `e` and whose deeper
levels come from `e`'s type (D5.8). `new(T)` returns `mut T*` and `new(T[n])` returns
`mut T[]`. The model is shallow (D5.9): immutability of a variable never propagates through a
pointer or slice it contains; the levels behind an indirection are fixed by the type.

```fort
Node n = {};              // 'next' has type mut Node* (struct above)
n.value = 1;              // error: cannot assign to field of immutable 'n'
n.next->value = 1;        // ok: level 1 of 'next' is mutable
mut i32* p = &n.value;    // error: '&n.value' has type i32*, not mut i32*
s[0] = 'x';               // error: string characters are immutable
s.len = 0;                // error: '.len' is not an lvalue
```

### 3.7 Module-level constants and globals (D7.10, D4.6)

At module level, `Type NAME = init;` is a compile-time constant: it lives in read-only memory, is
addressable, and is usable in array lengths and `case` labels. `mut Type g = init;` is a global
in writable memory. Both initializers must be constant expressions (D4.6) extended with `null`,
function names, `&` of a module-level declaration from any module, and struct or array literals
of those. No calls and no reads of `mut` globals are allowed, so there is no initialization
order. Top-level declarations are order-independent within a module: there are no forward
declarations; struct sizes and constant values are resolved lazily with cycle detection. Local
immutable variables are not constants.

```fort
i32 MAX = 64;
i32[MAX] table = {};              // ok: MAX is a constant
mut i32 counter = 0;
i32 START = counter;              // error: initializer reads mutable global 'counter'
i32 SIZE = compute();             // error: initializer calls a function
i32 A = B;                        // error: constant initializer cycle A -> B -> A
i32 B = A;
i32* PTR = &MAX;                  // ok
fn void f() {
    i32 n = 3;
    i32[n] a = {};                // error: array length is not a constant expression
}
```

### 3.8 Scoping and shadowing (D7.9)

One namespace per module holds functions, structs, enums, constants, globals, externs and import
bindings; any collision is an error. Lookup goes from the innermost block outward, then the
module namespace, then the universe (section 8). A local or parameter may not reuse the name of
an enclosing local or parameter. It may shadow a module-level name (including an import binding)
or a universe name, which then becomes inaccessible in that scope; a module-level declaration
may shadow a universe name. Enum members live in their enum (`Color.Red`), not in the module
namespace. Sibling scopes may reuse names.

```fort
import std::io;
struct Point { i32 x; i32 y; }
fn void Point() { }               // error: 'Point' is already declared in this module
fn void f(i32 n) {
    i32 n = 1;                    // error: 'n' shadows a parameter
    { i32 k = 1; }
    { i32 k = 2; }                // ok: sibling scopes
    i32 io = 0;                   // ok: the import binding 'io' is inaccessible below
    io.close(1);                  // error: 'io' is an i32, not a module
    i32 print = 0;                // ok: shadows the universe function in this scope
}
```

## 4. Types, briefly

| Form        | Example             | Size     | Zero value  | `==`     | Decisions   |
|-------------|---------------------|----------|-------------|----------|-------------|
| integers    | `i8 i16 i32 i64`    | 1 2 4 8  | `0`         | yes      | D3.1        |
|             | `u8 u16 u32 u64`    | 1 2 4 8  | `0`         | yes      | D3.1        |
| floats      | `f32 f64`           | 4 8      | `0.0`       | IEEE     | D3.1, D6.12 |
| `bool`      | `true`, `false`     | 1        | `false`     | yes      | D3.3        |
| `char`      | `'a'`               | 1        | `'\0'`      | yes      | D3.2        |
| `void`      | return type, `void*`| -        | -           | -        | D3.1, D3.11 |
| fixed array | `i32[4]`            | N * elem | all zero    | no       | D3.4        |
| slice       | `i32[]`             | 16       | `{null, 0}` | no       | D3.5        |
| `string`    | `"abc"`             | 16       | `""`        | contents | D3.7        |
| struct      | `Point`             | C layout | all zero    | no       | D3.8        |
| enum        | `Color`             | 4        | `0`         | yes      | D3.9        |
| pointer     | `Node*`, `void*`    | 8        | `null`      | identity | D3.11-D3.13 |
| function    | `fn i32(i32, i32)`  | 8        | `null`      | identity | D3.10       |

Alignment equals size for primitives; structs use C/System V layout. `void` is not a value type
(`void v = f();` is an error). Type suffixes (D3.6) come in three groups: `*` suffixes before
the array group apply to the element (`Node*[16]` is sixteen pointers); array and slice suffixes
read outside-in (`i32[3][4]` is indexed `a[i][j]` with `i < 3`, `i32[][4]` is a slice of
`i32[4]`); a `*` after the array group points to the whole array or slice (`u8[]*` is a pointer
to a slice, `i32[4]*` a pointer to an `i32[4]`), and no array suffix may follow it
(`i32[4]*[2]` does not parse; wrap it in a struct). Suffixes after a function type apply to the
function type. Identity, layout, conversions and the full constant rules are in
`type-system.md`.

## 5. Expressions

### 5.1 Precedence (D6.1)

| Level | Operators                                                            | Assoc |
|-------|----------------------------------------------------------------------|-------|
| 1     | primary: `()` `[]` `.` `->` call, slice, `cast`, `sizeof`, `new`, literals | -  |
| 2     | unary `! ~ - * &`                                                    | right |
| 3     | `* / % *%`                                                           | left  |
| 4     | `+ - +% -%`                                                          | left  |
| 5     | `<< >>`                                                              | left  |
| 6     | `< <= > >=`                                                          | left  |
| 7     | `== !=`                                                              | left  |
| 8     | `&`                                                                  | left  |
| 9     | `^`                                                                  | left  |
| 10    | `\|`                                                                 | left  |
| 11    | `&&`                                                                 | left  |
| 12    | `\|\|`                                                               | left  |
| 13    | `?:`                                                                 | right |

Postfix binds tighter than prefix: `*p->f` is `*(p->f)` and `&a[i]` is `&(a[i])`. Assignment is
a statement (6.1). There is no comma operator and no unary `+`. Comparisons do not chain:
`a < b < c` parses but is a type error (`bool < T`).

### 5.2 Operand rules (D6.2, D3.2, D3.3, D3.9, D3.13, D6.12, D6.13, D11.1-D11.3)

| Operators        | Operands                                               | Result       |
|------------------|--------------------------------------------------------|--------------|
| `+ - * /`        | both the same integer type, or the same float type     | operand type |
| `% +% -% *%`     | both the same integer type                             | operand type |
| unary `-`        | a signed integer or a float                            | operand type |
| `& \| ^ ~`       | integers, same type                                    | operand type |
| `<< >>`          | left: any integer; right: any integer or untyped const | left type    |
| `< <= > >=`      | same type: integers, floats or `char`                  | `bool`       |
| `== !=`          | same type, from the equality list below                | `bool`       |
| `! && \|\|`      | `bool`                                                 | `bool`       |

Every mixed-type operation is an error: there is no promotion, not even for `u8` or `i8`. "Same
type" includes the mutability levels of pointer and slice types (D3.12). Equality is defined on
integers, floats, `bool`, `char`, enums, pointers (identity), function pointers (identity) and
`string` (contents: `len` then bytes, so the zero string equals `""`); it is an error on
structs, fixed arrays and slices. `>>` is arithmetic for signed and logical for unsigned types.
`char` supports only comparisons (ordered by unsigned byte value), `switch` and `cast`; `bool`
only `== != ! && ||` and `cast` to integers; enums only `== !=`, `switch` and `cast`, with no
ordering.

Integer overflow (D11.1): in checked builds, the default, `+ - *`, unary `-`, `++ --` and the
compound assignments trap on overflow for signed and unsigned types, and a shift count that is
negative or at least the operand width traps; in release builds (`fort --release`) overflow
wraps in two's complement and the count is taken modulo the width. Programs must not rely on
either behavior. `+% -% *%` wrap in both modes (5.12). Integer division (D6.13, D11.3): `/`
truncates toward zero and `%` takes the sign of the dividend, as on x86-64 and in C; division by
zero, `MIN / -1` and `MIN % -1` are runtime errors in every build mode, checked at every width.
Floats (D6.12) follow IEEE 754: `NaN != NaN`, `-0.0 == 0.0`, division by zero yields an infinity
or NaN and never traps; there are no infinity or NaN literals (`std::math` provides bit casts);
`% ++ -- ~ & | ^ << >>` are errors on floats.

```fort
// a: i32, b: i64, c: u32, d: f64, ch: char, t: bool
i64 s = a + b;            // error: mismatched operand types i32 and i64
u32 n = -c;               // error: unary '-' on unsigned type u32
f64 r = d % 2.0;          // error: '%' on float type f64
char nx = ch + 1;         // error: no arithmetic on char
bool lt = t < false;      // error: no ordering on bool
i32 q = 7 / 0;            // error: constant division by zero
i32 w = a / (a - 1);      // runtime error when a == 1: division by zero
i32 sh = a << b;          // ok: the count may have any integer type; the result is i32
```

### 5.3 Untyped constants in context (D4.1-D4.5)

Integer, float and char literals, and constant expressions built from them, are untyped
constants. An untyped constant takes its type from context: the declared type of the variable
being initialized or assigned, the other operand of a binary operator, the parameter type, the
return type, the `case` operand type, or an index position (any integer type). `cast` is not a
context: an untyped operand of `cast` first takes its default type, then converts (5.9). The
count operand of a shift is not a context for the left operand: in `u64 m = 1 << n;` the untyped
`1` takes `u64` from the declaration whatever the type of `n`. The conversion is checked at
compile time: an untyped integer may become any integer type it fits in or any float type; an
untyped float may become only a float type, even when integral; a char literal defaults to
`char` and may become any integer type; an integer literal never becomes `char`. Arithmetic
among untyped constants folds exactly (integers in `[-2^63, 2^64 - 1]`, floats as `f64`);
integer with integer stays an untyped integer, integer with float becomes an untyped float, and
`~c` is `-c - 1`. With no context at all, an untyped integer becomes `i32` if it fits, else
`i64`, else it is an error; an untyped float becomes `f64`; a char literal becomes `char`
(D4.5). The full rules are in `type-system.md`, Constants.

```fort
u8 b = 256;                       // error: constant 256 does not fit u8
u32 c = -1;                       // error: constant -1 does not fit u32
i32 d = 2.0;                      // error: untyped float in integer context
f64 e = 1 / 2;                    // 0.0: integer folding happens first
u8 g = 'a';                       // 97
char h = 65;                      // error: integer constant in char context; use cast
u32 m = ~0;                       // error: constant -1 does not fit u32 (write 0xFFFFFFFF)
i32 n = 1 << 40;                  // error: constant 1099511627776 does not fit i32
i32 o = cast(0x80000000, i32);    // -2147483648: default type i64, then cast
u64 w = 1 << sh;                  // ok: '1' takes u64 from the declaration, not from 'sh'
print(1 << sh);                   // ok: no context, so '1' is i32 and the result is i32
```

### 5.4 Evaluation order (D6.3)

Operands, call arguments, and struct and array literal fields are evaluated left to right; in a
call the callee expression is evaluated before its arguments. `&&`, `||` and `?:` evaluate only
what they need. For an assignment the target's address, including any index and its bounds
check, is computed before the right-hand side; a compound assignment computes the target once.
Temporaries live until the end of the enclosing statement.

### 5.5 Lvalues, `&`, `*` and `null` (D6.7, D5.8, D3.10, D3.11, D10.4, D10.5)

Lvalues are: variables and parameters; `*p`; `p->f`; `e.f` where `e` is an lvalue; `e[i]` where
`e` is an lvalue fixed array or any slice or string expression; and parenthesized lvalues. `.len`
and `.ptr` are never lvalues. Field access and indexing on an rvalue struct or array yield
rvalues copied through a temporary. `&e` requires an lvalue and yields `T*` with the mutability
of 3.6; `&f` for a function `f` is an error, because a function name is already a value. `*p`
requires a pointer type other than `void*` or a function pointer and yields the pointee. There
is no pointer arithmetic: `p + 1`, `p++` and `p[i]` are errors (D10.4); the only ways to obtain
a pointer are `&`, `new`, `.ptr`, `cast` and extern calls. `null` is the zero pointer and
function-pointer value (D10.5); it takes its type from context and is an error where no pointer
type is expected. `== null` and `!= null` are allowed on pointers, `void*` and function pointers
only; slices and strings compare `.len` or `.ptr`. Dereferencing `null`, and returning the
address of a local or a slice of a local array, are undefined behavior and are not diagnosed.

```fort
i32* a = &(x + 1);        // error: '&' requires an lvalue
i32* b = &Point{1, 2}.x;  // error: '&' requires an lvalue
fn i32(i32) c = &inc;     // error: '&' on a function; write 'inc'
i32 d = *vp;              // error: cannot dereference 'void*'
i32* e = p + 1;           // error: no pointer arithmetic
bool f = s == null;       // error: 'null' compared with a slice; use 's.ptr == null'
print(null);              // error: 'null' needs a pointer-typed context
```

### 5.6 Field access, `->`, `.len` and `.ptr` (D6.10, D3.4, D3.5, D3.7, D9.4)

`e.f` accesses a field of a struct value. `p->f` is `(*p).f` and is required for pointers: `.`
on a pointer is an error with a hint, and `->` on a non-pointer is an error; when `*p` is itself
a pointer, write `(*p)->f`. Qualified names also use `.`: `io.read_file(path)`,
`geom.Point{1, 2}`, `Color.Red`, `m.Color.Red`. Read-only pseudo-fields: a fixed array has
`.len`, an untyped integer constant; a slice has `.len` (`u64`) and `.ptr` (`T*`, or `mut T*`
when level 1 of the slice is mutable); a string has `.len` (`u64`) and `.ptr` (`char*`). Fixed
arrays have no `.ptr`, and `.len` of a slice or string is not a constant expression.

```fort
i32 x = p.value;          // error: '.' on pointer 'p'; use '->'
i32 y = n->value;         // error: '->' on non-pointer 'n'; use '.'
i32* q = arr.ptr;         // error: fixed arrays have no '.ptr'
i32[s.len] b = {};        // error: '.len' of a slice is not a constant expression
i32[arr.len] c = {};      // ok: '.len' of a fixed array is a constant
```

### 5.7 Indexing and slicing (D6.8, D6.9, D10.6, D10.7)

`e[i]`: `e` is a fixed array, slice or string; `i` is any integer type or an untyped constant.
Signed indices are sign-extended and unsigned ones zero-extended, and one unsigned comparison
against the length catches negatives. Every index is bounds-checked in every build mode; the
`--no-bounds-check` option removes the checks for benchmarking and is documented as unsafe
(D10.6). Out of range is a runtime error; a constant index out of range for a fixed array is a
compile error. Indexing a string yields `char`. Pointers cannot be indexed, not even pointers to
arrays: write `(*p)[i]`.

`e[lo..hi]`, `e[lo..]`, `e[..hi]`, `e[..]`: `e` is a fixed array (lvalue only), a slice or a
string; the bounds are any integer type or untyped constants; a missing `lo` is 0 and a missing
`hi` is the length. The result is a slice, or a `string` for a string operand, whose element
mutability is that of `e`'s elements. The runtime check is `0 <= lo <= hi <= len` relative to
the operand, not the original allocation; the result may be empty. `p[lo..hi]` on a `T*` or
`mut T*` yields a `T[]` or `mut T[]` with no check: this is the explicit unsafe escape for
foreign memory, and a range beyond the object is undefined behavior (D10.7). `void*` cannot be
sliced.

```fort
i32[4] a = {1, 2, 3, 4};
i32 x = a[4];             // error: index 4 out of range for i32[4]
i32 y = a[i];             // runtime error when i >= 4
mut i32[] s = a[1..3];    // error: cannot add mutability; 'a' is immutable
i32[] t = a[1..3];        // {2, 3}
i32[] u = t[1..2];        // {3}: bounds are relative to t
i32[] v = t[2..1];        // runtime error: slice bounds 2..1
i32 z = p[0];             // error: pointers cannot be indexed
i32[] f = p[0..n];        // ok: unchecked view of n elements at p
i32[] g = vp[0..n];       // error: 'void*' cannot be sliced
```

### 5.8 Calls (D6.11, D8.2)

Arguments are matched by position and must convert to the parameter types (only the mutability
drop of 3.4 and the untyped-constant conversion of 5.3 apply). There are no defaults, named
arguments, overloading or variadics. A function name, a function-pointer-typed expression and a
qualified name `mod.f` are callable; calling a null function pointer is undefined behavior. A
call statement to a `noreturn` function is a terminating statement (7.3). Arguments and results
are passed by value (7.1).

```fort
add(1);                   // error: 'add' takes 2 arguments, 1 given
add(1, 2.0);              // error: untyped float for parameter 'b' of type i32
i32 t = table[i](3);      // ok: index, then call
```

### 5.9 `cast` (D6.4, D3.14, D4.4, D10.7)

`cast(expr, Type)` is the only explicit conversion; it is a keyword form so the parser never has
to guess whether a parenthesized name is a type, and it never traps. Allowed:

- integer to integer: widening sign- or zero-extends by the source's signedness, narrowing
  truncates, a same-width sign change reinterprets the bits;
- integer to float: round to nearest; float to integer: truncate toward zero, saturate at the
  target's range, NaN becomes 0; float to float;
- `bool` to integer (0 or 1); `char` to and from any integer type; enum to and from any integer
  type (integer to enum is unchecked);
- any pointer to any pointer or `void*`; mutability may be added, which is the cast-away-const
  escape, and writing through it into read-only memory is undefined behavior; pointer to and
  from `u64`; function pointer to and from `void*`;
- among `string`, `char[]`, `u8[]` and their `mut` forms; `T[]` to `mut T[]`; identity.

Forbidden: integer to `bool`; any other slice-to-slice cast (the element type of a slice never
changes, because `len` counts elements); pointer to slice; struct or array casts. An untyped
constant operand first takes its default type (D4.5) and then converts with the semantics above,
so `cast(0x80000000, i32)` is `-2147483648` and `cast(-1, u32)` is `4294967295`. There is no
strict aliasing: reading an object through a pointer of another type, as in `*cast(&x, u64*)`
for an `f64 x`, is defined.

```fort
bool b = cast(1, bool);            // error: cannot cast integer to bool; write '1 != 0'
u32[] u = cast(s, u32[]);          // error: cannot cast i32[] to u32[]
i32[] v = cast(p, i32[]);          // error: cannot cast pointer to slice; use 'p[0..n]'
Point q = cast(r, Point);          // error: cannot cast to struct type Point
mut i32* w = cast(cp, mut i32*);   // ok: adds mutability explicitly
i32 c = cast('a', i32) - '0';      // 49
u8 t = cast(300, u8);              // 44: 300 is i32, then truncated
```

### 5.10 `sizeof` and `new` (D3.15, D10.2, D5.8)

`sizeof(Type)` takes a type only and yields an untyped integer constant: 1 for `bool`, `char`,
`i8` and `u8`; the declared width for the other primitives; 8 for pointers and function
pointers; 16 for slices and `string`; 4 for enums; `N * sizeof(T)` for `T[N]`; the padded size
for structs. `sizeof(void)` and `sizeof(expr)` are errors, and there is no `alignof`.

`new(T)` returns `mut T*` to zero-initialized heap storage. `new(T[n])` returns `mut T[]` of `n`
zero-initialized elements, where `n` is any integer type or an untyped constant; later brackets
are fixed-array dimensions of the element (`new(i32[n][4])` is `mut i32[][4]`). A negative `n`,
a size that overflows, or allocation failure is a runtime error; `n == 0` is allowed and yields
a slice with a non-null pointer. `mut` is never written inside `new(...)`: the result is fully
mutable. `new(T{...})`, `new(T[])` and `new(void)` are errors. Heap storage is freed only by
`del` (8.2).

```fort
u64 a = sizeof(x);                 // error: 'sizeof' takes a type
u64 b = sizeof(void);              // error: 'sizeof(void)'
mut Point* p = new(Point{1, 2});   // error: 'new' takes a type; assign after allocation
mut i32[] s = new(i32[]);          // error: 'new' of a slice needs a count
mut i32[] t = new(i32[n]);         // ok; runtime error if n < 0
mut Node** pp = new(Node* mut);    // error: 'mut' inside 'new'
Point* q = new(Point);             // ok: mut Point* converts to Point*
q->x = 1;                          // error: cannot write through immutable pointer 'q'
```

### 5.11 Struct and array literals (D6.5)

`Point{1, 2}` is positional: every field, in declaration order. `Point{.x = 1, .y = 2}` is
designated: any order, omitted fields zeroed, no mixing with positional, no duplicates;
designators apply to structs only. `Point{}` is all-zero; qualified names work
(`geom.Point{1, 2}`). Typed array literals `i32[3]{1, 2, 3}` have exactly `N` elements or are
`{}`; further dimensions nest braces (`i32[2][2]{{1, 2}, {3, 4}}`). A bare `{...}` is allowed
only as the initializer of a declaration (local, module-level or `for` init) whose type is a
struct or fixed array, and nested inside another literal; `= {}` zero-initializes any aggregate,
slice or string. Bare braces are not expressions: they cannot follow `=` in an assignment or
appear as an argument or `return` operand. Trailing commas are allowed in brace lists and enum
bodies, not in parameter or argument lists. Literals are rvalues; a literal whose leaves are
constant expressions is a constant expression. `IDENT {` is never a block, because every
control-flow condition is parenthesized and every body is braced.

```fort
Point a = {1, 2};                 // ok
Point b = Point{.y = 2};          // ok: x is 0
Point c = Point{1};               // error: positional literal needs every field (2)
Point d = Point{.x = 1, 2};       // error: cannot mix designated and positional
Point e = Point{.x = 1, .x = 2};  // error: duplicate field 'x'
i32[3] f = {1, 2};                // error: array literal for i32[3] needs 3 elements
i32 h = {};                       // error: '{}' initializes aggregates, slices and strings only
i32[] s = {};                     // ok: the zero slice
Node* k = {};                     // error: '{}' does not initialize a pointer; use null
Line l = {{0, 0}, {1, 1}};        // ok: nested bare literals
n = {3, 4};                       // error: bare braces are not an expression; write Point{3, 4}
draw({1, 2});                     // error: bare braces are not an expression
```

### 5.12 Conditional and wrapping operators (D6.6, D11.2, D16)

`c ? a : b` requires a `bool` condition and two operands of one type; an untyped constant
operand adopts the other operand's type. It is right-associative and evaluates only the chosen
operand. `+% -% *%` and `+%= -%= *%=` are integer-only and wrap in two's complement in both
build modes, so hashes, checksums and counters can be written once and behave identically in
checked and release builds; `/`, `%` and the shifts have no wrapping form. Unsigned subtraction
traps in checked mode (`s.len - 1` on an empty slice): test first, or use `-%` when wrapping is
intended.

```fort
i32 y = n ? 1 : 2;                // error: condition must be bool
f64 z = flag ? x : 2.5;           // error: mismatched operand types i32 and f64
mut u32 h = 2166136261;
h = h *% 16777619;                // ok in both modes
u32 t = h *% 1.5;                 // error: wrapping operators are integer-only
```

## 6. Statements

A block is `{ statement* }`; every `if`, loop and `switch` body is a block.

### 6.1 Assignment (D7.2, D5.7, D6.3)

The assignment statements are `lv = e;`, the compound forms `lv op= e;` for `op` among
`+ - * / % +% -% *% & | ^ << >>`, and `lv++;` and `lv--;`. `lv` must be a mutable lvalue (3.6).
In `lv = e;` the value `e` converts to the type of `lv`. `lv op= e` has the operand rules and
overflow behavior of `lv op e` and evaluates `lv` once. `++` and `--` add or subtract 1 with the
checks of `+` and `-` and are allowed on integer types only: they are errors on floats, `char`
and pointers. Assignment is a statement, not an expression: `a = b = c;` and `if (x = 5)` do not
parse. This removes the "assignment where a comparison was meant" bug class, and no rule is
needed for the value of an assignment.

```fort
i32 x = 1;
x = 2;                    // error: cannot assign to immutable 'x'
mut i32 y = 1;
y = y++ + 1;              // error: '++' is a statement, not an expression
if (y = 5) { }            // error: assignment is not an expression
a[i] = f();               // a[i] is addressed and bounds-checked before f() runs
d++;                      // error: '++' on float type f64
```

### 6.2 Expression statements, blocks, `if`, `while`, `do` (D7.3, D7.4, D7.5)

An expression statement is a call whose result, if any, is discarded; any other expression as a
statement is an error, as is the empty statement `;`. A bare block `{ ... }` is a statement and
a scope. `if (cond) { } else if (cond) { } else { }` takes parenthesized `bool` conditions, and
braces are mandatory on every branch, which removes the dangling-`else` and "goto fail" bug
classes. `while (cond) { }` and `do { } while (cond);` take a `bool` condition; in `do`,
variables declared in the body are not visible in the condition.

```fort
compute(x);               // ok: result discarded
a * b;                    // error: expression statement must be a call
Point{1, 2};              // error: expression statement must be a call
;                         // error: empty statement
if (n) { }                // error: condition must be bool; write n != 0
if (ok) return;           // error: expected '{'
do {
    i32 c = next();
} while (c != 0);         // error: unknown name 'c'
```

### 6.3 `for` (D7.5, D16)

`for (init; cond; step) { }`: `init` is one declaration, an assignment, a call, or empty;
`cond` is `bool` or empty, which means `true`; `step` is an assignment, `++`, `--`, a call, or
empty. `for (;;)` is legal. A variable declared in `init` is scoped to the loop and must be
declared `mut` to be stepped, like any other variable; there is no exception for induction
variables. `continue` runs `step`. Slice and string lengths are `u64` and nothing converts
implicitly, so a loop over a slice declares a `u64` counter.

```fort
for (mut i32 i = 0; i < n; i++) { }        // ok
for (i32 i = 0; i < n; i++) { }            // error: cannot modify immutable 'i'
for (mut u64 i = 0; i < s.len; i++) { }    // ok
for (mut i32 i = 0; i < s.len; i++) { }    // error: mismatched types i32 and u64
for (i = 0; i < n; i += 2) { }             // ok when 'i' is an enclosing mutable variable
for (;;) { break; }                        // ok
```

### 6.4 Range `for` (D7.5)

`for (T x : coll) { }` and `for (mut T x : coll) { }`: `coll` is a fixed array, slice or string
expression, evaluated once before the first iteration; a fixed array is evaluated as a value, so
the loop iterates over a copy. `x` is a fresh copy of each element in order, taken at the start
of its iteration. `T` is the element type (`char` for a string); `mut` makes the copy assignable
without affecting the collection. `break` and `continue` work as in other loops.

```fort
i32[3] a = {1, 2, 3};
for (i32 v : a) { print(v); }          // 123
for (char c : "hi") { print(c); }      // hi
for (i64 v : a) { }                    // error: element type is i32, not i64
for (mut i32 v : a) { v = 0; }         // ok: modifies the copy only
for (i32 v : p) { }                    // error: cannot iterate a pointer; slice it first
```

### 6.5 `switch`, `break` and `continue` (D7.6, D7.7, D15, D16)

`switch (e) { case a, b: ... default: ... }`:

- `e` has an integer, `char` or enum type; not `bool`, `string` or a pointer. An untyped
  constant operand takes its default type (D4.5).
- Each label is a constant expression convertible to `e`'s type; duplicate values after
  evaluation are an error. At most one `default`, in any position.
- Each case body is an implicit block scope with an implicit `break` at its end; there is no
  fallthrough. An empty case body does nothing; `case a, b:` shares one body.
- `break` inside a case exits the `switch`. Inside a loop, `break` in a `switch` therefore exits
  the `switch`, not the loop (C semantics). `continue` targets the enclosing loop.
- A `switch` over an enum with no `default` must list every member (D7.7), so that adding a
  member finds every `switch` that needs updating.

`break;` exits the innermost enclosing loop or `switch`; `continue;` continues the innermost
enclosing loop, running `step` in a `for`. Outside a loop or `switch` they are errors. There is
no labeled `break`; use a flag or a helper function.

```fort
enum Color { Red, Green, Blue }
switch (c) {
    case Color.Red:
        handle_red();
}                                   // error: switch over Color does not handle Green, Blue
switch (n) {
    case 1:
        i32 k = 2;                  // scoped to this case
    case 1 + 0:                     // error: duplicate case value 1
    case 'a':                       // ok: 97 (char literal in integer context)
    case k:                         // error: unknown name 'k'
    default:
}
switch (ch) {
    case 65:                        // error: integer constant in char context; write 'A'
}
switch (s) { }                      // error: cannot switch on string
switch (flag) { }                   // error: cannot switch on bool
fn void f() {
    break;                          // error: 'break' outside a loop or switch
}
```

### 6.6 `defer` (D7.8, D11.4, D16)

`defer` is followed by an assignment, a `++`/`--` statement, a call statement, or a block.

- The deferred code runs when the enclosing block is exited by any path: falling off the end,
  `return`, `break` or `continue`. A loop body is a block, so a `defer` in it runs at the end of
  every iteration, before `step`; a `case` body is a block too.
- The set of deferred statements that run at an exit is static: those textually before the exit
  in each exited block, innermost block first, in reverse textual order within a block.
- Nothing is captured at `defer` time; the statement is ordinary code executed at exit, so
  `defer del(p); p = q;` frees `q`.
- `return e` evaluates `e` before deferred code runs, so deferred code cannot change the returned
  value.
- `return`, `break` and `continue` may not appear inside deferred code, and `defer` may not
  appear at module level.
- Runtime errors, including `panic` and a failed `assert`, abort without running deferred code
  (D11.4). A `noreturn` call never exits the block, so nothing deferred runs.

```fort
fn void demo() {
    defer println("A");
    {
        defer println("B");
        println("C");
    }
    for (mut i32 i = 0; i < 2; i++) {
        defer println("D", i);
        if (i == 1) {
            break;
        }
        println("E", i);
    }
    defer println("F");
    println("G");
}
// prints, one per line: C B E0 D0 D1 G F A
```

The inner block exits after `C` and runs `B`. Each loop iteration exits its body block and runs
that iteration's `D`; in the second iteration `break` exits the body before `E1`, and `D1` still
runs because the `defer` is textually before the `break`. Falling off the end of the function
runs the function block's deferred statements in reverse order: `F`, then `A`.

```fort
fn i32 g() {
    mut i32 x = 1;
    defer x = 2;
    return x;                 // returns 1: 'x' is read before the deferred assignment
}
fn void h(mut i32* p) {
    defer return;             // error: 'return' inside deferred code
    defer i32 t = 1;          // error: 'defer' takes an assignment, ++/--, call or block
    defer { *p = 0; }         // ok
}
defer cleanup();              // error: 'defer' at module level
```

### 6.7 `return` (D7.11, D8.4)

`return e;` returns `e` converted to the function's return type; `return;` returns from a `void`
function. `return e;` in a `void` function and `return;` in a non-`void` function are errors. A
non-`void` function must end in a terminating statement (7.3).

```fort
fn void f() { return 1; }     // error: 'return' with a value in a void function
fn i32 g() { return; }        // error: 'return' without a value in a function returning i32
```

## 7. Functions

### 7.1 Declaration, parameters and results (D8.1, D8.2, D8.3, D5.6, D7.10)

```fort
fn i32 add(i32 a, i32 b) {
    return a + b;
}
```

`fn ReturnType name(Type p1, Type p2) { ... }`, with `void` for no result: `fn void main() { }`.
The keyword makes top-level and statement-level parsing unambiguous while the declaration still
reads like C. Parameter names are required, also in `extern` declarations. Functions are
declared at module level only, are visible throughout the module regardless of order, and enter
the module namespace (3.8); recursion is allowed and its depth is bounded only by the OS stack.
There are no nested functions, closures, overloading, default arguments, variadics or methods.
Parameters and results are passed by value: primitives, pointers, slices and strings by copying
the scalar or the fat pointer; structs and fixed arrays by copying the whole value. There are no
reference parameters; pass a `mut T*` (or `mut T[]*`) to let the callee write the caller's
object. A parameter is a local of the callee: `mut` at level 0 makes the copy assignable, and
`mut` at deeper levels follows 3.3. A `mut` at level 0 of a return type is an error (3.5).

```fort
fn i32 f(i32) { return 0; }             // error: parameter needs a name
fn void g() {
    fn void inner() { }                 // error: functions are top-level only
}
fn void bump(i32 n) { n += 1; }         // error: cannot assign to immutable parameter 'n'
fn void bump2(mut i32 n) { n += 1; }    // ok: modifies the local copy
fn i32[4] copy(i32[4] a) { return a; }  // the array is copied in and copied out
```

### 7.2 Function types and values (D3.10, D3.6)

A function type is written `fn R(P1, P2)` with parameter types only. Identity is structural over
the parameter types (including pointee mutability), the return type and `noreturn`; level-0
`mut` on parameters is ignored. A function name, or a qualified name `m.f`, used as a value has
its function type; `&f` and `*f` are errors. `null` is a valid value, and calling it is
undefined behavior. `==` and `!=` compare identity. Suffixes after a function type apply to the
function type: `fn i32(i32)[4]` is an array of four function pointers, `fn i32[4](i32)` returns
an `i32[4]`. At statement level `fn` always begins a declaration whose type is a function type.

```fort
fn i32 inc(i32 x) { return x + 1; }
fn i32 dec(mut i32 x) { return x - 1; }
fn i32(i32) op = inc;             // ok
op = dec;                         // error: cannot assign to immutable 'op'
mut fn i32(i32) op2 = inc;
op2 = dec;                        // ok: level-0 'mut' on dec's parameter is ignored
fn i32(i32)[2] table = {inc, dec};
i32 r = table[1](5);              // 4
fn i32(i32) w = &inc;             // error: '&' on a function; write 'inc'
fn i64(i32) v = inc;              // error: mismatched function types
```

### 7.3 `noreturn` and terminating statements (D8.5, D8.4, D6.11)

`noreturn` is a return type: `fn noreturn fatal(string msg) { ... }`. Such a function may not
contain `return` and must end in a terminating statement; the compiler emits a trap after its
body and after every call to it. `panic` and `sys.exit` are `noreturn`. Without it, every
error-reporting helper would force a dead `return` after each call.

A terminating statement is one of: `return`; a call statement to a `noreturn` function,
including `panic`; an `if` with an `else` whose branches all terminate; `while (true)`,
`for (;;)`, or any `for` with an empty condition, with no `break` targeting it; a `switch` all
of whose cases terminate, when it has a `default` or is an exhaustive enum `switch` (D7.7); a
block whose last statement terminates. A non-`void` function body must end in a terminating
statement, or it is a compile error ("missing return"). The rule is structural, not a data-flow
analysis: a loop that may run zero times does not terminate, however obvious its `return`.
Catching this at compile time is a core "better than C" promise.

```fort
fn i32 sign(i32 x) {
    if (x < 0) {
        return -1;
    } else if (x > 0) {
        return 1;
    }
}                                 // error: missing return (add an else branch)
fn i32 code(Color c) {
    switch (c) {
        case Color.Red:
            return 1;
        case Color.Green, Color.Blue:
            return 2;
    }
}                                 // ok: exhaustive enum switch, every case returns
fn noreturn die(string msg) {
    eprintln(msg);
    return;                       // error: 'return' in a noreturn function
}
fn i32 checked(i32 x) {
    if (x < 0) {
        panic("negative");        // terminating: no dead return needed
    } else {
        return x;
    }
}                                 // ok
```

### 7.4 Entry point (D8.6, D11.6)

The module given to `fort` must define `fn i32 main()` or `fn i32 main(string[] args)`.
`args[0]` is the program name, and every element is NUL-terminated because it comes from `argv`.
The return value is the exit status; the process exits with `status & 0xFF`. A `main` returning
`void` is an error. A `main` in any other module is an ordinary function. The C runtime owns the
process entry: it builds `args`, calls the program, flushes output and exits.

```fort
fn i32 main(string[] args) {
    for (string a : args) {
        println(a);
    }
    return 0;
}
fn void main() { }                // error: 'main' must return i32
```

## 8. Builtins

### 8.1 Keyword forms (D12.1)

`new(T)`, `new(T[n])`, `sizeof(T)` and `cast(e, T)` are keywords that take type operands; they
are specified in 5.9 and 5.10.

### 8.2 Universe functions (D12.2, D10.3, D11.4)

`del(x)`, `assert(cond)`, `panic(msg)`, `print(...)`, `println(...)`, `eprint(...)`,
`eprintln(...)`, `fprint(fd, ...)` and `fprintln(fd, ...)` live in the universe scope, use
ordinary call syntax and have special typing. They may be shadowed by a module-level or local
declaration (3.8), cannot be used as values, and produce no value: they appear only as call
statements, `defer` operands and `for` init or step.

`del(x)` accepts any pointer, `void*` or slice regardless of mutability and frees it. `del(null)`
and `del` of a zero-length slice with a null pointer are no-ops. `del` of a `string` is a compile
error: cast to `u8[]` first, and only if the string came from `new`. `del` of a sub-slice, an
interior pointer, a stack address or a literal is undefined behavior and is not detected. `del`
does not null its argument. Allocations have no header, so `new`/`del` and C `malloc`/`free` are
interchangeable.

`assert(cond)` takes a `bool`; on failure it reports `<file>:<line>:<col>: assertion failed:
<expression text>` and aborts, and it is active in every build mode. `panic(msg)` takes a
`string`, reports `<file>:<line>:<col>: panic: <message>` and aborts; it is `noreturn`, so a
`panic(...)` statement is terminating (7.3). Runtime error contract (D11.4): the runtime flushes
buffered output, writes one line to stderr (`<file>:<line>:<col>: runtime error: <message>` for
the bounds, overflow, shift, division and allocation checks) and calls `abort()`, so the process
dies with SIGABRT, status 134 under a shell. Deferred code does not run.

```fort
del(s);                       // error: cannot del a string; cast to u8[] first
i32 r = del(p);               // error: 'del' has no value
fn void(i32*) f = del;        // error: 'del' cannot be used as a value
assert(n);                    // error: 'assert' requires a bool
panic(1);                     // error: 'panic' requires a string
assert(s.len > 0);            // runtime error when s is empty
```

### 8.3 The print family (D12.2, D11.7, D11.5)

`print` and `println` accept zero or more arguments of integer, float, `bool`, `char`, enum,
pointer, function pointer or `string` type and write them to stdout with no separators;
`println` appends `\n`. `eprint` and `eprintln` do the same to stderr, `fprint` and `fprintln`
to the `i32` descriptor `fd`. Each argument compiles to one per-type runtime call; an untyped
constant argument takes its default type (D4.5). Structs, arrays and slices are not printable.

| Type      | Text                                                            |
|-----------|-----------------------------------------------------------------|
| integers  | decimal; `u8` is a number                                       |
| `bool`    | `true` or `false`                                               |
| `char`    | its byte                                                        |
| enum      | the member name, or the number if no member matches             |
| pointers  | `0x` plus lowercase hex, `0x0` for null (also `void*`, functions)|
| `string`  | its bytes                                                       |
| floats    | shortest round-trip decimal, `%g`-style (see below)             |

Floats use exponent form below 1e-4 or at 1e17 and above, get `.0` appended when the text has
neither `.` nor `e`, and print as `inf`, `-inf` or `nan`. Output buffering (D11.5): `print` and
`println` write to a runtime buffer for stdout; `eprint` and `eprintln` are unbuffered; `fprint`
and `fprintln` use one buffer per descriptor. Buffers flush when full, on `io.close`, at exit,
and before any runtime error.

```fort
println("x = ", 42, ", ok = ", true);       // x = 42, ok = true
print('a');                                 // a
print(cast('a', u8));                       // 97
println(1.0, " ", 0.5, " ", 1e20);          // 1.0 0.5 1e+20
println(Color.Green, " ", cast(7, Color));  // Green 7
void* v = null;
println(v);                                 // 0x0
println(pt);                                // error: cannot print a value of struct type Point
```

## 9. Not in v1

A number of familiar features are deliberately absent from v1: generics, unions, methods,
closures, variadics, overloading, visibility modifiers, type aliases, labeled `break`, string
`switch` and others. D15 in `decisions.md` is the authoritative list; it names each feature with
the idiom to use instead, and this document does not repeat it.
