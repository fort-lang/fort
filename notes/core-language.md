# fort core language

## 1. Introduction and notation

This document is the reference for the core of fort v1: lexical structure, declarations and
mutability, expressions, statements, functions and builtins. `notes/decisions.md` is the decision
log and `notes/grammar.md` is the grammar; both are normative, and where this document disagrees
with either, this document has the bug. Rules cite decisions as `(D5.3)`. Types are summarized
in section 4 and specified in `type-system.md`; ownership (`own`, `move`) is introduced in 3.9
and its memory rules are in `memory-model.md`, as are the runtime checks; modules, imports and
`extern` are in `module-system.md`. In examples, a
trailing `// error: ...` comment marks a compile error; runtime errors say so explicitly.

## 2. Lexical structure

### 2.1 Source text and comments (D2.1, D2.2)

Source files are UTF-8 with the extension `.ft`; an optional leading byte-order mark is skipped.
Whitespace is space, tab, `\n` and `\r`; it separates tokens and is otherwise ignored. Non-ASCII
bytes are permitted only inside string literals and comments. `//` starts a comment that ends at
the end of the line, and it is the only comment form: fort has no block comments, so a region is
commented out one line at a time. The adjacent pair `/*` is a lexical error rather than a
division by a dereference, which catches the habit brought from C; separate the operators to
write the division.

```fort
i32 a = x /* stale */ + 1;    // error: block comments are not supported, use '//'
i32 b = x / *p;               // ok: divide by the pointee
```

### 2.2 Identifiers, keywords and reserved words (D2.3, D2.4)

An identifier matches `[A-Za-z_][A-Za-z0-9_]*`, is case-sensitive and has no length limit. `_`
is an ordinary identifier. Keywords and reserved words cannot be identifiers. The keywords are
`as bool break case cast char continue default defer do else enum extern f32 f64 false fn for
i8 i16 i32 i64 if import mut new noreturn null own return sizeof string struct switch true u8
u16 u32 u64 void while`. Reserved for future use, usable nowhere: `async await const match pub
priv trait type union yield`. `del`, `move`, `assert`, `panic` and the print family are
universe-scope functions (section 8), not keywords.

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
there is no definite-assignment analysis, and `= {}` zero-initializes any aggregate, slice,
string or enum (5.11). A declaration is recognized as a type-looking prefix followed by an
identifier (`grammar.md`, Disambiguation). A name is in scope from the end of its declaration
to the end of the enclosing block.

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
the header, level 2 the bytes), otherwise the first `[]` (`node*[]` is a slice of pointers,
`i32[][]` a slice of slices), otherwise the last `*` (`node**` is a pointer to a `node*`). Fixed
arrays and structs add no level: their elements and fields share the storage of the value that
contains them. `string` has a single level, its characters never being mutable, and so does
`void*`. Every level has its own mutability bit; the type of `*p`, `p->f` and `s[i]` is `p`'s
or `s`'s chain from level 1 inward (D5.7). Each `*` and `[]` also introduces a *reference*, the
pointer or slice header stored at the level just outside the one the suffix reaches; the
outermost reference is stored in the binding. References, not levels, carry the `own` mark of
3.9.

### 3.3 Placement of `mut` (D5.3)

