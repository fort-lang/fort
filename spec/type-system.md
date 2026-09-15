# fort type system

This document specifies fort's types, mutability model, conversions and constants for v1. It
implements the decisions in `decisions.md`, cited as `(Dn.m)`, and the type grammar in
`grammar.md`; where it disagrees with either of them, they win and this document has a bug.
Expression and statement semantics live in `core-language.md`; allocation, in-memory layout of
values and runtime checks live in `memory-model.md`.

## 1. Overview

All types are resolved at compile time. There is no run-time type information, no inference
(`var`/`auto`) and no type alias (D3.16). Every type is one of the forms below (D3).

| Form             | Written           | Kind          | Size          | Zero value  | Decision |
|------------------|-------------------|---------------|---------------|-------------|----------|
| integer          | `i8` .. `u64`     | scalar        | 1, 2, 4, 8    | `0`         | D3.1     |
| float            | `f32`, `f64`      | scalar        | 4, 8          | `0.0`       | D3.1     |
| boolean          | `bool`            | scalar        | 1             | `false`     | D3.3     |
| character        | `char`            | scalar        | 1             | `'\0'`      | D3.2     |
| no value         | `void`            | none          | none          | none        | D3.1     |
| pointer          | `T*`, `T* own`    | reference     | 8             | `null`      | D3.12    |
| opaque pointer   | `void*`, `void mut*` | reference  | 8             | `null`      | D3.11    |
| function pointer | `fn (P1, P2) R`    | reference     | 8             | `null`      | D3.10    |
| fixed array      | `T[N]`            | aggregate     | `N*sizeof(T)` | all zero    | D3.4     |
| span             | `T@`, `T@ own`    | reference     | 16            | `{null, 0}` | D3.5     |
| string           | `string`, `string own` | reference | 16           | `{null, 0}` | D3.7     |
| struct           | `name`            | aggregate     | fields, padded| all zero    | D3.8     |
| enum             | `name`            | scalar        | 4             | `0`         | D3.9     |

The three kinds differ in what a copy means:

- A **scalar** is copied as a machine word; the copy is independent of the original.
- An **aggregate** (struct, fixed array) is a value: assignment, argument passing and `return`
  copy every field or element (D3.4, D3.8, D8.2).
- A **reference** value (pointer, `void*`, function pointer, span, `string`) is itself copied by
  value, but the copy designates the same target storage as the original. Two spans obtained from
  one `new` alias the same elements; two pointers to one variable alias that variable.

A reference type other than a function pointer may be qualified `own` (section 8): `T* own`,
`void* own`, `T@ own`, `string own`. The qualifier says that the reference designates the start
of a live allocation that `del` may free; it changes neither size, layout nor zero value, but it
is part of the type, and copying an `own` value out of a variable is written `move` (D17.1,
D17.5). A struct or array that contains an `own` reference is an owning aggregate (section 8.5).

The zero value is what `= {}` produces for aggregates, spans, strings and enums (D6.5), what
`new` fills allocations with (D10.2), and what a zeroed scalar field holds.

`noreturn` is a return type, not a type (D8.5): `noreturn x = ...;` does not parse.

A type additionally carries one mutability bit per storage level behind an indirection
(section 7) and one ownership bit per reference (section 8). The binding's own mutability
(level 0) is a property of the declaration, not of the type (D3.12): `i32 x` and `i32 mut x`
hold values of the same type `i32`; `node* own p` and `node* p` do not.

## 2. Primitive types

| Type   | Size | Alignment | Values                                      |
|--------|------|-----------|---------------------------------------------|
| `i8`   | 1    | 1         | -128 .. 127                                 |
| `i16`  | 2    | 2         | -32768 .. 32767                             |
| `i32`  | 4    | 4         | -2^31 .. 2^31 - 1                           |
| `i64`  | 8    | 8         | -2^63 .. 2^63 - 1                           |
| `u8`   | 1    | 1         | 0 .. 255                                    |
| `u16`  | 2    | 2         | 0 .. 65535                                  |
| `u32`  | 4    | 4         | 0 .. 2^32 - 1                               |
| `u64`  | 8    | 8         | 0 .. 2^64 - 1                               |
| `f32`  | 4    | 4         | IEEE 754 binary32                           |
| `f64`  | 8    | 8         | IEEE 754 binary64                           |
| `bool` | 1    | 1         | `true`, `false`                             |
| `char` | 1    | 1         | one byte, 0 .. 255                          |
| `void` | none | none      | none; only a return type or the base of `void*` |

Alignment equals size for every primitive (D3.1). Raw bytes are `u8`; there is no distinct type
for them (D3.1). Integers are two's complement; `>>` is arithmetic on signed and logical
on unsigned types (D6.2). Floats follow IEEE 754 (D6.12). Every arithmetic, bitwise and
comparison operator requires both operands to have the same type; there is no promotion, not
even between `u8` and `i32` (D6.2).

```fort
i32 a = 1;
i64 b = a;                  // error: no implicit widening, use cast(a, i64)
u8 c = 1;
i32 d = a + c;              // error: operands of + must have the same type
```

### 2.1 `char`

`char` is a distinct one-byte character type (D3.2). It supports `== != < <= > >=`, `switch`,
`cast` to and from every integer type, and nothing else. Ordering compares unsigned byte values.
Character literals are untyped constants whose default type is `char` (D4.3). Strings are
sequences of `char` (D3.7).

```fort
char c = 'a';
bool lower = c >= 'a' && c <= 'z';
i32 digit = cast(c, i32) - '0';      // '0' becomes i32 from the other operand (D4.3)
u8 byte_value = 'a';                 // 97: a char literal in an integer context
char d = 65;                         // error: an integer literal never becomes char (D4.3)
char e = c + 1;                      // error: char has no arithmetic
char f = c & 0x0F;                   // error: char has no bitwise operators
```

### 2.2 `bool`

`bool` has exactly the values `true` and `false` (D3.3). It supports `== != ! && ||` and `cast`
to any integer type, yielding 0 or 1. Every condition (`if`, `while`, `for`, `do`, `?:`,
`assert`) must be a `bool`; there is no truthiness (D3.3).

```fort
bool b = 3 < 4;
i32 n = cast(b, i32);                // 1
if (n) { }                           // error: condition must be bool, write n != 0
node* p = null;
while (p) { }                        // error: condition must be bool, write p != null
bool c = cast(1, bool);              // error: no cast from integer to bool
bool d = b < true;                   // error: bool has no ordering
```

### 2.3 `void`

`void` names the absence of a value. It appears only as the return type of a function and as the
base of `void*` (D3.1, D3.11).

```fort
fn log(string s) void { }
void v = log("x");                   // error: void is not a value type
void[4] a = {};                      // error: void is not an element type
u64 n = sizeof(void);                // error: sizeof(void) (D3.15)
```

## 3. Arrays, spans and strings

### 3.1 Fixed arrays `T[N]`

`T[N]` holds exactly `N` elements of `T` contiguously (D3.4). `N` is a constant expression
(D4.6) greater than 0. Arrays of different lengths are different types. A fixed array is a
value: assignment, argument passing and `return` copy all `N` elements (D8.2). `a.len` is an
untyped integer constant equal to `N` (D3.4, D4.6). A fixed array has no `.ptr`; obtain a
pointer to an element with `&a[i]` or a span with `a[lo..hi]` (D6.9). A constant index that is
out of range or negative is a compile error (D6.8, D4.1); any other index is checked at run time.
Sizes are computed exactly, and a type whose size would exceed `2^63 - 1` bytes, the range of an
`i64` index, is the compile error "type is too large" at the declaration that introduces it,
reported like the infinite-size error of section 4.1 rather than as an allocation failure of the
compiler (D3.4).

```fort
u8[9223372036854775807] ok = {};     // 2^63 - 1 bytes: the largest type there is
u16[9223372036854775807] big = {};   // error: type is too large
struct wide { u8[4611686018427387904] a; u8[4611686018427387904] b; }   // error: type is too large
```

```fort
i32[4] a = {1, 2, 3, 4};
i32[4] b = a;                        // copies four elements; b and a are independent
i32[3] c = a;                        // error: i32[4] is not i32[3]
i32[0] z = {};                       // error: array length must be greater than 0
u64 n = a.len;                       // 4, an untyped constant
i32* p = a.ptr;                      // error: fixed arrays have no .ptr
i32 x = a[4];                        // error: constant index 4 out of range for i32[4]
i32[4]* q = &a;                      // pointer to the whole array (D3.6)
bool same = a == b;                  // error: no == on fixed arrays (D3.13)
```

Array literals are `i32[3]{1, 2, 3}` anywhere, `{1, 2, 3}` as the initializer of a declaration of
array type, and `{}` for all-zero (D6.5). A literal must supply exactly `N` elements or be empty.

### 3.2 Spans `T@`

A span is a fat pointer `{T* ptr; u64 len}` designating `len` elements of `T` that live
elsewhere; whether it owns them is part of its type (D3.5): a span from `new` is `own` and a
span taken of something is a view (section 8.3). All spans with the same element type, the same
element mutability and the same ownership are one type. The zero value is `{null, 0}`. Spans
come from `new(T, n)` (D10.2), from a span of an array, span, string or pointer (D6.9), from the
runtime (`string@ args`, D8.6) and from the zero initializer `{}`. There is no span literal.

`.len` (type `u64`) and `.ptr` are read-only pseudo-fields (D3.5). `s.ptr` is a pointer to the
element type whose target has the mutability of the span's elements and which is never `own`
itself (D17.3): textually, delete the trailing `@` together with any `own` or `mut` that follows
it, and append `*`.

| Span type       | `.ptr` type   | Span type                | `.ptr` type          |
|-----------------|---------------|--------------------------|----------------------|
| `i32@`          | `i32*`        | `node* mut@`             | `node* mut*`         |
| `i32 mut@`      | `i32 mut*`    | `node mut*@`             | `node mut**`         |
| `i32@ mut`      | `i32*`        | `string@`                | `string*`            |
| `bool@`         | `bool*`       | `i32[4]@`                | `i32[4]*`            |
| `i32@@`         | `i32@*`       | `i32@ mut@`              | `i32@ mut*`          |
| `u8 mut@ own`   | `u8 mut*`     | `node mut* own mut@ own` | `node mut* own mut*` |
| `u8@ own`       | `u8*`         | `node* own@`             | `node* own*`         |

