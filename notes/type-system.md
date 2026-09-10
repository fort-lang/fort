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
| pointer          | `T*`              | reference     | 8             | `null`      | D3.12    |
| opaque pointer   | `void*`           | reference     | 8             | `null`      | D3.11    |
| function pointer | `fn R(P1, P2)`    | reference     | 8             | `null`      | D3.10    |
| fixed array      | `T[N]`            | aggregate     | `N*sizeof(T)` | all zero    | D3.4     |
| slice            | `T[]`             | reference     | 16            | `{null, 0}` | D3.5     |
| string           | `string`          | reference     | 16            | `{null, 0}` | D3.7     |
| struct           | `Name`            | aggregate     | fields, padded| all zero    | D3.8     |
| enum             | `Name`            | scalar        | 4             | `0`         | D3.9     |

The three kinds differ in what a copy means:

- A **scalar** is copied as a machine word; the copy is independent of the original.
- An **aggregate** (struct, fixed array) is a value: assignment, argument passing and `return`
  copy every field or element (D3.4, D3.8, D8.2).
- A **reference** value (pointer, `void*`, function pointer, slice, `string`) is itself copied by
  value, but the copy designates the same target storage as the original. Two slices obtained from
  one `new` alias the same elements; two pointers to one variable alias that variable.

The zero value is what `= {}` produces for aggregates, slices and strings (D6.5), what `new`
fills allocations with (D10.2), and what a zeroed enum or scalar field holds.

`noreturn` is a return type, not a type (D8.5): `noreturn x = ...;` does not parse.

A type additionally carries one mutability bit per storage level behind an indirection
(section 7). The binding's own mutability (level 0) is a property of the declaration, not of the
type (D3.12): `i32 x` and `mut i32 x` hold values of the same type `i32`.

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