A `mut` before the base type marks every level mutable, including the binding: read it as
"fully mutable". A `mut` immediately after a `*` or `[]` suffix marks mutable exactly the
storage that holds the pointer or slice header that suffix builds, the level just outside the
one it introduces: read it as "this level only"; after the outermost suffix that storage is the
binding, level 0. A `mut` that marks a level twice (`mut i32* mut p`) is an error ("redundant
mut"), so every type has one spelling. `mut` never follows a fixed-array suffix; inside a
fixed-array type a `mut` after `*` marks the array's slots, which are level 0 of the array value
(`node* mut[4] t` has assignable slots and immutable nodes, and `t[..]` is `node* mut[]`).

| Declaration              | rebind `p = ...` | write through `*p`, `p->f`, `p[i]` |
|--------------------------|------------------|------------------------------------|
| `node* p`                | no               | no                                 |
| `node* mut p`            | yes              | no                                 |
| `mut node* p`            | yes              | yes                                |
| `i32[] s`                | no               | elements: no                       |
| `i32[] mut s`            | yes              | elements: no                       |
| `mut i32[] s`            | yes              | elements: yes                      |
| `node* mut[] t`          | no               | slots: yes, pointees: no           |
| `node* mut* pp`          | no               | `*pp`: yes, `**pp`: no             |
| `mut u8[]* out`          | yes              | `*out`: yes, bytes: yes            |
| `u8[] mut* out`          | no               | `*out`: yes, bytes: no             |
| `mut string s`           | yes              | never                              |
| `mut point q`            | yes (and fields) | not applicable                     |

```fort
mut node m = {};
node n = {};
node* p = &n;
node* mut q = &n;
mut node* r = &m;
p = &m;                   // error: cannot assign to immutable 'p'
q = &m;                   // ok
q->value = 1;             // error: cannot write through immutable pointer 'q'
r->value = 1;             // ok; the common cursor 'mut node* cur' can do both
fn bool read_file(string path, mut u8[] own* out) { ... }  // the own slot *out and its bytes
fn bool peek(string path, u8[] mut* out) { ... }        // *out rebindable, bytes immutable
```

### 3.4 Dropping mutability (D5.4, D3.14)

Dropping mutability is one of the two implicit conversions, the other being the drop of `own`
in 3.9, and the two combine. It applies wherever a value meets an expected type: initialization,
assignment, argument passing and `return`. Level 0 of the receiving binding is unconstrained.
For a level `k >= 1`, mutability may be dropped only if every level from 1 to `k - 1` is
immutable in the target type. Adding mutability at any level requires `cast` (5.9).

```fort
own mut node*[] a = new(node*[1]);   // slots (level 1) and pointees (level 2) mutable
node*[] b = a;                       // ok: lends 'a', dropping own, level 1 and level 2
node* mut[] c = a;                   // error: cannot drop level 2 behind mutable level 1
```

The third line is refused because `c[0]` would be a mutable slot holding a `node*`, so
`node k = {}; c[0] = &k;` would store the address of the immutable `k`, and `a[0]->value = 1`
would then write to `k` through `a`, which still sees a mutable node. The check is a short
recursion over the two type chains and closes the C hole of converting `T**` to `const T**`.

### 3.5 Struct fields, parameters and return types (D5.5, D5.6)

A field's own storage is exactly as mutable as the struct value that contains it: level 0 of a
field comes from the access path, not from the field type. A leading `mut` in a field type
therefore describes only the levels behind the field's indirections, and a `mut` that would
apply to the field's own slot is an error. A `mut` that would apply to level 0 of a return type
is an error for the same reason. Parameters follow 3.3; at level 0 `mut` makes the callee's
local copy assignable and is not part of the function's type (7.2). A field or a return type may
be an `own` reference (`own mut u8[] data;`, `fn own mut node* alloc()`); a struct with such a
field is an owning aggregate (3.9).

```fort
struct node {
    i32 value;
    mut node* next;       // ok: the node reached through 'next' is mutable
    node* mut prev;       // error: 'mut' on a field's own storage
    mut i32 count;        // error: 'mut' on a field's own storage
}
fn mut i32 f() { return 1; }                // error: 'mut' on a return type without indirection
fn void h(node* p) { p = p->next; }         // error: cannot assign to immutable parameter 'p'
fn void k(node* mut p) { p = p->next; }     // ok: level 0 of 'p' is mutable
```

### 3.6 Mutability of lvalues (D5.7, D6.7, D5.8, D5.9)

A variable or parameter is mutable when declared with a level-0 `mut`; `*p` and `p->f` when
level 1 of `p`'s type is `mut`; `e.f` and `e[i]` on a fixed array when `e` is mutable; `s[i]` on
a slice when level 1 of `s`'s type is `mut`; `str[i]` on a string never; `.len` and `.ptr` are
never lvalues. Assignment, compound assignment, `++`, `--` and `&e` producing a `mut T*` require
a mutable lvalue; `move` and `del` do not (3.9). `&e` has type `T*` whose level 1 is the
mutability of `e` and whose deeper levels come from `e`'s type (D5.8); the pointer it yields is
never `own` (D17.3). `new(T)` returns `own mut T*` and `new(T[n])` returns `own mut T[]`
(D17.3). The model is shallow (D5.9): immutability of a variable never propagates through a
pointer or slice it contains; the levels behind an indirection are fixed by the type.

```fort
node n = {};              // 'next' has type mut node* (struct above)
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
immutable variables are not constants. A constant lives in read-only memory, so `move` and `del`
of one are errors; an owning global, whose level 0 is `mut`, is emptied like a local (3.9).

```fort
i32 MAX = 64;
i32[MAX] table = {};              // ok: MAX is a constant
mut i32 counter = 0;
i32 START = counter;              // error: initializer reads mutable global 'counter'
i32 SIZE = compute();             // error: initializer calls a function
i32 A = B;                        // error: constant initializer cycle A -> B -> A
i32 B = A;
i32* PTR = &MAX;                  // ok
own node* ROOT = null;            // a constant: only the zero value is possible
own mut node* head = null;        // a global
fn void f() {
    i32 n = 3;
    i32[n] a = {};                // error: array length is not a constant expression
    del(ROOT);                    // error: cannot empty module-level constant 'ROOT'
    del(head);                    // ok
}
```

### 3.8 Scoping and shadowing (D7.9)

One namespace per module holds functions, structs, enums, constants, globals, externs and import
bindings; any collision is an error. Lookup goes from the innermost block outward, then the
module namespace, then the universe (section 8). A local or parameter may not reuse the name of
an enclosing local or parameter. It may shadow a module-level name (including an import binding)
or a universe name, which then becomes inaccessible in that scope; a module-level declaration
may shadow a universe name. Enum members live in their enum (`color.red`), not in the module
namespace. Sibling scopes may reuse names.

```fort
import std::io;
struct point { i32 x; i32 y; }
fn void point() { }               // error: 'point' is already declared in this module
fn void f(i32 n) {
    i32 n = 1;                    // error: 'n' shadows a parameter
    { i32 k = 1; }
    { i32 k = 2; }                // ok: sibling scopes
    i32 io = 0;                   // ok: the import binding 'io' is inaccessible below
    io.close(1);                  // error: 'io' is an i32, not a module
    i32 print = 0;                // ok: shadows the universe function in this scope
}
```

### 3.9 Ownership (D17.1-D17.8, D17.11, D17.14)

A pointer, `void*`, slice or `string` type may be qualified `own`. An `own` reference designates
the start of a live heap allocation, obtained from `new` (5.10) or adopted with `cast` (5.9),
that `del` (8.2) may free. `own` is part of the type, so `own node*` and `node*` are different
types (`type-system.md` section 8), and it is erased at run time. `own` on a non-reference type
or on a function-pointer type is an error; a struct or fixed array that contains an `own`
reference by value is an owning aggregate (below). Ownership is a typing discipline, not a
linear check: whether every allocation is freed exactly once is not tracked (D17.14, D15), and
the idiom is `defer del(x);` (6.6).

**Placement (D17.2).** `own` before the base type, and before any `mut` there, marks the
outermost reference of the type, the one the binding holds. `own` immediately after a `*` or
`[]` suffix, before any `mut` in that position, marks the reference that suffix introduces.
Unlike prefix `mut`, prefix `own` marks one reference only: in `own mut node*[] items` the slice
is owned and the pointers in it are borrowed, so `del(items[i])` does not compile, which is the
safe failure when the nodes belong to an arena. Marking one reference twice is an error
("redundant own"), and so is an `own` after the outermost suffix, whose reference the prefix
position already names (write `own node* p`, not `node* own p`), so every type has one spelling.
`own` never follows a fixed-array suffix. The mutability of each level is spelled as in 3.3;
`own` does not change it, and `del` or `move` through an indirection needs that level mutable.

| Declaration                 | `del(x)` | `del(x[i])`, `del(*x)` | Reads as                       |
|-----------------------------|----------|------------------------|--------------------------------|
| `own mut u8[] buf`          | yes      | no: not references     | owned, writable bytes          |
| `own u8[] data`             | yes      | no                     | owned bytes, read-only here    |
| `own mut node* n`           | yes      | no                     | owned node                     |
| `own node* mut n`           | yes      | no                     | read-only owned node, rebinds  |
| `own mut node*[] items`     | yes      | no: borrowed pointers  | owned slice of borrowed nodes  |
| `own mut node* own[] kids`  | yes      | yes                    | owned slice of owned nodes     |
| `node* own[] view`          | no       | no: slots immutable    | borrowed view of owned nodes   |
| `mut node* own[] v`         | no       | yes                    | mutable view of owned nodes    |
| `mut u8[] own* out`         | no       | `del(*out)`: yes       | pointer to an owned slot       |
| `own string name`           | yes      | no                     | owned characters               |

**Lending (D17.4).** `own X` converts implicitly to `X` wherever a value meets an expected type,
the same places as the drop of 3.4, and the two drops combine (`own mut u8[]` to `u8[]`). The
operands of `==`, `!=` and `?:` are lent as well, so `own` never blocks a comparison (5.2, 5.12).
The drop is monotone with the shape of 3.4: `own` may be dropped from a reference only if, in
the target type, no reference outside it is `own` and every level between the binding and the
storage holding that reference is immutable. `own mut node* own[]` converts to
`mut node* own[]` (a view of the owned slots) and to `node*[]`, but not to `own mut node*[]`,
whose nodes would belong to nobody once the slice is freed, and not to `mut node*[]`, through
which a borrowed pointer could be stored into a slot the source still sees as owned. Adding
`own` requires `cast`. Lending never empties the source, and a lent value must not outlive the
allocation (D17.14).

**Transfer (D17.5, D17.6).** Copying an `own` lvalue into an `own` place, that is a declaration's
initializer, an assignment target, an `own` parameter, an `own` field or element of a literal,
or a `return` operand that is not a bare local, requires `move(lv)`. `move` is a universe
function (8.2): it yields the operand's value and leaves the zero value (`null`, `{null, 0}`)
behind. An `own` rvalue, which is `new(...)`, a call result, `move(...)` or a `cast` to an `own`
type, flows into an `own` place directly. Emptying is not an assignment: the operand need not be
`mut`, and an immutable binding is still not assignable after the move. When the operand is
reached through an indirection (`*p`, `p->f`, `s[i]`), the level holding it must be mutable,
because the move changes storage that others can see; the fields and elements of a local value
count as the local. Moving a zero value yields a zero value. `del` empties its operand the same
way (8.2), and storing over a live `own` value is a runtime error in checked builds (6.1).

**Temporaries must land (D17.8).** An `own` rvalue may only be bound to an `own` place, passed
to an `own` parameter, or `del`ed. Anything else is an error ("owning temporary would leak"),
because nothing could ever `del` it: converting or casting it to a non-`own` type
(`mut node* n = new(node);`, `println(str.dup(x))`, `cast(new(node), node*)`), slicing it or
taking its `.ptr` (`new(u8[8])[..4]`, 5.6, 5.7), accessing a field of an owning aggregate rvalue
(`make_vec().data`), and discarding it as an expression statement (`move(x);`, `str.dup(s);`,
6.2).

**Returning and `defer` (D17.5, D7.8).** `return x` where `x` is a local variable or parameter
of `own` type is an implicit `move(x)`, performed before deferred code runs (6.6, 6.7). A
`defer del(x);` written right after the allocation therefore frees `x` on every path except the
one that hands it to the caller.

```fort
struct node { i32 value; mut node* next; }
fn void take(own mut node* n) { del(n); }     // takes ownership
fn void look(node* n) { println(n->value); }  // borrows
fn own mut node* build() {                    // fn bool fill(mut node* n) borrows too
    own mut node* n = new(node);
    defer del(n);                 // frees n on every path but the one that returns it
    if (!fill(n)) {
        return null;              // the deferred del(n) frees the node
    }
    return n;                     // implicit move: n is null when del(n) runs
}
fn void demo() {
    own mut node* a = build();    // ok: an own rvalue lands in an own place
    own mut node* b = a;          // error: copying own lvalue 'a' needs move(a)
    own mut node* c = move(a);    // ok: 'a' is now null
    node* v = c;                  // ok: lends; 'c' still owns the node
    look(c);                      // ok: lends
    take(c);                      // error: 'take' takes ownership; write move(c)
    take(move(c));                // ok: 'c' is null afterwards
    take(build());                // ok: the temporary lands in the own parameter
    mut node* d = build();        // error: owning temporary would leak; declare own mut node*
    look(build());                // error: owning temporary would leak
    own node* own e = null;       // error: redundant own
    node* own f = null;           // error: 'own' after the outermost suffix; write own node*
    own i32 g = 1;                // error: 'own' on a non-reference type
    own point h = {};             // error: 'own' on a struct; qualify a field instead
    own fn void(node*) i = look;  // error: 'own' on a function-pointer type
}
```

**Owning aggregates (D17.7).** A struct or fixed array that contains an `own` reference by
value, directly or through nested aggregates, is owning (`type-system.md` section 8). Copying
an owning lvalue into an owning place (initialization, assignment, a by-value parameter, a
literal element) requires `move`, which empties the whole value; returning a local owning value
is an implicit move; `del` of an aggregate is an error, because `del` is shallow. Functions
therefore take `vec*` or `mut vec*`, and a struct frees its own fields.

```fort
struct vec { own mut i32[] data; u64 len; }
fn void steal(node* own[] view, mut node* own[] slots, vec* v, mut vec* w) {
    own node* a = move(view[0]);         // error: cannot move out of immutable slot 'view[0]'
    own mut node* b = move(slots[0]);    // ok: level 1 of 'slots' is mutable
    own mut i32[] c = move(v->data);     // error: cannot move through immutable pointer 'v'
    own mut i32[] d = move(w->data);     // ok
    vec local = {};
    own mut i32[] e = move(local.data);  // ok: a field of a local counts as the local
    vec copy = local;                    // error: copying owning value 'local' needs move(local)
    vec taken = move(local);             // ok: 'local' is now all zero
    del(taken);                          // error: 'del' takes a reference, not an aggregate
    del(taken.data);                     // ok
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
| struct      | `point`             | C layout | all zero    | no       | D3.8        |
| enum        | `color`             | 4        | `0`         | yes      | D3.9        |
| pointer     | `node*`, `void*`    | 8        | `null`      | identity | D3.11-D3.13 |
| function    | `fn i32(i32, i32)`  | 8        | `null`      | identity | D3.10       |

Alignment equals size for primitives; pointers, function pointers, slices and strings align to
8, arrays to their element and structs to their most-aligned field, with C/System V layout
(D3.1, D3.8). `void` is not a value type (`void v = f();` is an error). Type suffixes (D3.6)
come in three groups: `*` suffixes before the array group apply to the element (`node*[16]` is
sixteen pointers); array and slice suffixes read outside-in (`i32[3][4]` is indexed `a[i][j]`
with `i < 3`, `i32[][4]` is a slice of `i32[4]`); a `*` after the array group points to the
whole array or slice (`u8[]*` is a pointer to a slice, `i32[4]*` a pointer to an `i32[4]`), and
no array suffix may follow it (`i32[4]*[2]` does not parse; wrap it in a struct). Suffixes after
a function type apply to the function type. Any pointer, `void*`, slice or `string` type may be
qualified `own` (3.9); the qualified type has the size, zero value and equality of the
unqualified one and is a distinct type (D17.1). Identity, layout, conversions and the full
constant rules are in `type-system.md`.

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
type" for comparison and `?:` operands means identical, mutability levels included (D3.12): the
implicit drop of 3.4 applies only to initialization, assignment, argument passing and `return`.
The `own` mark is the exception: the operands of `==`, `!=` and `?:` are lent first (D17.4), so
`n == m` compares an `own node*` with a `node*`, while an `own` rvalue operand is an error
(D17.8). Equality is defined on integers, floats, `bool`, `char`, enums, pointers (identity),
function pointers (identity) and `string` (contents: `len` then bytes, so the zero string equals
`""`); it is an error on structs, fixed arrays and slices. `>>` is arithmetic for signed and
logical for unsigned types; `<<` discards the bits shifted out and never checks for overflow,
so `1 << 31` on `i32` is `-2147483648` in both build modes; only the count is checked (below).
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
bool same = n == m;       // ok: own node* n is lent for the comparison with node* m
bool nu = new(node) == null;  // error: owning temporary would leak
```

### 5.3 Untyped constants in context (D4.1-D4.5)

Integer, float and char literals, and constant expressions built from them, are untyped
constants. An untyped constant takes its type from context: the declared type of the variable
being initialized or assigned, the other operand of a binary operator, the parameter type, the
return type, the `case` operand type, or an index, slice-bound or `new` count position, where
any integer type is accepted and a negative constant is a compile error. `cast` is not a
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
(D4.5). The full rules are in `type-system.md`, section 10.

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

Lvalues are: variables and parameters; module-level constants and globals (a constant is an
immutable lvalue: addressable and sliceable, never assignable); `*p`; `p->f`; `e.f` where `e` is
an lvalue; `e[i]` where `e` is an lvalue fixed array or any slice or string expression; and
parenthesized lvalues. `.len` and `.ptr` are never lvalues. Field access and indexing on an
rvalue struct or array yield rvalues copied through a temporary. `&e` requires an lvalue and
yields `T*` with the mutability of 3.6, never `own` at its outermost reference (D17.3): `&buf`
for an `own mut u8[] buf` is `mut u8[] own*`, a borrowed pointer to an owned slot. `&f` for a
function `f` is an error, because a function name is already a value. `*p` requires a pointer
type other than `void*` or a function pointer and yields the pointee. There is no pointer
arithmetic: `p + 1`, `p++` and `p[i]` are errors (D10.4); the only ways to obtain a pointer are
`null`, `&`, `new`, `.ptr`, `cast`, a function name and calls, and only `new`, a `cast` to an
`own` type and calls yield `own` pointers (3.9). `null` is the zero pointer and function-pointer
value (D10.5); it has no type of
its own: it takes its type from context and is an error where no pointer, `void*` or
function-pointer type is expected. `== null` and `!= null` are allowed on pointers, `void*` and
function pointers only; slices and strings compare `.len` or `.ptr`. Dereferencing `null`, and
returning the address of a local or a slice of a local array, are undefined behavior and are
not diagnosed.

```fort
i32* a = &(x + 1);        // error: '&' requires an lvalue
i32* b = &point{1, 2}.x;  // error: '&' requires an lvalue
fn i32(i32) c = &inc;     // error: '&' on a function; write 'inc'
i32 d = *vp;              // error: cannot dereference 'void*'
i32* e = p + 1;           // error: no pointer arithmetic
bool f = s == null;       // error: 'null' compared with a slice; use 's.ptr == null'
print(null);              // error: 'null' needs a pointer-typed context
bool g = null == null;    // error: 'null' has no type of its own
own node* h = &n;         // error: '&' yields a borrowed pointer; cannot add own
```

### 5.6 Field access, `->`, `.len` and `.ptr` (D6.10, D3.4, D3.5, D3.7, D9.4)

`e.f` accesses a field of a struct value. `p->f` is `(*p).f` and is required for pointers: `.`
on a pointer is an error with a hint, and `->` on a non-pointer is an error; when `*p` is itself
a pointer, write `(*p)->f`. Through a pointer to a slice or string, `->` also reaches the `.len`
and `.ptr` pseudo-fields (`out->len`); indexing through a pointer to an array or slice is
written `(*p)[i]` (5.7). Qualified names also use `.`: `io.read_file(path)`,
`geom.point{1, 2}`, `color.red`, `m.color.red`. Read-only pseudo-fields: a fixed array has
`.len`, an untyped integer constant; a slice has `.len` (`u64`) and `.ptr`, a pointer to the
element type carrying the element level's mutability (`i32*` for `i32[]`, `mut i32*` for
`mut i32[]`, `node* mut*` for `node* mut[]`) and never `own` at its outermost reference
(`mut u8*` for `own mut u8[]`, `mut node* own*` for `own mut node* own[]`, D17.3); a string has
`.len` (`u64`) and `.ptr` (`char*`).
Fixed arrays have no `.ptr`. `.len` of any expression of fixed-array type is a constant
expression and its operand is not evaluated (`m[i].len` is `4` for `i32[3][4] m`); `.len` of
a slice or string is not a constant expression.

```fort
i32 x = p.value;          // error: '.' on pointer 'p'; use '->'
i32 y = n->value;         // error: '->' on non-pointer 'n'; use '.'
i32* q = arr.ptr;         // error: fixed arrays have no '.ptr'
i32[s.len] b = {};        // error: '.len' of a slice is not a constant expression
i32[arr.len] c = {};      // ok: '.len' of a fixed array is a constant
own mut u8* d = buf.ptr;  // error: '.ptr' is a view; cannot add own
mut u8* e = new(u8[8]).ptr;  // error: owning temporary would leak (D17.8)
```

### 5.7 Indexing and slicing (D6.8, D6.9, D10.6, D10.7)

`e[i]`: `e` is a fixed array, slice or string; `i` is any integer type or an untyped constant,
and a negative constant is a compile error (D4.1). Signed indices are sign-extended and unsigned
ones zero-extended, and one unsigned comparison against the length catches negatives. Every
index is bounds-checked in every build mode; the `--no-bounds-check` option removes the checks
for benchmarking and is documented as unsafe (D10.6). Out of range is a runtime error
(`index 5 out of range for length 3`, `memory-model.md` section 6); a constant index out of
range for a fixed array is a compile error. Indexing a string yields `char`. Pointers cannot be
indexed, not even pointers to arrays: write `(*p)[i]`.

`e[lo..hi]`, `e[lo..]`, `e[..hi]`, `e[..]`: `e` is a fixed array (lvalue only), a slice or a
string; the bounds are any integer type or untyped constants (a negative constant is a compile
error); a missing `lo` is 0 and a missing `hi` is the length. The result is a slice, or a
`string` for a string operand, whose element mutability is that of `e`'s elements. It is always
a view: never `own` at its outermost reference, while `own` marks inside the element type stay
(`kids[1..]` on an `own mut node* own[] kids` is `mut node* own[]`) (D17.3). The runtime
check is `0 <= lo <= hi <= len` relative to the operand, not the original allocation; the result
may be empty. `p[lo..hi]` on a `T*` or `mut T*` yields a `T[]` or `mut T[]` with no check: this
is the explicit unsafe escape for foreign memory, and a range beyond the object is undefined
behavior (D10.7). Only the two-bound form exists for pointers: `p[lo..]`, `p[..hi]` and `p[..]`
are errors because a pointer has no length (D10.4). `void*` cannot be sliced.

```fort
i32[4] a = {1, 2, 3, 4};
i32 x = a[4];             // error: index 4 out of range for i32[4]
i32 w = a[-1];            // error: negative constant index
i32 y = a[i];             // runtime error when i >= 4
mut i32[] s = a[1..3];    // error: cannot add mutability; 'a' is immutable
i32[] t = a[1..3];        // {2, 3}
i32[] u = t[1..2];        // {3}: bounds are relative to t
own i32[] o = t[..];      // error: slicing yields a view; cannot add own
mut u8[] k = new(u8[8])[..4];  // error: owning temporary would leak (D17.8)
i32[] v = t[2..1];        // runtime error: slice bounds 2..1 out of range for length 2
i32 z = p[0];             // error: pointers cannot be indexed
i32[] f = p[0..n];        // ok: unchecked view of n elements at p
i32[] h = p[0..];         // error: a pointer has no length
i32[] g = vp[0..n];       // error: 'void*' cannot be sliced
```

### 5.8 Calls (D6.11, D8.2)

Arguments are matched by position and must convert to the parameter types (only the drops of
3.4 and 3.9 and the untyped-constant conversion of 5.3 apply). An `own` parameter takes
ownership of its argument (D6.11, D17.5): an `own` lvalue argument is written `move(x)` and an
`own` rvalue passes as it is; an `own` argument to a non-`own` parameter is lent, unless it is
an rvalue, which would leak (D17.8). There are no defaults, named arguments, overloading or
variadics. A function name, a function-pointer-typed expression and a
qualified name `mod.f` are callable; calling a null function pointer is undefined behavior. A
call statement to a `noreturn` function is a terminating statement (7.3). Arguments and results
are passed by value (7.1).

```fort
add(1);                   // error: 'add' takes 2 arguments, 1 given
add(1, 2.0);              // error: untyped float for parameter 'b' of type i32
i32 t = table[i](3);      // ok: index, then call
// fn void take(own mut node* n); fn void look(node* n); own mut node* n
take(n);                  // error: 'take' takes ownership of 'n'; write move(n)
take(move(n));            // ok: 'n' is null afterwards
take(new(node));          // ok: the temporary lands in the own parameter
look(n);                  // ok: lends 'n'
look(new(node));          // error: owning temporary would leak
```

### 5.9 `cast` (D6.4, D3.14, D4.4, D10.7, D17.3, D17.12)

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
- among `string`, `char[]`, `u8[]`, `mut char[]` and `mut u8[]`, and their `own` forms, where
  the result is `own` exactly when the target spells it: `cast(move(buf), own string)` for an
  `own mut u8[] buf`, and `cast(buf, string)` lends a view (D17.12); `T[]` to `mut T[]`;
  identity. A binding-level `mut` is never part of a cast target:
  the `mut` in a target names the levels behind the indirection, and `cast(s, u8[] mut)` is an
  error;
- adding `own` to any reference of a pointer or slice type: adoption of memory that came from C
  (`cast(p, own mut u8*)` for a `mut u8* p` returned by an extern that does not say `own`,
  `cast(line[0..n], own mut char[])`), the same unsafe escape as adding `mut`; a later `del` of
  adopted memory that is not the start of an allocation is undefined behavior (D10.7);
- dropping `own` at any level: a no-op wherever 3.9 already converts, and the escape where the
  monotone rule of 3.9 refuses the implicit form (`own mut node* own[]` to `mut node*[]`).

Forbidden: integer to `bool`; any other slice-to-slice cast (the element type of a slice never
changes, because `len` counts elements); pointer to slice; struct or array casts. An untyped
constant operand first takes its default type (D4.5) and then converts with the semantics above,
so `cast(0x80000000, i32)` is `-2147483648` and `cast(-1, u32)` is `4294967295`. `null` is not
a valid operand, because it has no type of its own (D10.5). There is no strict aliasing:
reading an object through a pointer of another type, as in `*cast(&x, u64*)` for an `f64 x`,
is defined. The result of a cast is `own` exactly when its target type says `own` (D3.14): an
`own` source cast to a non-`own` target lends, so the result is a view; a non-`own` source cast
to an `own` target adopts; and an `own` lvalue cast to an `own` target is a copy into an `own`
place, which must be written `cast(move(x), ...)` (D17.5), so that the bytes keep one owner. A
`cast` to an `own` type yields an `own` rvalue, which must land (3.9), and a cast that drops
`own` from an `own` rvalue is refused (D17.8).

```fort
bool b = cast(1, bool);            // error: cannot cast integer to bool; write '1 != 0'
u32[] u = cast(s, u32[]);          // error: cannot cast i32[] to u32[]
i32[] v = cast(p, i32[]);          // error: cannot cast pointer to slice; use 'p[0..n]'
point q = cast(r, point);          // error: cannot cast to struct type point
mut i32* w = cast(cp, mut i32*);   // ok: adds mutability explicitly
i32 c = cast('a', i32) - '0';      // 49
u8 t = cast(300, u8);              // 44: 300 is i32, then truncated
own mut u8* m = cast(libc.malloc(64), own mut u8*);   // ok: own rvalue to own type; del(m) frees
own mut u8* a2 = cast(c_alloc(64), own mut u8*);      // ok: adopts; mut u8* c_alloc(u64) is C
own string s1 = cast(move(buf), own string);          // ok: the target says own; 'buf' is emptied
own string s2 = cast(buf, own string);                // error: copying own lvalue 'buf' needs move
string s5 = cast(buf, string);                        // ok: lends a view; 'buf' still owns it
string s3 = cast(new(u8[4]), string);                 // error: owning temporary would leak
own string s4 = cast("abc", own string);              // compiles; del(s4) is undefined behavior
```

### 5.10 `sizeof` and `new` (D3.15, D10.2, D17.3, D17.8)

`sizeof(Type)` takes a type only and yields an untyped integer constant: 1 for `bool`, `char`,
`i8` and `u8`; the declared width for the other primitives; 8 for pointers and function
pointers; 16 for slices and `string`; 4 for enums; `N * sizeof(T)` for `T[N]`; the padded size
for structs. `sizeof(void)` and `sizeof(expr)` are errors, and there is no `alignof`.

`new(T)` returns `own mut T*` to zero-initialized heap storage. `new(T[n])` returns
`own mut T[]` of `n` zero-initialized elements, where `n` is any integer type or an untyped
constant; later brackets are fixed-array dimensions of the element (`new(i32[n][4])` is
`own mut i32[][4]`) (D17.3). A negative constant `n` is a compile error (D4.1); a negative `n`
at run time, a size that overflows, or allocation failure is a runtime error; `n == 0` is
allowed and yields a slice with a non-null pointer. `mut` is never written inside `new(...)`,
and `own` only in postfix positions of the element type, after a `*`: the result is fully
mutable and owned at its outermost reference, and `new(node* own[n])` yields
`own mut node* own[]`, an owned slice of owned pointers whose slots are all `null` (D17.3); a
prefix `own` inside `new(...)` does not parse (`grammar.md` section 6).
`new(T{...})`, `new(T[])` and `new(void)` are errors. The result is an `own` rvalue and must
land in an `own` place (3.9): `point* q = new(point);` is an error, not a conversion. Heap
storage is freed only by `del` (8.2).

```fort
u64 a = sizeof(x);                     // error: 'sizeof' takes a type
u64 b = sizeof(void);                  // error: 'sizeof(void)'
own mut point* p = new(point{1, 2});   // error: 'new' takes a type; assign after allocation
own mut i32[] s = new(i32[]);          // error: 'new' of a slice needs a count
own mut i32[] t = new(i32[n]);         // ok; runtime error if n < 0
own mut i32[] u = new(i32[-1]);        // error: negative constant count
own mut node** pp = new(node* mut);    // error: 'mut' inside 'new'
own mut node* own[] k = new(node* own[4]);   // ok: four null slots, each an owned node*
own mut node* own[] k2 = new(own node*[4]);  // error: prefix 'own' inside 'new' does not parse
own mut point* q = new(point);         // ok
point* r = new(point);                 // error: owning temporary would leak; write own mut point*
own point* w = new(point);             // ok: drops mut, keeps own
w->x = 1;                              // error: cannot write through immutable pointer 'w'
```

### 5.11 Struct and array literals (D6.5, D17.5)

`point{1, 2}` is positional: every field, in declaration order. `point{.x = 1, .y = 2}` is
designated: any order, omitted fields zeroed, no mixing with positional, no duplicates;
designators apply to structs only. `point{}` is all-zero; qualified names work
(`geom.point{1, 2}`). Typed array literals `i32[3]{1, 2, 3}` have exactly `N` elements or are
`{}`; further dimensions nest braces (`i32[2][2]{{1, 2}, {3, 4}}`). A bare `{...}` is allowed
only as the initializer of a declaration (local, module-level or `for` init) whose type is a
struct or fixed array, and nested inside another literal; `= {}` zero-initializes any aggregate,
slice, string or enum. Bare braces are not expressions: they cannot follow `=` in an assignment or
appear as an argument or `return` operand. Trailing commas are allowed in brace lists and enum
bodies, not in parameter or argument lists. Literals are rvalues; a literal whose leaves are
constant expressions is a constant expression. `IDENT {` is never a block, because every
control-flow condition is parenthesized and every body is braced. An `own` field or element of a
literal is an `own` place (D17.5): an `own` lvalue leaf must be written `move(x)`, and an `own`
rvalue leaf flows in directly. A literal is never itself `own`: `own string s = "";` is an error,
because a string literal is borrowed, and the empty owned string or slice is `{}`.

```fort
point a = {1, 2};                 // ok
point b = point{.y = 2};          // ok: x is 0
point c = point{1};               // error: positional literal needs every field (2)
point d = point{.x = 1, 2};       // error: cannot mix designated and positional
point e = point{.x = 1, .x = 2};  // error: duplicate field 'x'
i32[3] f = {1, 2};                // error: array literal for i32[3] needs 3 elements
i32 h = {};                       // error: '{}' initializes aggregates, slices, strings, enums
i32[] s = {};                     // ok: the zero slice
color m = {};                     // ok: holds 0, even if no member has that value
node* k = {};                     // error: '{}' does not initialize a pointer; use null
line l = {{0, 0}, {1, 1}};        // ok: nested bare literals
n = {3, 4};                       // error: bare braces are not an expression; write point{3, 4}
draw({1, 2});                     // error: bare braces are not an expression
// struct vec { own mut i32[] data; u64 len; }   own mut i32[] buf
vec v = vec{.data = buf, .len = 0};           // error: copying own lvalue 'buf' needs move(buf)
vec w = vec{.data = move(buf), .len = 0};     // ok
vec u = vec{.data = new(i32[8]), .len = 0};   // ok: an own rvalue lands in the own field
own mut u8[] z = {};              // ok: the zero slice, owned and empty
own string e = "";                // error: a literal is not owned; write {}
```

### 5.12 Conditional and wrapping operators (D6.6, D11.2, D16, D17.4)

`c ? a : b` requires a `bool` condition and two operands of one type, identical down to the
mutability levels (the implicit drop of 3.4 does not apply, D6.2); an untyped constant operand
adopts the other operand's type. The operands are lent (D17.4): an `own` lvalue operand
contributes its type without `own`, and the result is `own` only when both operands are `own`
rvalues or `null` (D6.2), so that the chosen temporary lands; an `own` rvalue paired with a lent
operand would leak and is an error (D17.8). It is right-associative and evaluates only the
chosen operand. `+% -% *%` and `+%= -%= *%=` are integer-only and wrap in two's complement in both
build modes, so hashes, checksums and counters can be written once and behave identically in
checked and release builds; `/`, `%` and the shifts have no wrapping form. Unsigned subtraction
traps in checked mode (`s.len - 1` on an empty slice): test first, or use `-%` when wrapping is
intended.

```fort
i32 y = n ? 1 : 2;                // error: condition must be bool
f64 z = flag ? x : 2.5;           // error: 2.5 cannot take the type i32 of 'x'
node* w = flag ? p : q;           // error: mut node* and node* differ (cast or copy first)
node* v = flag ? a : b;           // ok: own node* a and own node* b are lent
own mut node* n = flag ? new(node) : null;   // ok: both operands are own rvalues or null
own mut node* m = flag ? new(node) : a;      // error: owning temporary would leak; 'a' is lent
mut u32 h = 2166136261;
h = h *% 16777619;                // ok in both modes
u32 t = h *% 1.5;                 // error: wrapping operators are integer-only
```

## 6. Statements

A block is `{ statement* }`; every `if`, loop and `switch` body is a block.

### 6.1 Assignment (D7.2, D5.7, D6.3, D17.5, D17.11)

The assignment statements are `lv = e;`, the compound forms `lv op= e;` for `op` among
`+ - * / % +% -% *% & | ^ << >>`, and `lv++;` and `lv--;`. `lv` must be a mutable lvalue (3.6).
In `lv = e;` the value `e` converts to the type of `lv`. `lv op= e` has the operand rules and
overflow behavior of `lv op e` and evaluates `lv` once. `++` and `--` add or subtract 1 with the
checks of `+` and `-` and are allowed on integer types only: they are errors on floats, `char`
and pointers. Assignment is a statement, not an expression: `a = b = c;` and `if (x = 5)` do not
parse. This removes the "assignment where a comparison was meant" bug class, and no rule is
needed for the value of an assignment.

When `lv` has an `own` reference type, `e` must be an `own` rvalue or `move(x)` (3.9), and in
checked builds the store traps with `overwriting owned value` when `lv` currently holds a
non-zero value, because the old allocation would leak (D17.11, D11.4); the check runs after `e`
is evaluated, immediately before the store, and is reported at the `=` token. `del` and `move` leave
zero behind, so `del(v.data); v.data = new(...)` and `a = move(b)` after `move(a)` pass; release
builds store without checking; assignments of owning aggregates are not checked field by field.

```fort
i32 x = 1;
x = 2;                    // error: cannot assign to immutable 'x'
mut i32 y = 1;
y = y++ + 1;              // error: '++' is a statement, not an expression
if (y = 5) { }            // error: assignment is not an expression
a[i] = f();               // a[i] is addressed and bounds-checked before f() runs
d++;                      // error: '++' on float type f64
own mut node* h = null;
h = new(node);            // ok: 'h' was null
h = new(node);            // runtime error in checked builds: overwriting owned value
h = k;                    // error: copying own lvalue 'k' needs move(k)
del(h);
h = move(k);              // ok: 'h' was emptied by del
```

### 6.2 Expression statements, blocks, `if`, `while`, `do` (D7.3, D7.4, D7.5, D17.8)

An expression statement is a call whose result, if any, is discarded, unless the result has an
owning type, which nothing could then free (D17.8); any other expression as a statement is an
error, as is the empty statement `;`. A bare block `{ ... }` is a statement and
a scope. `if (cond) { } else if (cond) { } else { }` takes parenthesized `bool` conditions, and
braces are mandatory on every branch, which removes the dangling-`else` and "goto fail" bug
classes. `while (cond) { }` and `do { } while (cond);` take a `bool` condition; in `do`,
variables declared in the body are not visible in the condition.

```fort
compute(x);               // ok: result discarded
str.dup(s);               // error: owning result discarded; bind it or del it
a * b;                    // error: expression statement must be a call
point{1, 2};              // error: expression statement must be a call
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

### 6.4 Range `for` (D7.5, D17.10)

`for (T x : coll) { }` and `for (mut T x : coll) { }`: `coll` is a fixed array, slice or string
expression, evaluated once before the first iteration; a fixed array that owns nothing is
evaluated as a value, so the loop iterates over a copy. `x` is a fresh copy of each element in
order, taken at the start of its iteration. `T` is the element type (`char` for a string); `mut`
makes the copy assignable without affecting the collection. The loop lends its collection
(D17.10): an owning collection, an `own` slice or an owning fixed array, is iterated in place
and is never moved or copied; `T` is the element type without its outermost `own`, and an `own`
range variable is an error. Moving an element out is explicit,
`move(kids[i])` in an index loop. A range over elements that are owning aggregates (3.9) is an
error, since the copy could not be made without `move`; iterate by index and take `&a[i]`.
`break` and `continue` work as in other loops.

```fort
i32[3] a = {1, 2, 3};
for (i32 v : a) { print(v); }          // 123
for (char c : "hi") { print(c); }      // hi
for (i64 v : a) { }                    // error: element type is i32, not i64
for (mut i32 v : a) { v = 0; }         // ok: modifies the copy only
for (i32 v : p) { }                    // error: cannot iterate a pointer; slice it first
// own mut node* own[] kids; vec[4] vecs (struct vec has an own field)
for (mut node* c : kids) { c->value = 0; }     // ok: lends each node
for (own mut node* c : kids) { }               // error: a range variable cannot be own
for (mut u64 i = 0; i < kids.len; i++) {
    take(move(kids[i]));                       // ok: moving out is explicit
}
for (vec v : vecs) { }                         // error: elements are owning; iterate by index
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
enum color { red, green, blue }
switch (c) {
    case color.red:
        handle_red();
}                                   // error: switch over color does not handle green, blue
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

### 6.6 `defer` (D7.8, D11.4, D16, D17.5)

`defer` is followed by an assignment, a `++`/`--` statement, a call statement, or a block.

- The deferred code runs when the enclosing block is exited by any path: falling off the end,
  `return`, `break` or `continue`. A loop body is a block, so a `defer` in it runs at the end of
  every iteration, before `step`; a `case` body is a block too.
- The set of deferred statements that run at an exit is static: those textually before the exit
  in each exited block, innermost block first, in reverse textual order within a block.
- Nothing is captured at `defer` time; the statement is ordinary code executed at exit, so after
  `defer del(p);` a later `del(p); p = move(q);` makes the exit free `q`.
- `return e` evaluates `e` before deferred code runs, so deferred code cannot change the returned
  value. `return x` of an `own` local or parameter is an implicit move that empties `x` first
  (6.7), so `defer del(x);` frees `x` on every path except the one that hands it to the caller
  (D17.5); the `build` function in 3.9 is the pattern.
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
fn own mut u8[] slurp(i32 fd) {
    own mut u8[] buf = new(u8[4096]);
    defer del(buf);           // a no-op on the returning path, a free on every other
    if (io.read(fd, buf) < 0) {
        panic("read failed"); // aborts: deferred code does not run
    }
    return buf;               // implicit move: buf is {null, 0} when del(buf) runs
}
defer cleanup();              // error: 'defer' at module level
```

### 6.7 `return` (D7.11, D8.4, D17.5, D17.7)

`return e;` returns `e` converted to the function's return type; `return;` returns from a `void`
function. `return e;` in a `void` function and `return;` in a non-`void` function are errors. A
non-`void` function must end in a terminating statement (7.3). `return x;` where `x` is a local
variable or parameter of `own` type, or a local owning aggregate, is an implicit `move(x)`
(D17.5, D17.7): `x` is emptied before deferred code runs (6.6). Any other `own` lvalue operand,
a field, an element or a global, needs an explicit `move`, and an `own` rvalue flows as it is.
Because the implicit move yields an `own` rvalue, returning an `own` local from a function whose
return type is not `own` is refused as a leaking temporary (D17.8).

```fort
fn void f() { return 1; }     // error: 'return' with a value in a void function
fn i32 g() { return; }        // error: 'return' without a value in a function returning i32
fn own mut node* pop(mut list* l) {
    return l->head;           // error: 'l->head' is not a local; write move(l->head)
}
fn node* leak() {
    own mut node* n = new(node);
    return n;                 // error: owning temporary would leak; return own mut node*
}
```

## 7. Functions

### 7.1 Declaration, parameters and results (D8.1, D8.2, D8.3, D5.6, D7.10, D17.5, D17.7)

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

An `own` parameter takes ownership of its argument (D6.11, D17.5): the callee frees it or stores
it in an `own` place, and since a parameter is a local, `del(p)` is allowed on an immutable `p`
and `return p` is an implicit move (3.9). A parameter of owning aggregate type receives its
argument only through `move`, which is why functions take `vec*` or `mut vec*` (D17.7). A
return type may be `own` (`fn own mut u8[] read_all(i32 fd)`), and its value must land in the
caller (D17.8).

```fort
fn i32 f(i32) { return 0; }             // error: parameter needs a name
fn void g() {
    fn void inner() { }                 // error: functions are top-level only
}
fn void bump(i32 n) { n += 1; }         // error: cannot assign to immutable parameter 'n'
fn void bump2(mut i32 n) { n += 1; }    // ok: modifies the local copy
fn i32[4] copy(i32[4] a) { return a; }  // the array is copied in and copied out
fn void take(own mut node* n) { del(n); }         // ok: 'n' need not be mut
fn void eat(vec v) { del(v.data); }               // callers must write eat(move(x))
fn void reset(mut vec* v) { del(v->data); v->len = 0; }   // the usual shape
```

### 7.2 Function types and values (D3.10, D3.6, D17.1)

A function type is written `fn R(P1, P2)` with parameter types only. Identity is structural over
the parameter types (including pointee mutability), the return type and `noreturn`; level-0
`mut` on parameters is ignored. `own` at any reference of a parameter or return type is part of
the identity (D17.1): `fn void(own node*)` and `fn void(node*)` are different types, and so are
`fn own mut node*()` and `fn mut node*()`. `own` on a function-pointer type itself is an error.
A function name, or a qualified name `m.f`, used as a value has its function type; `&f` and `*f`
are errors. `null` is a valid value, and calling it is undefined behavior. `==` and `!=` compare
identity. Suffixes after a function type apply to the function type: `fn i32(i32)[4]` is an
array of four function pointers, `fn i32[4](i32)` returns
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
fn void(node*) t = take;          // error: fn void(own mut node*) is not fn void(node*)
own fn i32(i32) o = inc;          // error: 'own' on a function-pointer type
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
statement, or it is a compile error ("missing return", reported at the body's closing brace).
The rule is structural, not a data-flow analysis: a loop that may run zero times does not
terminate, however obvious its `return`. Catching this at compile time is a core "better than
C" promise.

```fort
fn i32 sign(i32 x) {
    if (x < 0) {
        return -1;
    } else if (x > 0) {
        return 1;
    }
}                                 // error: missing return (add an else branch)
fn i32 code(color c) {
    switch (c) {
        case color.red:
            return 1;
        case color.green, color.blue:
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
    del(args);                    // error: 'args' is a borrowed string[], not own (D17.3)
    return 0;
}
fn void main() { }                // error: 'main' must return i32
```

## 8. Builtins

### 8.1 Keyword forms (D12.1)

`new(T)`, `new(T[n])`, `sizeof(T)` and `cast(e, T)` are keywords that take type operands; they
are specified in 5.9 and 5.10.

### 8.2 Universe functions (D12.2, D10.3, D11.4, D17.6, D17.9)

`del(x)`, `move(lv)`, `assert(cond)`, `panic(msg)`, `print(...)`, `println(...)`, `eprint(...)`,
`eprintln(...)`, `fprint(fd, ...)` and `fprintln(fd, ...)` live in the universe scope, use
ordinary call syntax and have special typing. They may be shadowed by a module-level or local
declaration (3.8) and cannot be used as values. `move` yields a value; the others produce none
and appear only as call statements, `defer` operands and `for` init or step.

`del(x)` frees the allocation designated by `x`, which must have an `own` type of any
mutability: an `own` pointer, `own void*`, `own` slice or `own string`, as an lvalue or an
rvalue (D17.9, D10.3). On an lvalue, `del` empties the operand as `move` does (3.9), so the
binding need not be `mut` but a level reached through `*p`, `p->f` or `s[i]` must be; on an
rvalue it only frees. `del(null)` (the literal adopts `own void*`) and `del` of a zero slice or
string are no-ops, so `del(buf); del(buf);` frees once and a use after `del` dereferences
`null`. `del` is shallow:
`del(kids)` on an `own mut node* own[]` frees the slots, not the nodes, and `del` of a struct or
array is an error. A view, a sub-slice, a `.ptr`, a stack address, a literal and a `string` that
is not `own` are compile errors, because none of them has an `own` type. Allocations have no
header, so `new`/`del` and C `malloc`/`free` are interchangeable, and C memory is adopted with
`cast` (5.9).

`move(lv)` takes an lvalue of owning type, an `own` reference or an owning aggregate, yields its
value and leaves the zero value behind (D17.6); the mutability rules are those of `del`. It is
the one universe function with a value, and that value is an `own` rvalue which must land (3.9):
`move(x);` as a statement is an error. Moving a zero value yields a zero value.

`assert(cond)` takes a `bool`; on failure it reports `<file>:<line>:<col>: assertion failed:
<expression text>` and aborts, and it is active in every build mode. `panic(msg)` takes a
`string`, reports `<file>:<line>:<col>: panic: <message>` and aborts; it is `noreturn`, so a
`panic(...)` statement is terminating (7.3). Runtime error contract (D11.4): the runtime flushes
buffered output, writes one line to stderr (`<file>:<line>:<col>: runtime error: <message>` for
the bounds, overflow, shift, division and allocation checks, with the fixed message texts of
`memory-model.md` section 6) and calls `abort()`, so the process dies with SIGABRT, status 134
under a shell. `<file>` is the path the compiler opened; the column is that of the operator
token, or of the builtin's name for `new`, `assert` and `panic`; the `assert` text is the
source text of the expression, verbatim. Deferred code does not run.

```fort
// own mut u8[] buf; own mut node* n; node* v = n; node* own[] view; vec w; string s; i32 k
del(s);                       // error: 's' is a string, not an own string
del(buf[1..]);                // error: a sub-slice is a view, not an own slice
del(buf.ptr);                 // error: '.ptr' is a view
del(&k);                      // error: a stack address is not owned
del("abc");                   // error: a literal is not owned
del(v);                       // error: 'v' is a borrowed node*, not own
del(w);                       // error: 'del' takes a reference, not an aggregate
del(view[0]);                 // error: cannot empty immutable slot 'view[0]'
del(new(node));               // ok: frees the temporary
del(n); del(n);               // ok: the second call is del(null)
i32 r = del(n);               // error: 'del' has no value
fn void(own mut node*) f = del;   // error: 'del' cannot be used as a value
move(n);                      // error: owning temporary would leak
own mut node* m = move(k);    // error: 'move' needs an owning operand; 'k' is an i32
own mut node* o = move(v);    // error: 'move' needs an owning operand; 'v' is a borrowed node*
own mut node* q = move(view[0]);  // error: cannot move out of immutable slot 'view[0]'
assert(k);                    // error: 'assert' requires a bool
panic(1);                     // error: 'panic' requires a string
assert(s.len > 0);            // runtime error when s is empty
```

### 8.3 The print family (D12.2, D11.7, D11.5)

`print` and `println` accept zero or more arguments of integer, float, `bool`, `char`, enum,
pointer, function pointer or `string` type and write them to stdout with no separators;
`println` appends `\n`. `eprint` and `eprintln` do the same to stderr, `fprint` and `fprintln`
to the `i32` descriptor `fd`. Each argument compiles to one per-type runtime call and is
evaluated and written in turn, left to right; an untyped constant argument takes its default
type (D4.5). Structs, arrays and slices are not printable. An `own` pointer or string argument
is lent; an `own` rvalue argument is an error (D17.8).

| Type      | Text                                                            |
|-----------|-----------------------------------------------------------------|
| integers  | decimal; `u8` is a number                                       |
| `bool`    | `true` or `false`                                               |
| `char`    | its byte                                                        |
| enum      | the member name, or the number if no member matches             |
| pointers  | `0x` + lowercase hex; `0x0` for null; also `void*`, fn pointers   |
| `string`  | its bytes                                                       |
| floats    | shortest round-trip decimal in the argument's type, `%g`-style  |

Floats print as the shortest decimal that round-trips in the argument's own type (`f32` or
`f64`), in exponent form below 1e-4 or at 1e17 and above, the exponent written as `e`, a sign
and at least two digits (`1e+21`, `1.5e-07`); `.0` is appended when the text has neither `.`
nor `e`; `inf`, `-inf` and `nan` print as such. Output buffering (D11.5): `print` and `println`
write to a runtime buffer for stdout, which `fprint(1, ...)` shares; `eprint` and `eprintln` are
unbuffered; `fprint` and `fprintln` use one buffer per descriptor. An `extern` write to a
descriptor bypasses the buffers. Buffers flush when full, at exit and before any runtime error;
the runtime exports `fort_rt_flush(i32 fd)` and `fort_rt_flush_all()`, and `io.close` and
`io.flush` call the former (`memory-model.md` section 7).

```fort
println("x = ", 42, ", ok = ", true);       // x = 42, ok = true
print('a');                                 // a
print(cast('a', u8));                       // 97
println(1.0, " ", 0.5, " ", 1e20);          // 1.0 0.5 1e+20
println(cast(0.1, f32), " ", 0.00001);      // 0.1 1e-05
println(color.green, " ", cast(7, color));  // green 7
void* v = null;
println(v);                                 // 0x0
println(pt);                                // error: cannot print a value of struct type point
println(str.dup("x"));                      // error: owning temporary would leak
```

## 9. Not in v1

A number of familiar features are deliberately absent from v1: generics, unions, methods,
closures, variadics, overloading, visibility modifiers, type aliases, labeled `break`, string
`switch`, linear ownership (compile-time detection of leaks and of use after `move`, D17.14) and
others. D15 in `decisions.md` is the authoritative list; it names each feature with
the idiom to use instead, and this document does not repeat it.