```fort
i32 mut@ own s = new(i32, 8);        // eight zeroed elements, owned, all levels mutable
i32@ t = s;                          // same storage; own and element mutability dropped
u64 n = s.len;                       // 8
i32 mut* p = s.ptr;                  // a view of the elements
i32 mut* own q = s.ptr;              // error: .ptr is a view; cannot add own
i32@ e = {};                         // {null, 0}
i32@ u = {1, 2, 3};                  // error: there is no span literal
s.len = 4;                           // error: .len is not an lvalue (D6.7)
bool same = s == t;                  // error: no == on spans (D3.13)
```

### 3.3 Reading type suffixes

A type is `base_type [own] [mut] { ref_suffix } { array_suffix } { ref_suffix }`, where a
`ref_suffix` is `*` or `@` followed by its own optional `own` and `mut` (grammar section 4). A
reference suffix applies to everything to its left, so a sequence of them reads inside-out
(`node**` is a pointer to a pointer, `node*@` a span of pointers, `u8@*` a pointer to a span);
fixed-array suffixes form one group that reads outside-in like C declarators (`i32[3][4]` is
three arrays of four); reference suffixes before the group make arrays of references
(`node*[16]`, `node@[4]`) and after it references to the whole array (`i32[4]*`, `i32[4]@`); and
no array suffix may follow a trailing reference suffix (D3.6). Suffixes after a function type
apply to the function type. Each `own` and each `mut` marks the element it follows, the last
position being the binding's (sections 7.3 and 8.2).

| Type                     | Reads as                                             | `sizeof` |
|--------------------------|------------------------------------------------------|----------|
| `node*[16]`              | array of 16 pointers to `node`                       | 128      |
| `node**`                 | pointer to a pointer to `node`                       | 8        |
| `i32[3][4]`              | 3 arrays of 4 `i32`; `a[i][j]` with `i < 3`, `j < 4` | 48       |
| `i32[4]@`                | span whose elements are `i32[4]`                     | 16       |
| `i32@[4]`                | array of 4 spans of `i32`                            | 64       |
| `i32@@`                  | span whose elements are spans of `i32`               | 16       |
| `node* mut@`             | span of mutable slots, each holding a `node*`        | 16       |
| `u8@*`                   | pointer to a span header of `u8`                     | 8        |
| `i32[4]*`                | pointer to a whole `i32[4]`                          | 8        |
| `node*@*`                | pointer to a span of `node*`                         | 8        |
| `fn (i32) i32[4]`        | pointer to a function returning `i32[4]`             | 8        |
| `fn (i32) i32*`          | pointer to a function returning `i32*`               | 8        |
| `void*[2]`               | array of 2 opaque pointers                           | 16       |
| `u8 mut@ own`            | owned span of writable bytes                         | 16       |
| `node mut* own mut@ own` | owned span of owned pointers to mutable nodes        | 16       |
| `node* own@`             | borrowed span whose slots hold owned pointers        | 16       |
| `u8 mut@ own mut*`       | borrowed pointer to a slot holding an owned span     | 8        |
| `node* own[4]`           | array of 4 owned pointers: an owning aggregate       | 32       |
| `i32[4]*[2]`             | error: no array suffix after a reference suffix      |          |
| `i32 mut[4]`             | error: mark the array after its length, `i32[4] mut` |          |
| `mut i32*`               | error: nothing precedes the base type                |          |
| `node*[4] own`           | error: `own` never follows a fixed-array suffix      |          |
| `node own*`              | error: `own` marks a reference, not the base type    |          |

`new(i32[4], n)` returns `i32[4] mut@ own`: an owned span of `n` rows of four (D3.6, D10.2,
D17.3).

### 3.4 `string`

`string` is a distinct type with the layout of an immutable span of `char`, `{char* ptr; u64 len}`
(D3.7). String literals have type `string` and are stored in read-only memory followed by one NUL
byte that `len` does not count; a literal's `.ptr` can therefore be handed to C directly.
Sub-strings are not NUL-terminated. Indexing yields `char`, a span of a `string` is a `string`,
`.len` is `u64` and `.ptr` is `char*`. `==` and `!=` compare `len` first and then the bytes, so the
zero string `{null, 0}` equals `""`; only `.ptr == null` tells them apart (D3.7, D10.5). Bytes are
UTF-8 by convention and never validated. There is no `+`; `std.str` and `std.strbuf` concatenate
and return the owned form `string own` (D3.7, D13.2, section 8.6). Characters of a string are never
mutable (D5.2), whether or not the string is `own`.

```fort
string s = "hello";                  // storage: h e l l o NUL; s.len == 5
char c = s[1];                       // 'e'
string t = s[1..3];                  // "el", shares s's bytes, not NUL-terminated
char* p = s.ptr;
string z = {};                       // {null, 0}
bool eq = z == "";                   // true: both have len 0
bool dist = z.ptr == "".ptr;         // false
s[0] = 'j';                          // error: string characters are immutable
string u = s + t;                    // error: no + on string, use str.concat
u8@ raw = s;                         // error: string is distinct from u8@, use cast
string own d = str.dup(s);           // an owned copy; del(d) frees it
del(s);                              // error: 's' is a string, not a string own
```

## 4. Structs and enums

### 4.1 Structs

`struct name { T1 f1; T2 f2; }` declares a nominal type with no trailing semicolon (D3.8). Fields
are laid out in declaration order with natural alignment and the size is rounded up to the
struct's alignment (the largest field alignment), exactly as the selected target C ABI lays out
the same C struct. A struct has no methods and no inheritance.
An empty struct is an error. A struct may
contain itself only through a pointer or a span; containment by value is an "infinite size"
error (D3.8, D7.10). Structs are values (section 1) and support neither `==` nor `!=` (D3.13).

A struct `B` is contained **by value** in a struct `A` when a field of `A` is written `B`, or a
fixed array of any rank over it, and in no other case (D3.8). A `*` or an `@` anywhere in the
written type ends the containment, since the reference is a word or two whatever it refers to
(D3.11, D5.8), and so does a `fn` signature, a function pointer being an ordinary pointer whose
identity is structural over its signature (D3.10) and an aggregate crossing a call through a
hidden pointer (D9.9). That is the relation the "infinite size" error is a cycle of, and the one
a struct's size may depend on, so it is also what a declaration may wait for: top-level
declarations are order-independent within a module (D7.10), and a pair of structs that reach each
other by anything but value containment compiles in either order.

```fort
struct vec  { node mut* mut@ own items; }   // a span of pointers: two words
struct node { vec kids; i32 tag; }          // by value: three words
```

```fort
struct rec {
    u8 tag;
    i32 n;
    u16 k;
    f64 x;
}
```

| Field   | Offset | Size | Note                                     |
|---------|--------|------|------------------------------------------|
| `tag`   | 0      | 1    |                                          |
| padding | 1      | 3    | aligns `n` to 4                          |
| `n`     | 4      | 4    |                                          |
| `k`     | 8      | 2    |                                          |
| padding | 10     | 6    | aligns `x` to 8                          |
| `x`     | 16     | 8    |                                          |
| total   |        | 24   | `sizeof(rec) == 24`, alignment 8         |

```fort
struct empty { }                     // error: empty struct
struct loop { i32 v; loop next; }    // error: struct loop has infinite size
struct list { i32 v; list* next; }   // ok: self-reference through a pointer
struct tree { i32 v; tree@ kids; }   // ok: self-reference through a span
struct vec { i32 mut@ own data; u64 len; }   // ok: an owning struct (section 8.5)
rec a = rec{1, 2, 3, 4.0};           // positional: every field, in order
rec b = rec{.x = 1.0, .tag = 7};     // designated: any order, omitted fields zero
rec c = {};                          // all-zero
rec d = rec{1, .n = 2};              // error: positional and designated mixed
bool same = a == b;                  // error: no == on structs (D3.13)
```

Field mutability follows the struct that contains the field; a `mut` in a field type describes
only the levels behind the field's indirections (D5.5, section 7.5). A field may be an `own`
reference; the struct is then an owning aggregate and is copied with `move` (section 8.5).

### 4.2 Enums

`enum color { red, green = 5, blue }` declares a nominal type whose underlying type is `i32`
(D3.9). Members are scoped: `color.red` everywhere, including `case` labels, and `m.color.red`
for an enum from module `m` (D9.4). Enum members are not in the module namespace (D7.9). Values
start at 0 and each member is one more than its predecessor; an explicit value is a constant
expression converted to `i32` (D4.2) that may not refer to the enum itself; duplicate values are
errors. Enums support `==`, `!=`, `switch` and `cast` to and from every integer type; an
integer-to-enum cast is unchecked. There are no ordering operators. A zeroed enum holds 0 even if
no member has that value.

```fort
enum color { red, green = 5, blue }  // red 0, green 5, blue 6
color c = color.red;
color d = red;                       // error: enum members are scoped, write color.red
i32 v = cast(color.blue, i32);       // 6
color e = cast(7, color);            // holds 7; no member matches; not an error
bool lt = color.red < color.blue;    // error: enums have no ordering
color f = 0;                         // error: an integer never becomes an enum implicitly
enum e1 { a, b = 0 }                 // error: duplicate value 0
enum e2 { a = 1, b = a + 1 }         // error: value refers to the enum being declared
enum e3 { a = 2147483647, b }        // error: 2147483648 does not fit i32
```

A `switch` over an enum without `default` must list every member (D7.7):

```fort
switch (c) {
case color.red, color.green:
    println("warm");
}                                    // error: switch over color does not handle blue
```

Listing every member is not covering every value: a zeroed enum holds 0 whether or not 0 is a
member, and an integer casts to an enum unchecked, so the compiler gives such a `switch` a
`default` of its own that reports the runtime error `enum value 0 is not a member of color` and
aborts (D7.7, D11.4).

## 5. Function types and `void*`

### 5.1 Function types