Alignment equals size for every primitive (D3.1). Raw bytes are `u8`; there is no other
one-byte integer type. Integers are two's complement; `>>` is arithmetic on signed and logical
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
`cast` to and from every integer type, and nothing else. Ordering compares byte values.
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
Node* p = null;
while (p) { }                        // error: condition must be bool, write p != null
bool c = cast(1, bool);              // error: no cast from integer to bool
bool d = b < true;                   // error: bool has no ordering
```

### 2.3 `void`

`void` names the absence of a value. It appears only as the return type of a function and as the
base of `void*` (D3.1, D3.11).

```fort
fn void log(string s) { }
void v = log("x");                   // error: void is not a value type
void[4] a = {};                      // error: void is not an element type
u64 n = sizeof(void);                // error: sizeof(void) (D3.15)
```

## 3. Arrays, slices and strings

### 3.1 Fixed arrays `T[N]`

`T[N]` holds exactly `N` elements of `T` contiguously (D3.4). `N` is a constant expression
(D4.6) greater than 0. Arrays of different lengths are different types. A fixed array is a
value: assignment, argument passing and `return` copy all `N` elements (D8.2). `a.len` is an
untyped integer constant equal to `N` (D3.4, D4.6). A fixed array has no `.ptr`; obtain a
pointer to an element with `&a[i]` or a slice with `a[lo..hi]` (D6.9). A constant index that is
out of range is a compile error; any other index is checked at run time (D6.8).

```fort
i32[4] a = {1, 2, 3, 4};
i32[4] b = a;                        // copies four elements; b and a are independent
i32[3] c = a;                        // error: i32[4] is not i32[3]
i32[0] z = {};                       // error: array length must be greater than 0
u64 n = a.len;                       // 4, an untyped constant
i32* p = a.ptr;                      // error: fixed arrays have no .ptr
i32 x = a[4];                        // error: constant index 4 out of range for i32[4]
i32[4]* q = &a;                      // error: pointer to array is not expressible (D3.6)
bool same = a == b;                  // error: no == on fixed arrays (D3.13)
```

Array literals are `i32[3]{1, 2, 3}` anywhere, `{1, 2, 3}` as the initializer of a declaration of
array type, and `{}` for all-zero (D6.5). A literal must supply exactly `N` elements or be empty.

### 3.2 Slices `T[]`

A slice is a fat pointer `{T* ptr; u64 len}` designating `len` elements of `T` that live
elsewhere; it owns nothing (D3.5). All slices with the same element type and the same element
mutability are one type. The zero value is `{null, 0}`. Slices come from `new(T[n])` (D10.2),
from slicing an array, slice, string or pointer (D6.9), from the runtime (`string[] args`, D8.6)
and from the zero initializer `{}`. There is no slice literal.

`.len` (type `u64`) and `.ptr` are read-only pseudo-fields (D3.5). The type of `s.ptr` is the
slice type with its last `[]` suffix, and any `mut` written after it, replaced by `*`; the pointer's
target has the mutability of the slice's elements:

| Slice type      | `.ptr` type   | Slice type      | `.ptr` type                            |
|-----------------|---------------|-----------------|----------------------------------------|
| `i32[]`         | `i32*`        | `Node* mut[]`   | `Node* mut*`                           |
| `mut i32[]`     | `mut i32*`    | `mut Node*[]`   | `mut Node**`                           |
| `i32[] mut`     | `i32*`        | `string[]`      | `string*`                              |
| `fn i32(i32)[]` | `fn i32(i32)*`| `i32[][4]`      | error: pointer to `i32[4]` (D3.6)      |

```fort
mut i32[] s = new(i32[8]);           // eight zeroed elements, all levels mutable
i32[] t = s;                         // same storage; element mutability dropped (D5.4)
u64 n = s.len;                       // 8
mut i32* p = s.ptr;
i32[] e = {};                        // {null, 0}
i32[] u = {1, 2, 3};                 // error: there is no slice literal
s.len = 4;                           // error: .len is not an lvalue (D6.7)
bool same = s == t;                  // error: no == on slices (D3.13)
```

### 3.3 Reading type suffixes

Suffixes follow the base type (grammar section 4). `*` binds tighter than `[...]`: a `*` chain is
read inside-out (`Node**` is a pointer to a `Node*`), and array or slice suffixes are read
outside-in like C declarators (`i32[3][4]` is three arrays of four) (D3.6). Suffixes after a
function type apply to the function type. Pointer-to-array and pointer-to-slice cannot be written;
wrap such a value in a struct (D3.6).

| Type              | Reads as                                              | `sizeof` |
|-------------------|-------------------------------------------------------|----------|
| `Node*[16]`       | array of 16 pointers to `Node`                        | 128      |
| `Node**`          | pointer to a pointer to `Node`                        | 8        |
| `i32[3][4]`       | 3 arrays of 4 `i32`; `a[i][j]` with `i < 3`, `j < 4`  | 48       |
| `i32[][4]`        | slice whose elements are `i32[4]`                     | 16       |
| `i32[4][]`        | array of 4 slices of `i32`                            | 64       |
| `i32[][]`         | slice whose elements are slices of `i32`              | 16       |
| `Node* mut[]`     | slice of mutable slots, each holding a `Node*`        | 16       |
| `fn i32(i32)[4]`  | array of 4 pointers to functions `fn i32(i32)`        | 32       |
| `fn i32[4](i32)`  | pointer to a function taking `i32`, returning `i32[4]`| 8        |
| `fn i32(i32)*`    | pointer to a slot holding a function pointer          | 8        |
| `void*[2]`        | array of 2 opaque pointers                            | 16       |
| `i32[4]*`         | error: pointer to array is not expressible            |          |
| `i32[]*`          | error: pointer to slice is not expressible            |          |
| `i32[4] mut`      | error: `mut` never follows a fixed-array suffix       |          |
| `i32 mut*`        | error: `mut` precedes the base type or follows a suffix|         |

`new(i32[n][4])` returns `mut i32[][4]`: a slice of `n` rows of four (D3.6, D10.2).

### 3.4 `string`

`string` is a distinct type with the layout of an immutable slice of `char`,
`{char* ptr; u64 len}` (D3.7). String literals have type `string` and are stored in read-only
memory followed by one NUL byte that `len` does not count; a literal's `.ptr` can therefore be
handed to C directly. Sub-strings are not NUL-terminated. Indexing yields `char`, slicing yields
`string`, `.len` is `u64` and `.ptr` is `char*`. `==` and `!=` compare `len` first and then the
bytes, so the zero string `{null, 0}` equals `""`; only `.ptr == null` tells them apart (D3.7,
D10.5). Bytes are UTF-8 by convention and never validated. There is no `+`; `std::strbuf`
concatenates (D3.7, D13.2). Characters of a string are never mutable (D5.2).

```fort
string s = "hello";                  // storage: h e l l o NUL; s.len == 5
char c = s[1];                       // 'e'
string t = s[1..3];                  // "el", shares s's bytes, not NUL-terminated
char* p = s.ptr;
string z = {};                       // {null, 0}
bool eq = z == "";                   // true: both have len 0
bool dist = z.ptr == "".ptr;         // false
s[0] = 'j';                          // error: string characters are immutable
string u = s + t;                    // error: no + on string, use std::strbuf
u8[] raw = s;                        // error: string is distinct from u8[], use cast
```

## 4. Structs and enums

### 4.1 Structs

`struct Name { T1 f1; T2 f2; }` declares a nominal type with no trailing semicolon (D3.8). Fields
are laid out in declaration order with natural alignment and the size is rounded up to the
struct's alignment (the largest field alignment), exactly as the System V C ABI lays out the same
C struct. A struct has no methods and no inheritance. An empty struct is an error. A struct may
contain itself only through a pointer or a slice; containment by value is an "infinite size"
error (D3.8, D7.10). Structs are values (section 1) and support neither `==` nor `!=` (D3.13).

```fort
struct Rec {
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
| total   |        | 24   | `sizeof(Rec) == 24`, alignment 8         |

```fort
struct Empty { }                     // error: empty struct
struct Loop { i32 v; Loop next; }    // error: struct Loop has infinite size
struct List { i32 v; List* next; }   // ok: self-reference through a pointer
struct Tree { i32 v; Tree[] kids; }  // ok: self-reference through a slice
Rec a = Rec{1, 2, 3, 4.0};           // positional: every field, in order
Rec b = Rec{.x = 1.0, .tag = 7};     // designated: any order, omitted fields zero
Rec c = {};                          // all-zero
Rec d = Rec{1, .n = 2};              // error: positional and designated mixed
bool same = a == b;                  // error: no == on structs (D3.13)
```

Field mutability follows the struct that contains the field; a `mut` in a field type describes
only the levels behind the field's indirections (D5.5, section 7.5).

### 4.2 Enums

`enum Color { Red, Green = 5, Blue }` declares a nominal type whose underlying type is `i32`
(D3.9). Members are scoped: `Color.Red` everywhere, including `case` labels, and `m.Color.Red`
for an enum from module `m` (D9.4). Enum members are not in the module namespace (D7.9). Values
start at 0 and each member is one more than its predecessor; an explicit value is a constant
expression converted to `i32` (D4.2) that may not refer to the enum itself; duplicate values are
errors. Enums support `==`, `!=`, `switch` and `cast` to and from every integer type; an
integer-to-enum cast is unchecked. There are no ordering operators. A zeroed enum holds 0 even if
no member has that value.

```fort
enum Color { Red, Green = 5, Blue }  // Red 0, Green 5, Blue 6
Color c = Color.Red;
Color d = Red;                       // error: enum members are scoped, write Color.Red
i32 v = cast(Color.Blue, i32);       // 6
Color e = cast(7, Color);            // holds 7; no member matches; not an error
bool lt = Color.Red < Color.Blue;    // error: enums have no ordering
Color f = 0;                         // error: an integer never becomes an enum implicitly
enum E1 { A, B = 0 }                 // error: duplicate value 0
enum E2 { A = 1, B = A + 1 }         // error: value refers to the enum being declared
enum E3 { A = 2147483647, B }        // error: 2147483648 does not fit i32
```

A `switch` over an enum without `default` must list every member (D7.7):

```fort
switch (c) {
case Color.Red, Color.Green:
    println("warm");
}                                    // error: switch over Color does not handle Blue
```

## 5. Function types and `void*`

### 5.1 Function types

A function type is written `fn R(P1, P2)` with parameter types only (D3.10, D8.1). A function
name used as a value has its function type. Identity is structural over the parameter types
including the mutability levels behind their indirections, the return type, and whether the
function is `noreturn`; `mut` at level 0 of a parameter is ignored (D3.10, D5.6). `null` is a
valid value; calling it is undefined behavior (D10.7). `==` and `!=` compare identity.

| Types compared                          | Same? | Reason                                  |
|-----------------------------------------|-------|-----------------------------------------|
| `fn void(i32)` and `fn void(mut i32)`   | yes   | level 0 of a parameter is ignored       |
| `fn void(Node*)` and `fn void(Node* mut)`| yes  | level 0 of a parameter is ignored       |
| `fn void(Node*)` and `fn void(mut Node*)`| no   | level 1 differs                         |
| `fn i32()` and `fn void()`              | no    | return type differs                     |
| `fn noreturn()` and `fn void()`         | no    | `noreturn` is part of the type          |

```fort
fn i32 add(i32 a, i32 b) { return a + b; }
fn i32(i32, i32) op = add;
i32 r = op(2, 3);                    // 5
bool same = op == add;               // true
fn i32(i32, i32) n = null;
fn i32(i32, i32) p = &add;           // error: & on a function name (D3.10)
i32 s = (*op)(1, 2);                 // error: function pointers cannot be dereferenced
fn i64(i32, i32) q = add;            // error: fn i32(i32, i32) is not fn i64(i32, i32)
```

### 5.2 `void*`

`void*` is an opaque pointer with a single storage level: it cannot be dereferenced, cannot reach
fields, and cannot be indexed or sliced (D3.11, D5.2). Every conversion to or from `void*`
requires `cast` (D3.14); `== null` is allowed (D10.5).

```fort
mut i32 x = 1;
void* vp = cast(&x, void*);
void* w = &x;                        // error: pointer to void* requires cast
i32 v = *vp;                         // error: void* cannot be dereferenced
i32 f = vp->x;                       // error: void* has no fields
i32 g = vp[0];                       // error: void* cannot be indexed
i32[] h = vp[0..1];                  // error: void* cannot be sliced
mut i32* back = cast(vp, mut i32*);
u64 addr = cast(vp, u64);
bool isnull = vp == null;
```

## 6. Type identity and equality

Two types are identical when (D3.12):

| Form             | Identical when                                                         |
|------------------|------------------------------------------------------------------------|
| primitive        | same name                                                              |
| struct, enum     | same declaration (nominal); `m.Vector` and `Vector` inside `m` agree   |
| fixed array      | identical element type and equal length                                |
| slice            | identical element type and element mutability (level 1)                |
| pointer          | identical pointee type and level-1 mutability                          |
| `void*`          | always                                                                 |
| function type    | structurally, as in section 5.1                                        |
| `string`         | always; distinct from `char[]` and `u8[]`                              |

Mutability bits behind an indirection are part of identity at every depth: `Node* mut*` and
`Node**` are different types. Level 0 is not: `Node* mut p` and `Node* q` hold the same type.

```fort
struct P1 { i32 x; }
struct P2 { i32 x; }
P1 a = {};
P2 b = a;                            // error: P1 is not P2, even with the same fields
```

`==` and `!=` are defined on integers, floats, `bool`, `char`, enums, pointers (address
identity), `void*`, function pointers (identity) and `string` (contents). They are compile errors
on structs, fixed arrays and slices (D3.13). Both operands must have identical types, including
mutability levels; an untyped constant takes the other operand's type (D4.1), and `null` takes
the type of a pointer operand (D10.5).

```fort
mut i32* p = &x;
i32* q = p;
bool e1 = p == q;                    // error: mut i32* and i32* are different types
bool e2 = p == cast(q, mut i32*);    // ok
bool e3 = null == null;              // error: null has no type here
i32[] s = {};
bool e4 = s == null;                 // error: compare s.ptr or s.len instead
```

## 7. Mutability

### 7.1 Immutable by default

Everything is immutable unless marked `mut`: variables, parameters, the targets of pointers and
the elements of slices (D5.1). Struct fields and array elements share the mutability of the value
that contains them.

```fort
i32 x = 1;
x = 2;                               // error: x is immutable
mut i32 y = 1;
y = 2;
```

### 7.2 Storage levels

A declared type is a chain of storage levels (D5.2). Level 0 is the binding's own storage. Each
`*` and each `[]` suffix introduces one further level: the storage reached through that
indirection. Levels are numbered from the binding inward: level 1 is reached through the
outermost indirection, which is the last `*` of a pointer chain or the first `[]` of a slice
chain, following the reading rules of section 3.3. Fixed arrays and structs add no level: their
elements and fields live in the storage of the containing value. `string` has exactly one level
(its characters are never mutable) and so does `void*` and every function type.

| Declared type   | Level 0    | Level 1                     | Level 2                |
|-----------------|------------|-----------------------------|------------------------|
| `i32 x`         | `x`        |                             |                        |
| `Node* p`       | `p`        | `*p`, `p->f`                |                        |
| `Node** pp`     | `pp`       | `*pp`                       | `**pp`, `(*pp)->f`     |
| `i32[] s`       | `s`        | `s[i]`                      |                        |
| `i32[][] s`     | `s`        | `s[i]` (an `i32[]` header)  | `s[i][j]`              |
| `Node*[] t`     | `t`        | `t[i]` (a `Node*` slot)     | `*t[i]`, `t[i]->f`     |
| `i32[3][4] m`   | `m`, `m[i][j]` |                         |                        |
| `Point q`       | `q`, `q.x` |                             |                        |
| `string s`      | `s`        |                             |                        |

### 7.3 The placement rule

A `mut` before the base type marks every level mutable, including the binding. A `mut`
immediately after a `*` or `[]` suffix marks mutable exactly the storage that holds the pointer
or slice header introduced by that suffix, that is, the level just outside the level the suffix
reaches (D5.3). Read the front `mut` as "fully mutable" and any other `mut` as "this level only".
`mut` never follows a fixed-array suffix, and a `mut` that does not precede the base type or
follow a `*` or `[]` does not parse.

In the table, "rebind" is `x = ...` on the binding itself; "level 1" covers writes such as
`*p = v`, `p->f = v`, `s[i] = v`, `t[i] = q`; "level 2" covers `**pp = v`, `(*pp)->f = v`,
`s[i][j] = v`, `t[i]->f = v`. The first ten rows are the D5.3 table.

| Declaration          | rebind               | level 1              | level 2            |
|----------------------|----------------------|----------------------|--------------------|
| `Node* p`            | no                   | no                   |                    |
| `Node* mut p`        | yes                  | no                   |                    |
| `mut Node* p`        | yes                  | yes                  |                    |
| `i32[] s`            | no                   | elements: no         |                    |
| `i32[] mut s`        | yes                  | elements: no         |                    |
| `mut i32[] s`        | yes                  | elements: yes        |                    |
| `Node* mut[] t`      | no                   | slots: yes           | pointees: no       |
| `Node* mut* pp`      | no                   | `*pp`: yes           | `**pp`: no         |
| `mut string s`       | yes                  | never                |                    |
| `mut Point q`        | yes (and fields)     | not applicable       |                    |
| `Node** pp`          | no                   | no                   | no                 |
| `Node** mut pp`      | yes                  | no                   | no                 |
| `mut Node** pp`      | yes                  | yes                  | yes                |
| `i32[][] mut s`      | no                   | `s[i] = t`: yes      | `s[i][j] = 1`: no  |
| `i32[] mut[] s`      | yes                  | no                   | no                 |
| `mut i32[][] s`      | yes                  | yes                  | yes                |
| `i32[][4] s`         | no                   | rows and cells: no   |                    |
| `mut i32[][4] s`     | yes                  | rows and cells: yes  |                    |
| `Node* mut[4] a`     | yes, and `a[i] = q`  | `a[i]->v = 1`: no    |                    |
| `mut Node*[4] a`     | yes, and `a[i] = q`  | `a[i]->v = 1`: yes   |                    |
| `i32[3][4] m`        | no, `m[i][j]` too    |                      |                    |
| `mut i32[3][4] m`    | yes, `m[i][j]` too   |                      |                    |
| `Point* p`           | no                   | `p->x = 1`: no       |                    |
| `mut Point* p`       | yes                  | `p->x = 1`: yes      |                    |
| `string[] mut v`     | yes                  | `v[i] = "a"`: no     | `v[i][0]`: never   |
| `mut string[] v`     | yes                  | `v[i] = "a"`: yes    | `v[i][0]`: never   |
| `void* mut vp`       | yes                  | not applicable       |                    |
| `mut fn i32(i32) f`  | yes                  | not applicable       |                    |

In `i32[][4] s` the `[4]` adds no level, so a row `s[i]` and a cell `s[i][j]` are both at
level 1. In `Node* mut[4] a` the `[4]` adds no level either, so the slots are level 0 and the
`mut` after `*` makes the binding, and with it every slot, assignable.

Rationale (D5.3): with C-style placement and the default inverted, the most common local,
`mut Node* cur = head; cur = cur->next;`, would need `mut` twice. What cannot be expressed is
"mutable target, non-rebindable variable", a lint-level property.

### 7.4 Dropping mutability

Dropping mutability is the only implicit conversion (D3.14, D5.4). It applies when a value is
used to initialize a declaration, on the right of an assignment, as an argument, and as a
`return` operand. Level 0 of the receiving binding is unconstrained. For a level `k >= 1`,
mutability may be dropped only if every level from 1 to `k - 1` is immutable in the target type.
Adding mutability at any level requires `cast` (D3.14).

| Conversion                          | Result | Reason                                        |
|-------------------------------------|--------|-----------------------------------------------|
| `mut Node*` to `Node*`              | ok     | drops level 1                                 |
| `mut Node*` to `Node* mut`          | ok     | level 0 is free; level 1 dropped              |
| `Node*` to `mut Node*`              | error  | adds mutability                               |
| `mut Node**` to `Node**`            | ok     | drops levels 1 and 2                          |
| `mut Node**` to `Node* mut*`        | error  | drops level 2 while level 1 stays mutable     |
| `mut Node*[]` to `Node*[]`          | ok     | drops levels 1 and 2                          |
| `mut Node*[]` to `Node* mut[]`      | error  | drops level 2 while level 1 stays mutable     |
| `mut i32[][]` to `i32[] mut[]`      | ok     | only level 0 of the target is mutable         |
| `mut i32[][]` to `i32[][] mut`      | error  | drops level 2 while level 1 stays mutable     |
| `mut i32[]` to `i32[]`              | ok     | drops level 1                                 |
| `i32[]` to `mut i32[]`              | error  | adds mutability                               |

The rule closes the C `T** -> const T**` hole with a short recursive check:

```fort
mut i32 x = 1;
mut i32* p = &x;                     // mut i32*: p is rebindable, *p is writable
mut i32** pp = &p;                   // &p: level 1 is p's storage, level 2 is x
i32* mut* q = pp;                    // error: cannot drop mutability at level 2 through a
                                     //        mutable level 1
// If the line above were accepted, the next two lines would both type-check and
// together write through pp into the immutable c:
i32 c = 5;
*q = &c;                             // *q is a mutable slot holding an i32*
**pp = 7;                            // writes c through pp's mut i32** type
```

`new(Node*[3])` returns `mut Node*[]`, so binding it to `Node* mut[] t` is refused by the same
rule; declare `mut Node*[] t` or use `Node*[] t` (D5.4).

### 7.5 Struct fields and return types

A field's own storage is exactly as mutable as the struct value that contains it: level 0 of the
field is inherited from the access path (D5.5). A leading `mut` in a field type therefore
describes the levels behind the field's indirections; a `mut` that would apply only to the
field's own slot is an error. The same holds for return types: a returned value is a temporary,
so a `mut` that marks only level 0 of a return type is an error.

```fort
struct Node {
    i32 value;
    mut Node* next;                  // *next is writable through any Node, even an immutable one
    Node* prev;                      // *prev is read-only
    mut i32[] items;                 // elements writable
}
struct B1 { mut i32 x; }             // error: mut would apply only to the field's own slot
struct B2 { Node* mut next; }        // error: mut would apply only to the field's own slot
struct B3 { mut Point p; }           // error: mut would apply only to the field's own slot
struct B4 { mut i32[4] a; }          // error: a fixed array adds no level
struct B5 { i32[] mut s; }           // error: mut would apply only to the field's own slot

fn mut Node* head(List l) { return l.first; }   // ok: describes level 1
fn mut i32 count(List l) { return 0; }          // error: mut on a return type without indirection
fn Node* mut first(List l) { return l.first; }  // error: mut would apply only to the result
```

### 7.6 Parameters

A parameter is a local copy of the argument (D8.2). `mut` on a parameter follows the placement
rule; at level 0 it makes the callee's copy assignable (D5.6). Function-type identity ignores
level 0 of every parameter (D3.10), so `fn void(i32)` and `fn void(mut i32)` are one type.

```fort
fn i32 total(i32[] xs, mut i32 acc) {
    for (i32 x : xs) { acc += x; }
    return acc;
}
fn void bump(i32 n) { n += 1; }                  // error: n is immutable
fn void step(Node* mut cur) { cur = cur->next; } // ok: rebinds the local copy only
fn void set(mut Node* n) { n->value = 1; }       // ok: writes through the pointer
fn void set2(Node* n) { n->value = 1; }          // error: *n is immutable
```

### 7.7 Mutability of lvalues

An lvalue (D6.7) is mutable according to the table below (D5.7). Assignment, compound assignment,
`++`, `--` and `&e` yielding a `mut T*` all require a mutable lvalue.

| Lvalue                        | Mutable when                                              |
|-------------------------------|-----------------------------------------------------------|
| variable, parameter `x`       | `x` was declared with level 0 mutable                     |
| `*p`, `p->f`                  | level 1 of `p`'s type is mutable                          |
| `e.f`, `e[i]` on a fixed array| `e` is a mutable lvalue                                   |
| `s[i]` on a slice             | level 1 of `s`'s type is mutable                          |
| `str[i]` on a string          | never                                                     |
| `(e)`                         | as `e`                                                    |
| `s.len`, `s.ptr`, `a.len`     | not lvalues                                               |
| call result, literal          | not lvalues; `f().x` and `f()[0]` are rvalue copies       |

```fort
Point q = {};
q.x = 1;                             // error: q is immutable
mut Point r = {};
r.x = 1;
mut i32[] s = new(i32[2]);
s[0]++;
string str = "ab";
str[0] = 'c';                        // error: string characters are immutable
s.len = 1;                           // error: .len is not an lvalue
make_point().x = 1;                  // error: not an lvalue
```

### 7.8 Results of `&` and `new`

`&e` requires an lvalue and has the pointer type whose level 1 is the mutability of `e` and whose
deeper levels come from `e`'s type (D5.8, D6.7). `new(T)` returns `mut T*` and `new(T[n])`
returns `mut T[]` (D5.8, D10.2). `&e` is an error when `e` is a fixed array or a slice, because
the pointer type cannot be written (D3.6).

| Declaration           | Expression | Type                                              |
|-----------------------|------------|---------------------------------------------------|
| `i32 x`               | `&x`       | `i32*`                                            |
| `mut i32 x`           | `&x`       | `mut i32*`                                        |
| `Node* p`             | `&p`       | `Node**`                                          |
| `Node* mut p`         | `&p`       | `Node* mut*`                                      |
| `mut Node* p`         | `&p`       | `mut Node**`                                      |
| `mut i32[4] a`        | `&a[2]`    | `mut i32*`; `&a` is an error (pointer to array)   |
| `mut i32[] s`         | `&s[0]`    | `mut i32*`; `&s` is an error (pointer to slice)   |
| `string str`          | `&str[0]`  | `char*`; `&str` is `string*`                      |
| `mut Point q`         | `&q.x`     | `mut i32*`                                        |
| module `i32 K = 3;`   | `&K`       | `i32*` (D7.10)                                    |

```fort
Point* p = new(Point);               // ok: level 1 dropped (D5.4)
p->x = 1;                            // error: *p is immutable through p
mut Point* q = new(Point);
q->x = 1;
i32* r = &(1 + 2);                   // error: & requires an lvalue
```

### 7.9 The shallow model

Immutability of a variable never propagates through a pointer or slice it contains; the levels
behind an indirection are fixed by the type alone (D5.9).

```fort
struct Node {
    i32 value;
    mut Node* next;
}
Node n = Node{.value = 0, .next = new(Node)};
n.value = 1;                         // error: n is immutable
n.next = null;                       // error: the slot is as immutable as n
n.next->value = 1;                   // ok: level 1 of the field type is mutable
```

## 8. Conversions

### 8.1 Implicit

The only implicit conversion is dropping mutability under the rule of section 7.4 (D3.14, D5.4).
It applies to initialization, assignment, argument passing and `return`. Nothing else converts
implicitly: not integer widths, not signedness, not integer to float, not array to slice, not
`char[]` to `string`. Untyped constants are not converted; they take a type from context
(section 9).

```fort
i32 a = 1;
i64 b = a;                           // error: no implicit widening
f64 f = a;                           // error: no implicit integer to float
i32[4] arr = {};
i32[] s = arr;                       // error: write arr[..]
```

### 8.2 `cast`

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
| `T*`                | `U*`, any `mut`          | reinterpret the address; may add `mut`        |
| `T*`, `void*`       | `void*`, `U*`            | reinterpret the address; may add `mut`        |
| `T*`, `void*`       | `u64`                    | the address as an integer                     |
| `u64`               | `T*`, `void*`            | the integer as an address                     |
| pointer             | other integer types      | error                                         |
| function pointer    | `void*`                  | reinterpret                                   |
| `void*`             | function pointer         | reinterpret                                   |
| function pointer    | `u64`, other fn type     | error (go through `void*`)                    |
| `string`            | `char[]`, `u8[]`         | reinterpret the header                        |
| `string`            | `mut char[]`, `mut u8[]` | reinterpret; adds `mut` (cast-away-const)     |
| `char[]`, `u8[]`    | `string`, each other     | reinterpret the header; `mut` forms likewise  |
| `T[]`               | `mut T[]`                | add mutability at every level                 |
| `mut T*`, `mut T[]` | `T*`, `T[]`              | drop mutability, as the implicit conversion   |
| slice               | other element type       | error: `len` counts elements of one type      |
| pointer             | slice                    | error (use `p[lo..hi]`)                       |
| slice               | pointer                  | error (use `.ptr`)                            |
| fixed array         | anything                 | error (use `a[..]`)                           |
| struct              | anything                 | error                                         |
| anything            | fixed array, struct      | error                                         |
| any `T`             | `T`                      | identity                                      |

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
i32 s4 = cast(0.0 / 0.0, i32);       // 0: NaN
i32 b = cast(true, i32);             // 1
bool x = cast(1, bool);              // error: no cast from integer to bool
i32 c = cast('A', i32);              // 65
char d = cast(0x1F600, char);        // '\x00': low byte kept
Color e = cast(2, Color);            // holds 2, no check
Shape g = cast(Color.Red, Shape);    // error: no cast between enums
mut i32* p = cast(&k, mut i32*);     // adds mutability; writing k is undefined if k is read-only
u64 a = cast(p, u64);
i32 n = cast(p, i32);                // error: pointer casts only to u64
mut u8[] bytes = cast("abc", mut u8[]);   // aliases read-only memory; writing it is undefined
u8[] ro = cast("abc", u8[]);              // ok
i8[] sb = cast(ro, i8[]);            // error: element type of a slice never changes
i32[] q = cast(p, i32[]);            // error: no cast from pointer to slice
Point pt = cast(rec, Point);         // error: no struct casts
```

## 9. Untyped constants and constant expressions

### 9.1 Untyped constants

Integer, float and character literals are untyped constants (D4.1). An untyped constant has no
type of its own; it takes one from context:

- the declared type of the variable being initialized or assigned;
- the type of the other operand of a binary operator, except that the count operand of a shift
  is never a context for the shifted operand (section 9.4);
- the parameter type in a call, the return type in `return`, the operand type in a `case` label;
- an index or slice-bound position, where any integer type is accepted (D6.8, D6.9).

`cast(c, T)` is not a context: an untyped `c` takes its default type first (D4.1, D4.5).

```fort
i64 a = 1;                           // 1 is i64
u8 b = 1;                            // 1 is u8
f64 c = 1;                           // 1.0
i32 d = 3 + a;                       // error: 3 becomes i64, and i64 is not i32
```

### 9.2 Contextual conversion is checked (D4.2)

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

### 9.3 Character literals (D4.3)

A character literal's default type is `char`; in an integer context it takes that integer type.
An integer literal never becomes `char` implicitly.

```fort
char c = 'a';
u8 b = 'a';                          // 97
i32 d = cast(c, i32) - '0';          // '0' becomes i32
char e = 65;                         // error: use cast(65, char)
```

### 9.4 Folding among untyped constants (D4.4)

Operators applied to untyped constants fold at compile time. Integer with integer stays an
untyped integer, so `1 / 2` is `0`; integer with float becomes an untyped float; `~c` on an
untyped integer is `-c - 1`. Untyped integers are evaluated exactly in `[-2^63, 2^64 - 1]`; an
intermediate outside that range, and division by zero, are compile errors. Untyped floats are
evaluated as `f64`; an `f32` constant is the rounding of that `f64` value. The shifted operand of
a shift takes its type from the context of the whole shift expression, never from the count.

```fort
f64 h = 1 / 2;                       // 0.0: integer division folded first
f64 k = 1.0 / 2;                     // 0.5
i32 q = 1 << 40 >> 38;               // 4: folded exactly, then fits i32
u32 m = ~0;                          // error: ~0 is -1, which does not fit u32
u32 m2 = 0xFFFFFFFF;                 // ok
i32 z = 1 / 0;                       // error: constant division by zero
u64 big = 1 << 64;                   // error: intermediate outside the constant range
u64 sh = 1 << n;                     // 1 is u64 from the declaration, whatever n's type
i32 mixed = 1 + 0.5;                 // error: 1.5 is a float constant
f64 mixed2 = 1 + 0.5;                // 1.5
```

### 9.5 Default types (D4.5)

With no context at all (an argument to `print`, an operand of `cast`, the operand of a `switch`,
the count in `new(T[n])`), an untyped integer becomes `i32` if it fits, otherwise `i64`,
otherwise it is an error; an untyped float becomes `f64`; a character literal becomes `char`.
`null` has no default type: it takes the type of a pointer it is initialized into, assigned to,
passed as, returned as, or compared with.

```fort
print(7);                            // i32
print(3000000000);                   // i64
print(1 << 63);                      // error: 9223372036854775808 does not fit i64
print(1.5);                          // f64
print('a');                          // char: prints a
print(null);                         // error: null has no type here
switch (2) { case 2: }               // the operand is i32 (D7.6)
```

### 9.6 Constant expressions (D4.6)

Array lengths, `case` labels, enum values and module-level initializers (D7.10) require constant
expressions. A constant expression is one of:

- a literal, `true`, `false`, `null`;
- a module-level immutable declaration with a constant initializer, from any module;
- an enum member;
- `sizeof(T)`;
- `.len` of an expression of fixed-array type (only the type is used; the expression is not
  evaluated, so `s[i].len` is constant when `s` is an `i32[][4]`);
- unary `- ! ~`; binary `+ - * / % +% -% *% & | ^ << >> < <= > >= == != && ||`; `?:`;
- `cast` whose source and target are both integer or float types;
- a parenthesized constant expression;
- a struct or array literal whose leaves are constant expressions.

Not constant: calls, `&` (except `&` of a module-level declaration inside a module-level
initializer, D7.10), indexing, slicing, field access, `.len` of a slice or string, reads of
`mut` globals, `cast` involving `char`, `bool`, enums, pointers or slices, and a `?:` whose
condition is not constant.

```fort
i32 N = 4;                           // module level: a constant, addressable, read-only
i32[N * 2] buf = {};                 // ok: 8
i32[buf.len + 1] more = {};          // ok: 9
mut i32 g = 4;
i32[g] bad = {};                     // error: reads a mut global
i32[buf[0]] bad2 = {};               // error: indexing is not constant
i32[cast(Color.Blue, i32)] bad3 = {};// error: cast of an enum is not constant
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
struct S1 { S2 s; }
struct S2 { S1 s; }                  // error: struct S1 has infinite size
```

## 10. `sizeof`, layout and ABI

### 10.1 `sizeof`

`sizeof(Type)` takes a type, never an expression, and yields an untyped integer constant
(D3.15). `sizeof(void)` is an error. There is no `alignof` (D15); the alignments below are what
the compiler uses.

| Type                       | `sizeof`             | Alignment        | Note                     |
|----------------------------|----------------------|------------------|--------------------------|
| `i8`, `u8`, `bool`, `char` | 1                    | 1                |                          |
| `i16`, `u16`               | 2                    | 2                |                          |
| `i32`, `u32`, `f32`, enum  | 4                    | 4                | enums are `i32` (D3.9)   |
| `i64`, `u64`, `f64`        | 8                    | 8                |                          |
| `T*`, `void*`, `fn R(P)`   | 8                    | 8                |                          |
| `T[N]`                     | `N * sizeof(T)`      | that of `T`      | elements are contiguous  |
| `T[]`, `string`            | 16                   | 8                | `{ptr, len}` (D3.5, D3.7)|
| struct                     | fields plus padding  | largest field    | rounded up; matches C    |
| `void`                     | error                |                  |                          |

```fort
u64 a = sizeof(i32[3][4]);           // 48
u64 b = sizeof(Node*[16]);           // 128
u64 c = sizeof(string);              // 16
u64 d = sizeof(fn i32(i32));         // 8
u64 e = sizeof(Rec);                 // 24, from section 4.1
u64 f = sizeof(x);                   // error: sizeof takes a type, not an expression
u64 g = sizeof(void);                // error
```

### 10.2 Passing and returning

Semantically every parameter and result is passed by value (D8.2): scalars and reference values
copy the scalar or the fat pointer; structs and fixed arrays copy the whole value. The internal
calling convention realizes this as follows (D9.9):

| Value kind                                    | Passed                        | Returned        |
|-----------------------------------------------|-------------------------------|-----------------|
| integers, `bool`, `char`, enums, all pointers | integer registers (System V)  | `rax`           |
| `f32`, `f64`                                  | SSE registers (System V)      | `xmm0`          |
| struct, fixed array, slice, `string`          | pointer to a caller-made copy | hidden pointer  |

Struct layout is identical to the C layout of the same declaration, so passing `&s` to C works.
A fort function is usable as a C callback, and an `extern` function may be declared, exactly when
every parameter and the result are integers, floats, `bool`, `char`, pointers or function
pointers; slices, strings, structs and arrays never cross an `extern` boundary (D9.8). Narrow
integers and `bool` are zero- or sign-extended on both sides of the boundary; C `char*` maps to
`char*` or `u8*`, C `size_t` to `u64` (D9.8).

```fort
extern fn i64 write(i32 fd, void* buf, u64 n);
extern fn void sort(i32[] xs);       // error: slices cannot cross an extern boundary
```

## 11. Not in v1

Generics, unions (tagged or untagged), `Result`, methods, closures, overloading, default and
named arguments, type aliases, struct, array and slice equality, alignment and packed attributes,
`alignof`, `sizeof(expr)`, pointer-to-array and pointer-to-slice types, and string `switch` are
deferred; D15 lists each with the idiom to use instead (a fat struct with a kind field for
unions, `bool` plus out-parameters for results, an opaque `u8[N]` field with a C shim for
alignment, a struct wrapper for a pointer to an array or slice).