A function type is written `fn (P1, P2) R` with parameter types only (D3.10, D8.1). A function
name, or a qualified name `m.f` naming a function in module `m`, used as a value has its function
type. Function-pointer types have no variable tail (D8.3).
A C extern with `...` is not a pointer value (D3.10).
Identity is structural over the parameter types, including the mutability levels behind their
indirections and their `own` marks (D17.1), the
return type, and whether the function is `noreturn`; `mut` at level 0 of a parameter is ignored
(D3.10, D5.6). `null` is a valid value; calling it is undefined behavior (D10.7). `==` and `!=`
compare identity. A function-pointer type itself is never `own` (D17.1).

| Types compared                          | Same? | Reason                                  |
|-----------------------------------------|-------|-----------------------------------------|
| `fn (i32) void` and `fn (i32 mut) void`   | yes   | the outermost position is ignored       |
| `fn (node*) void` and `fn (node* mut) void`| yes  | level 0 of a parameter is ignored       |
| `fn (node*) void` and `fn (node mut*) void`| no   | level 1 differs                         |
| `fn (node*) void` and `fn (node* own) void`| no   | the parameter's ownership differs       |
| `fn () node mut*` and `fn () node mut* own`| no   | the result's ownership differs          |
| `fn () i32` and `fn () void`              | no    | return type differs                     |
| `fn () noreturn` and `fn () void`         | no    | `noreturn` is part of the type          |

```fort
fn add(i32 a, i32 b) i32 { return a + b; }
fn (i32, i32) i32 op = add;
i32 r = op(2, 3);                    // 5
bool same = op == add;               // true
fn (i32, i32) i32 n = null;
fn (i32, i32) i32 p = &add;          // error: & on a function name (D3.10)
i32 s = (*op)(1, 2);                 // error: function pointers cannot be dereferenced
fn (i32, i32) i64 q = add;           // error: fn (i32, i32) i32 is not fn (i32, i32) i64
fn take(node mut* own n) void { del(n); }
fn (node*) void t = take;            // error: fn (node mut* own) void is not fn (node*) void
fn (i32, i32) i32 own u = add;       // error: an own marks a reference (D17.2)
```

### 5.2 `void*`

`void*` is an opaque pointer with no pointee type: it cannot be dereferenced, cannot reach
fields, cannot be indexed and has no span expression (D3.11). It reaches one storage level all the
same, the storage the address names, and `void mut*` marks that storage writable (D5.2, D5.3). No
expression reaches it, so the mark states what a callee may do with the address: C writes the same
distinction as `void*` against `const void*`. Every conversion to or from `void*` requires `cast`
(D3.14); `== null` is allowed (D10.5). `void* own` is its owned form (D17.1): `del` accepts it, a
`cast` between it and a typed pointer yields `own` exactly when the target says `own` (D3.14,
section 9.2), and it is the type C's `malloc` and `free` are declared with (section 11.2).

`void* mut p` and `void mut* p` are two types. The first is a rebindable binding whose target is
read-only; the second is a fixed binding whose target a callee may write. `void mut*` converts to
`void*` by the one implicit conversion of D5.4, and the reverse needs a `cast`.

```fort
i32 mut x = 1;
void* vp = cast(&x, void*);
void mut* wp = cast(&x, void mut*);  // the bytes behind wp may be written
void* back_off = wp;                 // ok: dropping mut is implicit (D5.4)
void mut* up = vp;                   // error: adding mut needs a cast
void* w = &x;                        // error: pointer to void* requires cast
i32 v = *vp;                         // error: void* cannot be dereferenced
i32 f = vp->x;                       // error: void* has no fields
i32 g = vp[0];                       // error: void* cannot be indexed
i32@ h = vp[0..1];                   // error: cannot take a span of a void*
i32 mut* back = cast(vp, i32 mut*);
u64 addr = cast(vp, u64);
bool isnull = vp == null;
void mut* own raw = libc.malloc(16); // an own rvalue lands; malloc answers a writable block
void* peek = raw;                    // ok: lends, and drops the mut on the way (D5.4)
u8 mut* own bytes = cast(move(raw), u8 mut* own);   // ok: the target says own, so raw is moved
u8 mut* own twice = cast(raw, u8 mut* own);         // error: copying own lvalue 'raw' needs move
```

## 6. Type identity and equality

Two types are identical when (D3.12):

| Form             | Identical when                                                         |
|------------------|------------------------------------------------------------------------|
| primitive        | same name                                                              |
| struct, enum     | same declaration (nominal); `m.vector` and `vector` inside `m` agree   |
| fixed array      | identical element type and equal length                                |
| span             | identical element type, element mutability (level 1) and `own` mark    |
| pointer          | identical pointee type, level-1 mutability and `own` mark              |
| `void*`          | same `own` mark and same level-1 mutability                            |
| function type    | structurally, as in section 5.1, `own` marks included                  |
| `string`         | same `own` mark; distinct from `char@` and `u8@`                     |

Mutability bits behind an indirection are part of identity at every depth: `node* mut*` and
`node**` are different types. Level 0 is not: `node* mut p` and `node* q` hold the same type.
Ownership marks are part of identity at every reference (D17.1): `node* own` and `node*`,
`node* own@` and `node*@`, `string own` and `string` are pairs of different types. Unlike
mutability, `own` never keeps two values from being compared: the operands of `==` and `!=`
are lent first (D17.4, section 8.4).

```fort
struct p1 { i32 x; }
struct p2 { i32 x; }
p1 a = {};
p2 b = a;                            // error: p1 is not p2, even with the same fields
```

`==` and `!=` are defined on integers, floats, `bool`, `char`, enums, pointers (address
identity), `void*`, function pointers (identity) and `string` (contents). They are compile errors
on structs, fixed arrays and spans (D3.13). Both operands must have identical types, including
mutability levels, because the implicit drop of section 7.4 does not apply to comparison operands
(D6.2); an untyped constant takes the other operand's type (D4.1), and `null` takes the type of
a pointer operand (D10.5).

```fort
i32 mut* p = &x;
i32* q = p;
bool e1 = p == q;                    // error: i32 mut* and i32* are different types
bool e2 = p == cast(q, i32 mut*);    // ok
bool e3 = null == null;              // error: null has no type here
i32@ s = {};
bool e4 = s == null;                 // error: compare s.ptr or s.len instead
node* own n = new(node);
node* m = n;
bool e5 = n == m;                    // ok: own is dropped from n for the comparison
bool e6 = n == null;                 // ok
node* own k = m;                     // error: node* own is not node*; cast adopts, or move
```

## 7. Mutability

### 7.1 Immutable by default

Everything is immutable unless marked `mut`: variables, parameters, the targets of pointers and
the elements of spans (D5.1). Struct fields and array elements share the mutability of the value
that contains them.

```fort
i32 x = 1;
x = 2;                               // error: x is immutable
i32 mut y = 1;
y = 2;
```

### 7.2 Storage levels

A declared type is a chain of storage levels (D5.2). Level 0 is the binding's own storage. Each
`*` and each `@` suffix introduces one further level: the storage reached through that
indirection. Levels are numbered from the binding inward, following the reading rules of
section 3.3: because reference suffixes read inside-out, the last reference suffix of the type is
level 1, the one before it level 2, and so on, whether it is a `*` or an `@`. Fixed arrays and
structs add no level: their elements and fields live in the storage of the containing value.
`string` has exactly one level (its characters are never mutable) and so does every function
type. A `void*` has two: the binding and the storage the address names, which no expression
reaches and whose mutability `void mut*` marks (D3.11). Each `*` and `@` also introduces a
reference, the pointer or span header stored at the level just outside the one it reaches;
references, not levels, carry the `own` mark (section 8.2).

| Declared type   | Level 0    | Level 1                     | Level 2                |
|-----------------|------------|-----------------------------|------------------------|
| `i32 x`         | `x`        |                             |                        |
| `node* p`       | `p`        | `*p`, `p->f`                |                        |
| `node** pp`     | `pp`       | `*pp`                       | `**pp`, `(*pp)->f`     |
| `i32@ s`        | `s`        | `s[i]`                      |                        |
| `i32@@ s`       | `s`        | `s[i]` (an `i32@` header)   | `s[i][j]`              |
| `node*@ t`      | `t`        | `t[i]` (a `node*` slot)     | `*t[i]`, `t[i]->f`     |
| `u8@* out`      | `out`      | `*out` (a span header)      | `(*out)[i]`            |
| `i32[4]* pa`    | `pa`       | `*pa`, `(*pa)[i]`           |                        |
| `i32[3][4] m`   | `m`, `m[i][j]` |                         |                        |
| `point q`       | `q`, `q.x` |                             |                        |
| `string s`      | `s`        |                             |                        |
| `void* vp`      | `vp`       | the bytes; no expression    |                        |

### 7.3 The placement rule

A `mut` marks the storage of the type element it follows (D5.3): after the base type, values of
that type; after a `*`, the pointer that suffix introduces, that is, the storage holding it;
after an `@`, the span header; after a fixed-array suffix, the array, whose elements share its
storage, so `i32[4] mut a` marks both and `i32 mut[4]` is an error ("mark the array after its
length"). Nothing precedes the base type, so `mut node* p` does not parse. Each storage level
has exactly one position, therefore every type has exactly one spelling and a doubled marker
(`node mut mut* p`) does not parse. The last position is the binding's own storage, so the `mut`
immediately before the name is what makes the variable assignable.

In the table, "rebind" is `x = ...` on the binding itself; "level 1" covers writes such as
`*p = v`, `p->f = v`, `s[i] = v`, `t[i] = q`; "level 2" covers `**pp = v`, `(*pp)->f = v`,
`s[i][j] = v`, `t[i]->f = v`. Every row of the D5.3 table appears below, with further shapes.

| Declaration          | rebind               | level 1              | level 2            |
|----------------------|----------------------|----------------------|--------------------|
| `i32 mut x`          | yes                  | not applicable       |                    |
| `point mut q`        | yes (and fields)     | not applicable       |                    |
| `i32[4] mut a`       | yes (and elements)   | not applicable       |                    |
| `node* p`            | no                   | no                   |                    |
| `node* mut p`        | yes                  | no                   |                    |
| `node mut* p`        | no                   | yes                  |                    |
| `node mut* mut p`    | yes                  | yes                  |                    |
| `i32@ s`             | no                   | elements: no         |                    |
| `i32@ mut s`         | yes                  | elements: no         |                    |
| `i32 mut@ s`         | no                   | elements: yes        |                    |
| `i32 mut@ mut s`     | yes                  | elements: yes        |                    |
| `node* mut@ t`       | no                   | slots: yes           | pointees: no       |
| `node mut*@ t`       | no                   | slots: no            | pointees: yes      |
| `node* mut* pp`      | no                   | `*pp`: yes           | `**pp`: no         |
| `node mut** pp`      | no                   | `*pp`: no            | `**pp`: yes        |
| `string mut s`       | yes                  | never                |                    |
| `node** pp`          | no                   | no                   | no                 |
| `node** mut pp`      | yes                  | no                   | no                 |
| `node mut** mut pp`  | yes                  | yes                  | yes                |
| `i32@ mut@ s`        | no                   | `s[i] = t`: yes      | `s[i][j] = 1`: no  |
| `i32@@ mut s`        | yes                  | no                   | no                 |
| `i32 mut@ mut@ mut s`| yes                  | yes                  | yes                |
| `i32[4]@ s`          | no                   | rows and cells: no   |                    |
| `i32[4] mut@ mut s`  | yes                  | rows and cells: yes  |                    |
| `node*[4] mut a`     | yes, and `a[i] = q`  | `a[i]->v = 1`: no    |                    |
| `node mut*[4] mut a` | yes, and `a[i] = q`  | `a[i]->v = 1`: yes   |                    |
| `i32[3][4] m`        | no, `m[i][j]` too    |                      |                    |
| `i32[3][4] mut m`    | yes, `m[i][j]` too   |                      |                    |
| `point* p`           | no                   | `p->x = 1`: no       |                    |
| `point mut* p`       | no                   | `p->x = 1`: yes      |                    |
| `string@ mut v`      | yes                  | `v[i] = "a"`: no     | `v[i][0]`: never   |
| `string mut@ mut v`  | yes                  | `v[i] = "a"`: yes    | `v[i][0]`: never   |
| `u8@* out`           | no                   | `*out = s`: no       | `(*out)[i]`: no    |
| `u8@* mut out`       | yes                  | `*out = s`: no       | `(*out)[i]`: no    |
| `u8@ mut* out`       | no                   | `*out = s`: yes      | `(*out)[i]`: no    |
| `u8 mut@ mut* out`   | no                   | `*out = s`: yes      | `(*out)[i]`: yes   |
| `i32[4]* pa`         | no                   | `(*pa)[i] = 1`: no   |                    |
| `i32[4] mut* pa`     | no                   | `(*pa)[i] = 1`: yes  |                    |
| `void* mut vp`       | yes                  | no expression        |                    |
| `void mut* vp`       | no                   | no expression        |                    |
| `fn (i32) i32 f`     | no, and none is legal| not applicable       |                    |
| `node mut* own mut@ own kids` | no          | slots: yes           | nodes: yes         |
| `node* own@ view`    | no                   | slots: no            | nodes: no          |
| `u8 mut@ own mut* out` | no                 | `*out = s`: yes      | bytes: yes         |
| `node* own mut n`    | yes                  | `n->x = 1`: no       |                    |

The last four rows carry `own` marks (section 8) and read exactly like their unqualified forms:
`own` never changes which levels are mutable. In `i32[4]@ s` the `[4]` adds no level, so a row
`s[i]` and a cell `s[i][j]` are both at level 1. In `node*[4] mut a` the `[4]` adds no level
either, so the slots are level 0 and the `mut` after the array's length makes the binding, and
with it every slot, assignable. In `u8@ mut* out` the `mut` after the `*` marks the storage that
holds the pointer, which is `out` itself; the span header `*out` is level 1 and writable because
the `*` reaches it.

Rationale (D5.3): this is C's east-const (`int const x`, `node const* p`, `node* const p`) with
the default inverted, and the same rule places `own` (section 8.2). Because reference suffixes
read inside-out, the binding's marker sits next to the name in every declaration, and no
combination is unspellable: the traversal local is `node mut* mut cur`, "writable target, fixed
binding" is `node mut* p`, and "fixed target, rebindable binding" is `node* mut p`.

### 7.4 Dropping mutability

Dropping mutability is one of the two implicit conversions (D3.14, D5.4); dropping `own`
(section 8.4) is the other, and the two combine. It applies when a value is used to initialize a
declaration, on the right of an assignment, as an argument, and as a `return` operand; it does
not apply to the operands of a comparison or of `?:`, which must have identical types (D6.2).
Level 0 of the receiving binding is unconstrained. For a level `k >= 1`, mutability may be
dropped only if every level from 1 to `k - 1` is immutable in the target type. Nothing adds
mutability at any level: no implicit conversion and no `cast` (D3.14).

| Conversion                                 | Result | Reason                                 |
|--------------------------------------------|--------|----------------------------------------|
| `node mut*` to `node*`                     | ok     | drops level 1                          |
| `node mut*` to `node* mut`                 | ok     | level 0 is free; level 1 dropped       |
| `node*` to `node mut*`                     | error  | adds mutability                        |
| `node mut* mut*` to `node**`               | ok     | drops levels 1 and 2                   |
| `node mut* mut*` to `node* mut*`           | error  | drops level 2 behind a mutable level 1 |
| `node mut* mut@` to `node*@`               | ok     | drops levels 1 and 2                   |
| `node mut* mut@` to `node* mut@`           | error  | drops level 2 behind a mutable level 1 |
| `i32 mut@ mut@` to `i32@ mut@`             | ok     | only level 0 of the target is mutable  |
| `i32 mut@ mut@` to `i32@@ mut`             | error  | drops level 2 behind a mutable level 1 |
| `i32 mut@` to `i32@`                       | ok     | drops level 1                          |
| `i32@` to `i32 mut@`                       | error  | adds mutability                        |

The rule closes the C `T** -> const T**` hole with a short recursive check:

```fort
i32 mut x = 1;
i32 mut* mut p = &x;                 // p is rebindable and *p is writable
i32 mut* mut* pp = &p;               // &p: level 1 is p's storage, level 2 is x
i32* mut* q = pp;                    // error: cannot drop mutability at level 2 through a
                                     //        mutable level 1
// If the line above were accepted, the next two lines would both type-check and
// together write through pp into the immutable c:
i32 c = 5;
*q = &c;                             // *q is a mutable slot holding an i32*
**pp = 7;                            // writes c through pp's i32 mut* mut* type
```

`new(node mut*, 3)` returns `node mut* mut@ own`, so binding it to `node* mut@ own t` is refused
by the same rule; declare `node mut* mut@ own t`, or write `new(node*, 3)` and get the element type
you asked for (D5.4, D10.2). Binding either to `node*@ t` fails for another reason: the owning
temporary would leak (D17.8, section 8.3).

### 7.5 Struct fields and return types

A field's own storage is exactly as mutable as the struct value that contains it: level 0 of the
field is inherited from the access path (D5.5). The outermost position of a field type therefore
never carries `mut`; the markers a field type does carry describe the levels behind its
indirections. The same holds for return types: a returned value is a temporary, so a `mut` in the
outermost position of a return type is an error.

```fort
struct node {
    i32 value;
    node mut* next;                  // *next is writable through any node, even an immutable one
    node* prev;                      // *prev is read-only
    i32 mut@ items;                  // elements writable
    u8 mut@ own name;                // an owned span; node is now an owning aggregate
}
struct b1 { i32 mut x; }             // error: 'mut' on the field's own storage
struct b2 { node* mut next; }        // error: 'mut' on the field's own storage
struct b3 { point mut p; }           // error: 'mut' on the field's own storage
struct b4 { i32[4] mut a; }          // error: an array shares the storage of its field
struct b5 { i32@ mut s; }            // error: 'mut' on the field's own storage

fn head(list l) node mut* { return l.first; }   // ok: describes level 1
fn count(list l) i32 mut { return 0; }          // error: 'mut' on a return type's own storage
fn first(list l) node* mut { return l.first; }  // error: 'mut' on a return type's own storage
fn alloc() node mut* own { return new(node); }  // ok: an owned result (section 8.3)
fn bad() i32 own { return 0; }                  // error: own on a non-reference type
```

### 7.6 Parameters

A parameter is a local copy of the argument (D8.2). `mut` on a parameter follows the placement
rule; in the outermost position it makes the callee's copy assignable (D5.6). Function-type
identity ignores that position on every parameter (D3.10), so `fn (i32) void` and
`fn (i32 mut) void` are one type.

```fort
fn total(i32@ xs, i32 mut acc) i32 {
    for (i32 x : xs) { acc += x; }
    return acc;
}
fn bump(i32 n) void { n += 1; }                  // error: n is immutable
fn step(node* mut cur) void { cur = cur->next; } // ok: rebinds the local copy only
fn set(node mut* n) void { n->value = 1; }       // ok: writes through the pointer
fn set2(node* n) void { n->value = 1; }          // error: *n is immutable
```

### 7.7 Mutability of lvalues

An lvalue (D6.7) is mutable according to the table below (D5.7). Assignment, compound assignment,
`++`, `--` and `&e` yielding a `T mut*` all require a mutable lvalue.

| Lvalue                        | Mutable when                                              |
|-------------------------------|-----------------------------------------------------------|
| variable, parameter `x`       | `x` was declared with its outermost position `mut`        |
| module constant `K`, global `g` | `g` was declared `mut`; `K` never (addressable, D7.10)  |
| `*p`, `p->f`                  | level 1 of `p`'s type is mutable                          |
| `e.f`, `e[i]` on a fixed array| `e` is a mutable lvalue                                   |
| `s[i]` on a span              | level 1 of `s`'s type is mutable                          |
| `str[i]` on a string          | never                                                     |
| `(e)`                         | as `e`                                                    |
| `s.len`, `s.ptr`, `a.len`     | not lvalues                                               |
| call result, literal          | not lvalues; `f().x` and `f()[0]` are rvalue copies       |

`move(e)` and `del(e)` are not assignments and do not need a mutable `e`; only when `e` is
reached through `*p`, `p->f` or `s[i]` must the level holding it be mutable (D17.6, section 8).

```fort
point q = {};
q.x = 1;                             // error: q is immutable
point mut r = {};
r.x = 1;
i32 mut@ own s = new(i32, 2);
s[0]++;
i32 mut@ own t = new(i32, 2);
del(t);                              // ok: t is immutable, and now {null, 0}
t = new(i32, 2);                     // error: t is immutable
string str = "ab";
str[0] = 'c';                        // error: string characters are immutable
s.len = 1;                           // error: .len is not an lvalue
make_point().x = 1;                  // error: not an lvalue
```

### 7.8 Results of `&` and `new`

`&e` requires an lvalue and has the pointer type whose level 1 is the mutability of `e` and whose
deeper levels come from `e`'s type (D5.8, D6.7); the pointer itself is never `own`, but the
`own` marks of `e`'s type stay behind it (D17.3). `new(T)` returns `T mut* own` and `new(T, n)`
returns `T mut@ own` (D5.8, D10.2, D17.3).

| Declaration          | Expression | Type                                                    |
|----------------------|------------|---------------------------------------------------------|
| `i32 x`              | `&x`       | `i32*`                                                  |
| `i32 mut x`          | `&x`       | `i32 mut*`                                              |
| `node* p`            | `&p`       | `node**`                                                |
| `node* mut p`        | `&p`       | `node* mut*`                                            |
| `node mut* mut p`    | `&p`       | `node mut* mut*`                                        |
| `i32[4] a`           | `&a`       | `i32[4]*`                                               |
| `i32[4] mut a`       | `&a`       | `i32[4] mut*`; `&a[2]` is `i32 mut*`                    |
| `i32@ s`             | `&s`       | `i32@*`                                                 |
| `i32@ mut s`         | `&s`       | `i32@ mut*`                                             |
| `i32 mut@ mut s`     | `&s`       | `i32 mut@ mut*`; `&s[0]` is `i32 mut*`                  |
| `string str`         | `&str`     | `string*`; `&str[0]` is `char*`                         |
| `point mut q`        | `&q.x`     | `i32 mut*`                                              |
| module `i32 K = 3;`  | `&K`       | `i32*` (D7.10)                                          |
| `u8 mut@ own mut buf`| `&buf`     | `u8 mut@ own mut*`: a borrowed pointer to an owned slot |
| `node mut* own mut n`| `&n`       | `node mut* own mut*`                                    |
| `node* own n`        | `&n`       | `node* own*`                                            |

```fort
point* p = new(point);               // error: owning temporary would leak (D17.8)
point mut* own mut q = new(point);
q->x = 1;
point* own w = new(point);           // ok: level 1 dropped, own kept (D5.4)
w->x = 1;                            // error: *w is immutable through w
point mut* own mut* own pq = &q;     // error: & yields a borrowed pointer
point mut* own mut* ok = &q;         // ok: 'q' is mutable, so level 1 is too
i32* r = &(1 + 2);                   // error: & requires an lvalue
```

### 7.9 The shallow model

Immutability of a variable never propagates through a pointer or span it contains; the levels
behind an indirection are fixed by the type alone (D5.9).

```fort
struct node {
    i32 value;
    node mut* next;
}
node mut other = {};
node n = node{.value = 0, .next = &other};
n.value = 1;                         // error: n is immutable
n.next = null;                       // error: the slot is as immutable as n
n.next->value = 1;                   // ok: level 1 of the field type is mutable
```

## 8. Ownership

### 8.1 The `own` qualifier

A pointer, `void*`, span or `string` type may be qualified `own` (D17.1): `T* own`,
`void* own`, `T@ own`, `string own`. The qualifier states that the reference designates the
start of a live allocation obtained from `new`, or adopted with `cast` (section 9.2), and that
`del` on it is meaningful (`core-language.md` 8.2). It changes nothing at run time: an `own`
type has the size, alignment, layout, zero value and calling convention of the unqualified type,
and the mark is erased in generated code. It is part of type identity (section 6):
`node* own` and `node*` are different types, and so are `fn (node* own) void` and
`fn (node*) void`. `own` on a scalar, a struct, a fixed array or a function-pointer type is an
error; a struct or array that contains an `own` reference is an owning aggregate instead
(section 8.5). Ownership is a typing discipline, not a linear check: a use after `move`, two
owners made with `cast`, and a leak are not diagnosed (D17.14, D15).

```fort
node mut* own n = new(node);         // an owned node
i32 own a = 1;                       // error: own on a non-reference type
point own p = {};                    // error: own on a struct; qualify a field instead
i32[4] own arr = {};                 // error: own on a fixed array
fn (i32) i32 own f = inc;            // error: an own marks a reference (D17.2)
node* b = n;                         // ok: lends (section 8.4)
node* own c = b;                     // error: cannot add own implicitly; cast adopts
```

### 8.2 Placement and levels

Each `*` and `@` suffix introduces a reference, a pointer or a span header, that is stored at
the level just outside the level the suffix reaches (section 7.2); the outermost reference is
stored at level 0, in the binding. `own` marks references, not levels (D17.2): an `own` follows
the `*` or `@` whose reference it marks as owning its target, and `string`, a reference without a
suffix (section 3.4), takes it directly (`string own name`). In a position it precedes `mut`
(`node* own mut p`), and nothing precedes the base type.

Each `own` marks one reference: in `node* mut@ own items` the span is owned and the pointers in
it are borrowed, the safe reading when the nodes belong to an arena. Because the outermost
reference is the one the binding holds, the `own` immediately before the name is what `del(x)`
requires. An `own` never follows a non-reference base type (`node own*`) nor a fixed-array suffix
(`node*[4] own`), while `node* own[4] t` is four owning pointers; a doubled marker does not
parse, so every type has exactly one spelling. Inside `new(...)` an `own` parses only after a `*`
of the element type (section 8.3). The mutability of each level is spelled independently by
section 7.3; `own` and `mut` combine freely, `own` first.

| Declaration                    | Level 0 holds             | Level 1 holds          | Level 2 |
|--------------------------------|---------------------------|------------------------|---------|
| `u8 mut@ own buf`              | owned header              | bytes                  |         |
| `u8@ own data`                 | owned header              | bytes, read-only       |         |
| `node mut* own n`              | owned pointer             | the node               |         |
| `node* own mut n`              | owned pointer, rebindable | the node, read-only    |         |
| `node* mut@ own items`         | owned header              | borrowed pointers      | nodes   |
| `node mut* own mut@ own kids`  | owned header              | owned pointers         | nodes   |
| `node* own@ view`              | borrowed header           | owned pointers, fixed  | nodes   |
| `node* own mut@ v`             | borrowed header           | owned pointers, mutable| nodes   |
| `u8 mut@ own mut* out`         | borrowed pointer          | owned header (`*out`)  | bytes   |
| `node* own[4] t`               | four owned pointers       | nodes                  |         |
| `string own name`              | owned header              | characters, never mutable|       |

The type of a derived lvalue keeps the marks of the references inside it: `kids[i]` is a
`node mut* own`, `view[i]` a `node* own`, `*out` a `u8 mut@ own`, `t[i]` a `node* own`, and
`items[i]` a `node*`. Whether `del` or `move` may empty such an lvalue is decided by the
mutability of the level holding it (D17.6): `del(kids[i])` and `del(*out)` compile,
`del(view[i])` does not.

```fort
node* own own p = null;              // error: a doubled marker does not parse
own node* q = null;                  // error: nothing precedes the base type
node own* r = null;                  // error: own marks a reference, not the base type
node* own[4] t = {};                 // ok: an owning array of four pointers (section 8.5)
node*[4] own u = {};                 // error: own never follows a fixed-array suffix
node* mut own v = null;              // error: own precedes mut in a position
i32 own w = 1;                       // error: own on a non-reference type
```

### 8.3 Producers and views

`new(T)` yields `T mut* own`, `new(T, n)` yields `T mut@ own`, `new(T[K], n)` yields
`T[K] mut@ own`, and standard-library functions that allocate return `own` (D17.3, D13.5).
`cast` may add `own` to a reference, adopting memory (section 9.2). Everything else yields a
view: a span expression, `.ptr`, `&`, literals and the runtime's `args` never produce an `own`
reference,
although the `own` marks inside the element or pointee type survive. An `own` rvalue must land
in an `own` place (a declaration, an assignment target, an `own` parameter, an `own` field or
element of a literal, a `return`) or be freed with `del`; anything else is the error "owning
temporary would leak", because nothing could free it afterwards (D17.8): converting or casting it
to a non-`own` type, taking a span of it or taking its `.ptr`, accessing a field of an owning
aggregate
rvalue (section 8.5), and discarding it as an expression statement.

| Expression                                     | Type                                   |
|------------------------------------------------|----------------------------------------|
| `new(node)`                                    | `node mut* own`                        |
| `new(u8, n)`                                   | `u8 mut@ own`                          |
| `new(i32[4], n)`                               | `i32[4] mut@ own`                      |
| `new(node*)`                                   | `node* mut* own`                       |
| `new(node*, n)`                                | `node* mut@ own`                       |
| `new(node mut*, n)`                            | `node mut* mut@ own`                   |
| `new(node mut* own, n)`                        | `node mut* own mut@ own`, slots `null` |
| `buf[..]`, `buf[lo..hi]` for `u8 mut@ own buf` | `u8 mut@`                              |
| `buf.ptr`                                      | `u8 mut*`                              |
| `&buf` for `u8 mut@ own mut buf`               | `u8 mut@ own mut*`                     |
| `kids[1..]` for `node mut* own mut@ own kids`  | `node mut* own mut@`                   |
| `kids.ptr`                                     | `node mut* own mut*`                   |
| `&n` for `node mut* own mut n`                 | `node mut* own mut*`                   |
| `"abc"`, `s[1..]`, `sys.args()`                | `string`, `string`, `string@`          |
| `str.dup(s)`, `strbuf.take(&b)`                | `string own`                           |

`new` always marks the outermost reference; an `own` after a `*` of the element type marks the
slots, so `new(node mut* own, n)` is an owned span of `n` owned slots, all `null` (D17.3), while a
count-less `new(node*)` allocates one pointer slot (D10.2). Nothing precedes the base type inside
`new(...)`, and a `mut` parses in every position of the element type but the outermost one, which
`new` fills with the storage it allocates (grammar section 6, D10.2).

```fort
u8 mut@ own mut buf = new(u8, 16);
u8 mut@ own part = buf[4..];         // error: taking a span yields a view; cannot add own
u8 mut* own p = buf.ptr;             // error: .ptr is a view; cannot add own
u8 mut@ own mut* slot = &buf;        // ok: a borrowed pointer to the owned slot
u8 mut@ own mut* own bad = &buf;     // error: & yields a borrowed pointer
u8 mut@ leak = new(u8, 8);           // error: owning temporary would leak
u8@ tmp = str.dup("x");              // error: owning temporary would leak
u8 mut@ head = new(u8, 8)[..4];      // error: owning temporary would leak
u8 mut* first = new(u8, 8).ptr;      // error: owning temporary would leak
node mut* own mut@ own kids = new(node mut* own, 2);  // ok: two null owned slots
node mut* own mut@ own bad2 = new(own node*, 2);   // error: nothing precedes the base type
del(new(u8, 8));                     // ok: the temporary is freed
```

### 8.4 Lending

`own X` converts implicitly to `X` wherever a value meets an expected type, in the same places
as the mutability drop of section 7.4, and the two drops combine (D17.4). The operands of `==`,
`!=` and `?:` are lent as well, so `own` never blocks a comparison; an `own` rvalue operand of
`==` or `!=` is refused by D17.8, and `?:` yields `own` only when both operands are `own` rvalues
or `null` (D6.2). Lending never empties the source. The drop is monotone with the shape of
section 7.4: `own` may be dropped from a reference only if, in the target type, no reference
outside it is `own` and every level between the binding and the storage holding that reference
is immutable. The first condition keeps a container from being freed while its elements are
owned by nobody; the second closes the `T** -> const T**` hole for ownership, where a borrowed
value could be stored, through the copy, into a slot the source still sees as owned. Adding
`own` requires `cast` (section 9.2), which is also the escape from the monotone rule.

| Conversion                                        | Result | Reason                          |
|---------------------------------------------------|--------|---------------------------------|
| `u8 mut@ own` to `u8 mut@`                        | ok     | drops the outer own             |
| `u8 mut@ own` to `u8@`                            | ok     | drops the own and level 1        |
| `node* own` to `node*`                            | ok     | drops the outer own             |
| `node*` to `node* own`                            | error  | adds own; adoption needs cast   |
| `node mut* own mut@ own` to `node mut* own mut@`  | ok     | drops the outer own only        |
| `node mut* own mut@ own` to `node*@`              | ok     | drops both; target level 1 fixed|
| `node mut* own mut@ own` to `node* own@ own`      | ok     | keeps both own marks            |
| `node mut* own mut@ own` to `node mut* mut@ own`  | error  | drops the inner own, keeps outer|
| `node mut* own mut@ own` to `node mut* mut@`      | error  | inner own behind mutable level 1|
| `u8 mut@ own mut*` to `u8@*`                      | ok     | drops the inner own; level 1 fixed|
| `u8 mut@ own mut*` to `u8 mut@ mut*`              | error  | inner own behind mutable level 1|
| `string own` to `string`                          | ok     | drops the outer own             |

```fort
node mut* own mut@ own kids = new(node mut* own, 2);
node mut* mut@ w = kids;                // error: cannot drop own at level 1 behind a mutable slot
// If the line above were accepted, the next two lines would both type-check and
// together put a stack address where del(kids[0]) expects an allocation:
node mut local = {};
w[0] = &local;
node*@ ro = kids;                       // ok: no slot is writable through ro
node mut* own mut@ view = kids;         // ok: the slots are still owned, and kids still owns them
node mut* mut@ own half = move(kids);   // error: the nodes would be owned by nobody
bool same = kids[0] == view[0];         // ok: both operands lend, as node mut*
node mut* own pick = flag ? new(node) : null;  // ok: own rvalues or null on both sides
node mut* mixed = flag ? new(node) : view[0];  // error: owning temporary would leak
```

### 8.5 Owning aggregates

A struct or fixed array that contains an `own` reference by value, directly or through nested
aggregates, is an owning aggregate (D17.7). It is not itself spelled with `own` (section 8.1),
but it is copied like an `own` value: copying an owning lvalue into an owning place, that is
initialization, assignment, a by-value parameter or a literal element, requires `move`, which
yields the whole value and leaves the all-zero value behind; returning a local owning value is
an implicit move; and an owning rvalue must land whole, since a field or element taken from it
would strand the rest (D17.8). `del` of an aggregate is an error, because `del` is shallow: a
struct frees its own fields, one `del` per `own` field, and a container exposes a `free`
function that does so (D13.5). Functions therefore take an owning struct by pointer, `vec*` or
`vec mut*`, and a range `for` over owning elements is an error (`core-language.md` 6.4).
Assignment to an owning aggregate is not checked field by field for live values (D17.11).

```fort
struct vec { i32 mut@ own data; u64 len; }
struct pair { vec a; vec b; }        // owning through nested aggregates
fn vec_new(u64 cap) vec {
    vec v = vec{.data = new(i32, cap), .len = 0};
    return v;                        // implicit move of a local
}
fn vec_free(vec mut* v) void { del(v->data); v->len = 0; }
vec x = vec_new(4);                  // ok: an owning rvalue lands
vec y = x;                           // error: copying owning value 'x' needs move(x)
vec mut z = move(x);                 // ok: x is now {{null, 0}, 0}
node* own[4] slots = {};             // an owning array
node* own[4] more = slots;           // error: copying owning value 'slots' needs move(slots)
del(z);                              // error: del takes a reference, not a vec
i32 mut@ own d = vec_new(2).data;    // error: owning temporary would leak
vec_free(&z);                        // ok: z.data is {null, 0} afterwards
```

### 8.6 Strings

`string own` is an owned, immutable character sequence (D17.12): `str.dup`, `str.concat` and
`strbuf.take` return it; literals, sub-strings and `sys.args()` are `string`. `del(string own)`
is legal and `del(string)` is not. The casts among `string`, `char@`, `u8@` and their `mut`
forms (section 9.2) yield `own` exactly when the target spells it (D3.14), so a string built in
a `u8 mut@ own` becomes a `string own` with `cast(move(buf), string own)`: the target says
`own`, so the `own` lvalue must be moved (D17.5), and `cast(buf, string)` lends a view instead.

```fort
string own d = str.dup("abc");       // ok
string v = d;                        // ok: lends
string w = d[1..];                   // ok: a sub-string is a view
string own e = "";                   // error: a literal is not owned; write {}
string own z = {};                   // ok: the zero string, owned and empty
del(v);                              // error: 'v' is a string, not a string own
del(d);                              // ok: d is now {null, 0}
u8 mut@ own buf = new(u8, 3);
string peek = cast(buf, string);     // ok: lends; buf still owns the bytes
string own t3 = cast(buf, string own);        // error: copying own lvalue 'buf' needs move(buf)
string own t = cast(move(buf), string own);   // ok: the target says own; buf is emptied
string own t2 = cast(d, string);     // error: cannot add own; cast(d, string) only lends
```

## 9. Conversions

### 9.1 Implicit

The implicit conversions are dropping mutability under the rule of section 7.4 and dropping
`own` under the rule of section 8.4 (D3.14, D5.4, D17.4); the two combine. They apply to
initialization, assignment, argument passing and `return`. Nothing else converts
implicitly: not integer widths, not signedness, not integer to float, not array to span, not
`char@` to `string`. Untyped constants are not converted; they take a type from context
(section 10).

```fort
i32 a = 1;
i64 b = a;                           // error: no implicit widening
f64 f = a;                           // error: no implicit integer to float
i32[4] arr = {};
i32@ s = arr;                        // error: write arr[..]
u8 mut@ own b2 = new(u8, 2);
u8@ v = b2;                          // ok: lends, dropping own and mutability
u8@ own o = v;                       // error: cannot add own implicitly
```

### 9.2 `cast`

`cast(expr, Type)` is the explicit conversion (D6.4, D3.14). It is a keyword form so the parser
never has to decide whether a parenthesized name is a type. Casts never trap. An untyped operand
first takes its default type (D4.5) and is then converted with the runtime semantics below
(D4.1, D4.4). The complete matrix follows; any pair not listed as allowed is a compile error.

| Source              | Target                   | Semantics                                     |
|---------------------|--------------------------|-----------------------------------------------|
| integer             | wider integer            | sign-extend if source signed, else zero-extend|
| integer             | narrower integer         | keep the low bits                             |
| integer             | same width, other sign   | reinterpret the bits                          |
| integer             | float                    | round to nearest even                         |
| float               | integer                  | truncate toward zero, saturate, NaN gives 0   |
| `f32`               | `f64`                    | exact                                         |
| `f64`               | `f32`                    | round to nearest even, overflow gives infinity|
| `bool`              | integer                  | 0 or 1                                        |
| integer             | `bool`                   | error                                         |
| `char`              | integer                  | byte value 0 .. 255, then integer to integer  |
| integer             | `char`                   | keep the low byte                             |
| `char`              | `bool`, float, enum      | error                                         |
| enum                | integer                  | the `i32` value, then integer to integer      |
| integer             | enum                     | integer to `i32`; unchecked, no member needed |
| enum                | enum, `bool`, float      | error (go through an integer)                 |
| `bool`              | float, `char`, enum      | error                                         |
| float               | `bool`, `char`, enum     | error                                         |
| `T*`                | `U*`, any `own`          | reinterpret the address; may add `own`        |
| `T*`, `void*`       | `void*`, `U*`            | reinterpret the address; may add `own`        |
| `T mut*`            | any target above         | the same, and the `mut` may be dropped        |
| any pointer         | a target that adds `mut` | error: a cast never adds `mut`                |
| `T*`, `void*`       | `u64`                    | the address as an integer                     |
| `u64`               | `T*`, `void*`            | the integer as an address                     |
| `u64`               | `T mut*`, `void mut*`    | error: an integer marks no level `mut`        |
| pointer             | other integer types      | error                                         |
| function pointer    | `void*`                  | reinterpret                                   |
| `void*`             | function pointer         | reinterpret                                   |
| function pointer    | `u64`, other fn type     | error (go through `void*`)                    |
| `string`            | `char@`, `u8@`           | reinterpret the header                        |
| `string`            | `char mut@`, `u8 mut@`   | error: the bytes of a `string` are immutable  |
| `char@`, `u8@`      | `string`, each other     | reinterpret the header                        |
| `char mut@`, `u8 mut@` | `string`, `char@`, `u8@`, each other | reinterpret; drops `mut`  |
| `char@`, `u8@`      | `char mut@`, `u8 mut@`   | error: a cast never adds `mut`                |
| `T mut@`            | `T@`                     | drop mutability at every level                |
| `T@`                | `T mut@`                 | error: a cast never adds `mut`                |
| `T@`, `T*`, string family | the same with `own` added at any reference | adoption; no check   |
| `own` reference     | same, `own` dropped at any level | lends; refused on an `own` rvalue     |
| `own` rvalue        | any `own` target above   | transfer: the result is `own`                 |
| `own` lvalue        | any `own` target above   | error unless `cast(move(x), T)` (D17.5)       |
| `T mut*`            | `T*`                     | drop mutability, as the implicit conversion   |
| span                | other element type       | error: `len` counts elements of one type      |
| pointer             | span                     | error (use `p[lo..hi]`)                       |
| span                | pointer                  | error (use `.ptr`)                            |
| fixed array         | anything                 | error (use `a[..]`)                           |
| struct              | anything                 | error                                         |
| anything            | fixed array, struct      | error                                         |
| any `T`             | `T`                      | identity                                      |

**A cast never adds `mut`, from any source.** The target marks a level `mut` only where the
source marks the level at the same depth `mut` too. Where the source names no such level -- an
integer, the `void` behind a `void*`, a `string`, or a pointee type the cast reinterprets -- the
target marks no level below that point. So `cast(cast(p, void*), u8 mut*)` fails at the second
cast, and the storage a C function gives is writable only where its `extern` says `void mut*`
(D3.14, D17.13). What a cast still does with a mark is drop it, at any level, which the implicit
conversion of 8.4 refuses behind a mutable level.

A `mut` in the outermost position of a cast target is an error, a cast result having no binding,
so `cast(s, u8@ mut)` does not compile; the markers a target does carry name the levels behind
its indirections (D3.14). `null` is not a valid cast
operand, because it has no type of its own (D10.5).

Ownership in casts (D3.14, D17.3, D17.12): the result of a cast is `own` exactly when its target
type says `own`. A cast may add `own` to any reference of a pointer or span type, adopting
memory that came from C, which is the one unsafe mark a cast adds (a later `del` of adopted
memory that is not the start of an allocation is undefined behavior, D10.7); it may drop `own` at
any level, including where the implicit drop of section 8.4 refuses, and then lends: the result is a
view and the source keeps ownership. An `own` rvalue cast to an `own` target transfers. An `own`
lvalue cast to an `own` target is a copy into an `own` place and must be written
`cast(move(x), T)` (D17.5), which empties `x`. A `cast` to an `own` type yields an `own` rvalue,
which must land (section 8.3), and a cast that drops `own` from an `own` rvalue is refused
(D17.8).

```fort
i64 w = cast(cast(-1, i8), i64);     // -1: sign-extended because i8 is signed
u64 z = cast(cast(-1, i8), u64);     // 18446744073709551615: the i8 is sign-extended to u64
u8 t = cast(300, u8);                // 44: 300 is i32 by default, low byte kept
i32 m = cast(0x80000000, i32);       // -2147483648: the literal is i64, truncated
u32 u = cast(-1, u32);               // 4294967295: same width, bits reinterpreted
f32 f = cast(16777217, f32);         // 16777216.0: rounded to nearest even
i32 s1 = cast(1e10, i32);            // 2147483647: saturated
i32 s2 = cast(-3.99, i32);           // -3: truncated toward zero
u8 s3 = cast(-1.0, u8);              // 0: saturated
i32 s4 = cast(math.f64_nan(), i32);  // 0: NaN (std.math provides NaN, D6.12)
i32 b = cast(true, i32);             // 1
bool x = cast(1, bool);              // error: no cast from integer to bool
i32 c = cast('A', i32);              // 65
char d = cast(0x1F600, char);        // '\x00': low byte kept
color e = cast(2, color);            // holds 2, no check
shape g = cast(color.red, shape);    // error: no cast between enums
i32 mut* p = cast(&k, i32 mut*);     // error unless k is mutable: a cast never adds mut
u64 a = cast(p, u64);
i32 n = cast(p, i32);                // error: pointer casts only to u64
u8 mut@ bytes = cast("abc", u8 mut@);   // error: a literal is read-only and a cast adds no mut
u8@ ro = cast("abc", u8@);              // ok
i8@ sb = cast(ro, i8@);                 // error: element type of a span never changes
i32@ q = cast(p, i32@);                 // error: no cast from pointer to span
point pt = cast(r, point);           // error: no struct casts
u8 mut* own m = cast(libc.malloc(64), u8 mut* own);    // own rvalue to own type; del(m) frees it
u8 mut@ own got = cast(p2[0..n], u8 mut@ own);         // adopts C memory at a u8 mut* p2
node mut* own mut@ own kids = new(node mut* own, 8);   // owned slots (section 8.3)
node mut* mut@ esc = cast(kids, node mut* mut@);       // ok: drops own where 8.4 refuses
string own t = cast(move(buf), string own);            // the target says own; buf is emptied
string own t2 = cast(buf, string own);                 // error: copying own lvalue 'buf' needs move
string t5 = cast(buf, string);                         // ok: lends; buf still owns the bytes
string t3 = cast(new(u8, 4), string);                  // error: owning temporary would leak
string own t4 = cast("abc", string own);               // compiles; del(t4) is undefined
```

## 10. Untyped constants and constant expressions

### 10.1 Untyped constants

Integer, float and character literals are untyped constants (D4.1). An untyped constant has no
type of its own; it takes one from context:

- the declared type of the variable being initialized or assigned;
- the type of the other operand of a binary operator, except that the count operand of a shift
  is never a context for the shifted operand (section 10.4);
- the parameter type in a call, the return type in `return`, the operand type in a `case` label;
- an index, span-bound or `new` count position, where any integer type is accepted and a
  negative constant is a compile error (D4.1, D6.8, D6.9, D10.2).

`cast(c, T)` is not a context: an untyped `c` takes its default type first (D4.1, D4.5).

```fort
i64 a = 1;                           // 1 is i64
u8 b = 1;                            // 1 is u8
f64 c = 1;                           // 1.0
i32 d = 3 + a;                       // error: 3 becomes i64, and i64 is not i32
```

### 10.2 Contextual conversion is checked (D4.2)

An untyped integer may become any integer type it fits in, or any float type (rounded to nearest
even). An untyped float may become only a float type, never an integer type, even when its value
is integral.

```fort
u8 ok = 255;
u8 e1 = 256;                         // error: 256 does not fit u8
u32 e2 = -1;                         // error: -1 does not fit u32
f32 ok2 = 2;                         // 2.0
i32 e3 = 2.0;                        // error: a float constant never becomes an integer
f32 e4 = 1e39;                       // error: not finite in f32 (D4.4)
```

### 10.3 Character literals (D4.3)

A character literal's default type is `char`; in an integer context it takes that integer type.
An integer literal never becomes `char` implicitly.

```fort
char c = 'a';
u8 b = 'a';                          // 97
i32 d = cast(c, i32) - '0';          // '0' becomes i32
char e = 65;                         // error: use cast(65, char)
```

### 10.4 Folding among untyped constants (D4.4)

Operators applied to untyped constants fold at compile time. Integer with integer stays an
untyped integer, so `1 / 2` is `0`; integer with float becomes an untyped float; `~c` on an
untyped integer is `-c - 1`; constant `/` and `%` truncate toward zero exactly as at run time
(D6.13), so `-7 / 2` is `-3` and `-7 % 2` is `-1`. `&`, `|` and `^` operate on the infinite
two's-complement extension of their operands, which is Go's rule, so `-1 & 0xFFFFFFFFFFFFFFFF` is
`18446744073709551615` and only `^` can leave the range; a shift folds exactly too, `<<` as
a multiplication and `>>` as a floor division (`-3 >> 1` is `-2`), with a count in `0..63`
(D4.4). Untyped integers are evaluated exactly in `[-2^63, 2^64 - 1]`; an intermediate outside
that range, a shift count outside `0..63`, and division by zero, are compile errors. Untyped
floats are evaluated as `f64`; an `f32` constant is the rounding of that `f64` value. The shifted
operand of a shift takes its type from the context of the whole shift expression, never from the
count.

```fort
f64 h = 1 / 2;                       // 0.0: integer division folded first
f64 k = 1.0 / 2;                     // 0.5
i32 q = 1 << 40 >> 38;               // 4: folded exactly, then fits i32
u32 m = ~0;                          // error: ~0 is -1, which does not fit u32
u32 m2 = 0xFFFFFFFF;                 // ok
i32 z = 1 / 0;                       // error: constant division by zero
u64 big = 1 << 64;                   // error: constant shift count must be in 0..63
u64 sh = 1 << n;                     // 1 is u64 from the declaration, whatever n's type
i32 mixed = 1 + 0.5;                 // error: 1.5 is a float constant
f64 mixed2 = 1 + 0.5;                // 1.5
```

### 10.5 Default types (D4.5)

With no context at all (an argument to `print`, an operand of `cast`, the operand of a `switch`,
the shifted operand of a shift in no other context), an untyped integer becomes `i32` if it
fits, otherwise `i64`, otherwise it is an error; an untyped float becomes `f64`; a character
literal becomes `char`. `null` has no default type: it takes the type of a pointer it is
initialized into, assigned to, passed as, returned as, or compared with.

An untyped expression that folded to no value takes a default type too, and "if it fits" then
reads on every constant the type reaches (D4.5, the note of 2026-09-13). The expression becomes
`i32` when all of them fit `i32`, and `i64` when one of them does not. A node that folded stands
for its whole subtree, here as in section 10.4, so the value of that node decides and the
constants below it are not read again.

The three clauses answer together, because one type covers the whole expression (D6.2). The clause
that wins is the strongest any constant of the expression asks for, in this order: the float
clause, then the character clause, then the integer clause (D4.5, the note of 2026-09-14). The
integer clause wins over the character clause because a character literal takes an integer type in
an integer context while an integer constant never becomes `char` (D4.3). Both win over the float
clause, because D4.2 permits an untyped integer to take a float type in a float context and no
decision requires it where there is no context at all.

The type the clause chooses is not always a type every constant can take. D4.2 and D4.3 decide
that one constant at a time, and a constant that cannot take it is the error: `c ? 1 : 2.5` is an
`i32` whose float constant is refused, and `c ? 'a' : 1.5` is a `char` whose float constant is
refused. Folding comes first and is not this rule: `1 + 2.5` is the untyped float 3.5 (D4.4).

The rule chooses the type and the operand rules then hold against it, at the point a context fixes
it (D6.2). The operands of an expression that folded to no value carry no value, so the operand
rules cannot read their kind where the operator stands. A constant that does not fit the type is
reported first, so `char c = 1 << n;` names the integer constant `1` and not the operand rule. Two
expressions fold to no value: a shift whose count is a variable, and a `?:` whose condition is a
run-time value. A shift takes an integer left operand, so the `char` and the `f64` of this section
are errors inside one, and only a `?:` shows those two clauses at run time.

```fort
print(7);                            // i32
print(3000000000);                   // i64
print(1 << 63);                      // error: 9223372036854775808 does not fit i64
print(1.5);                          // f64
print('a');                          // char: prints a
print(null);                         // error: null has no type here
switch (2) { case 2: }               // the operand is i32 (D7.6)
print(1 << n);                       // i32: the count is a variable, and 1 fits i32
print(4294967296 << n);              // i64: 4294967296 does not fit i32
print((2147483648 - 1) << n);        // i32: the operand folded to 2147483647
print(9223372036854775808 << n);     // error: 9223372036854775808 does not fit i64
print(c ? 'a' : 'b');                // char: prints a
print(c ? 1.5 : 2.5);                // f64
print(c ? 'a' : 98);                 // i32: an integer constant never becomes char
print(c ? 1 : 2.5);                  // error: a float constant does not become i32
print(c ? 'a' : 1.5);                // error: a float constant does not become char
print(1.5 + (1 << n));               // error: a float constant does not become i32
print('a' << n);                     // error: '<<' takes an integer left operand, not char
print((c ? 1.5 : 2.5) << 1);         // error: '<<' takes an integer left operand, not f64
print((c ? 1.5 : 2.5) & (c ? 0.5 : 1.5)); // error: '&' takes integer operands, not f64
char x = 1 << n;                     // error: an integer constant does not become char
```

### 10.6 Constant expressions (D4.6)

Array lengths, `case` labels, enum values and module-level initializers (D7.10) require constant
expressions. A constant expression is one of:

- a literal, `true`, `false`, `null`;
- a module-level immutable declaration with a constant initializer, from any module;
- an enum member;
- `sizeof(T)`;
- `.len` of an expression of fixed-array type (only the type is used; the expression is not
  evaluated, so `s[i].len` is constant when `s` is an `i32@[4]`);
- unary `- ! ~`; binary `+ - * / % +% -% *% & | ^ << >> < <= > >= == != && ||`; `?:`;
- `cast` whose source and target are each an integer or float type, `char` or an enum, so
  `cast(color.blue, i32) + 1` may size an array;
- a parenthesized constant expression;
- a struct or array literal whose leaves are constant expressions.

Not constant: calls, `&` (except `&` of a module-level declaration inside a module-level
initializer, D7.10), indexing, span expressions, field access, `.len` of a span or string, reads of
`mut` globals, `cast` involving `bool`, pointers or spans (`cast` of `null` is an error
outright, D10.5), and a `?:` whose condition is not constant.

```fort
i32 N = 4;                           // module level: a constant, addressable, read-only
i32[N * 2] buf = {};                 // ok: 8
i32[buf.len + 1] more = {};          // ok: 9
i32 mut g = 4;
i32[g] bad = {};                     // error: reads a global mut
i32[buf[0]] bad2 = {};               // error: indexing is not constant
i32[cast(color.blue, i32) + 1] ok3 = {};   // ok: 7, with blue == 6 from section 4.2
i32[cast(true, i32)] bad3 = {};      // error: cast of a bool is not constant
string S = "abc";
i32[S.len] bad4 = {};                // error: .len of a string is not constant
```

Typed constant folding respects the declared type: once a constant has a type, every operation
on it is checked as it would be at run time in checked mode, and a failure is a compile error
rather than a trap.

```fort
i32 A = 2147483647;
i32 B = A + 1;                       // error: overflow folding i32 constant
i32 C = A +% 1;                      // -2147483648: wrapping operators fold by wrapping
u8 D = 200;
u8 E = D * 2;                        // error: 400 does not fit u8
```

Constants and struct sizes are resolved lazily, in dependency order, with cycle detection; a cycle
is an error (D4.6, D7.10).

```fort
i32 X = Y + 1;
i32 Y = 2;                           // ok: Y is resolved before X is needed
i32 P = Q;
i32 Q = P;                           // error: constant P depends on itself
struct s1 { s2 s; }
struct s2 { s1 s; }                  // error: struct s1 has infinite size
```

## 11. `sizeof`, layout and ABI

### 11.1 `sizeof`

`sizeof(Type)` takes a type, never an expression, and yields an untyped integer constant
(D3.15). `sizeof(void)` is an error. There is no `alignof` (D15); the alignments below are what
the compiler uses.

| Type                       | `sizeof`             | Alignment        | Note                     |
|----------------------------|----------------------|------------------|--------------------------|
| `i8`, `u8`, `bool`, `char` | 1                    | 1                |                          |
| `i16`, `u16`               | 2                    | 2                |                          |
| `i32`, `u32`, `f32`, enum  | 4                    | 4                | enums are `i32` (D3.9)   |
| `i64`, `u64`, `f64`        | 8                    | 8                |                          |
| `T*`, `void*`, `fn (P) R`   | 8                    | 8                |                          |
| `T[N]`                     | `N * sizeof(T)`      | that of `T`      | elements are contiguous  |
| `T@`, `string`             | 16                   | 8                | `{ptr, len}` (D3.5, D3.7)|
| struct                     | fields plus padding  | largest field    | rounded up; matches C    |
| `void`                     | error                |                  |                          |

```fort
u64 a = sizeof(i32[3][4]);           // 48
u64 b = sizeof(node*[16]);           // 128
u64 c = sizeof(string);              // 16
u64 d = sizeof(fn (i32) i32);         // 8
u64 e = sizeof(rec);                 // 24, from section 4.1
u64 h = sizeof(u8 mut@ own);         // 16: own changes no size (D17.1)
u64 f = sizeof(x);                   // error: sizeof takes a type, not an expression
u64 g = sizeof(void);                // error
```

### 11.2 Passing and returning

Semantically every parameter and result is passed by value (D8.2): scalars and reference values
copy the scalar or the fat pointer; structs and fixed arrays copy the whole value. The internal
calling convention realizes this as follows (D9.9). This table shows Linux x86-64 registers:

| Value kind                                    | Passed                        | Returned        |
|-----------------------------------------------|-------------------------------|-----------------|
| integers, `bool`, `char`, enums, all pointers | integer registers (System V)  | `rax`           |
| `f32`, `f64`                                  | SSE registers (System V)      | `xmm0`          |
| struct, fixed array, span, `string`           | pointer to a caller-made copy | hidden pointer  |

Mac arm64 uses `x0` to `x7` for scalar integers and pointers, and `v0` to `v7` for floats.
It returns scalars in `x0` or `v0`. It uses `x8` for a fort aggregate result pointer.
The call site marks that pointer `sret(%T)` on Mac (D9.9).

The Linux registers are what System V assigns; the compiler expresses the convention in LLVM IR as
ordinary scalar parameters, a plain `ptr` parameter for an aggregate argument and a leading
`ptr sret(%T)` parameter for an aggregate result. LLVM assigns the target registers
(`toolchain.md` 6 item 7).

Struct layout is identical to the C layout of the same declaration, so passing `&s` to C works.
A fort function is usable as a C callback, and an `extern` function may be declared, exactly when
every parameter and the result are integers, floats, `bool`, `char`, enums (passed as `i32`),
pointers or function pointers whose own signature is extern-legal in turn; spans, strings,
structs and arrays never cross an `extern` boundary (D9.8). Narrow integers and `bool` are zero-
or sign-extended on both sides of the boundary; C `char*` maps to `char*` or `u8*`, C `size_t`
to `u64` (D9.8). `own` may appear in
an `extern` signature (D17.13): it is erased like everywhere else and documents the C side's
convention, so a fort caller must `move` into an `own` parameter and must land an `own` result.
A `void*` result carries the marks the C function's contract states: `malloc` answers storage the
caller may write, so it is `void mut* own`, and `free` takes `void* own`, which every caller
reaches by dropping marks (D3.11, D5.4). Signature identity includes `own` and the level-1
mutability (D9.8): two modules declaring one C symbol with and without either conflict.

```fort
extern fn write(i32 fd, u8* buf, u64 n) i64;       // C means bytes here, so fort says bytes
extern fn sort(i32@ xs) void;        // error: spans cannot cross an extern boundary
extern fn malloc(u64 n) void mut* own;
extern fn free(void* own p) void;
void mut* own raw = malloc(16);      // ok: the owned result lands
free(raw);                           // error: 'raw' is an own lvalue; write move(raw)
free(move(raw));                     // ok; del(raw) would have done the same (D10.3)
void* lost = malloc(16);             // error: owning temporary would leak
```

## 12. Not in v1

Generics, unions (tagged or untagged), `Result`, methods, closures, overloading, default and
named arguments, type aliases, struct, array and span equality, alignment and packed attributes,
`alignof`, `sizeof(expr)`, string `switch` and linear ownership (compile-time detection of leaks
and of use after `move`, of which section 8 is the intended base, D17.14) are deferred; D15
lists each with the idiom to use instead (a fat struct with a kind field for unions, `bool` plus
out-parameters for results, an opaque `u8[N]` field with a C shim for alignment, `defer del`
for ownership). An array suffix after a trailing reference suffix does not parse in v1; wrap the
reference in a struct (D3.6).
