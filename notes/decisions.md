# fort design decisions

This is the decision log for the fort language, v1. It is the source of truth: when another
document in `notes/` disagrees with this file, this file wins and the other document has a bug.
Each decision has an identifier (`D3.4`) that the specification documents cite. Rationale is kept
short; the specification documents carry the full prose, examples and edge cases.

Sections:

- D1 Naming and files
- D2 Lexical structure
- D3 Types
- D4 Untyped constants and constant expressions
- D5 Mutability
- D6 Expressions and evaluation
- D7 Statements
- D8 Functions
- D9 Modules, namespaces and FFI
- D10 Memory and runtime checks
- D11 Build modes and the runtime contract
- D12 Builtins
- D13 Standard library scope
- D14 Toolchain and test conventions
- D15 Not in v1
- D16 Hazards not to re-litigate
- D17 Ownership
- D18 Float printing in the runtime
- D19 Target: LLVM IR
- Ready-to-implement checklist

## D1 Naming and files

- **D1.1** The language is `fort`. Source files use the extension `.ft`. The compiler binary is
  `fort`. The documents' earlier `.lang` and `langc` are gone.
- **D1.2** The design lives in `notes/`. `decisions.md` (this file) and `grammar.md` are normative;
  `core-language.md`, `type-system.md`, `memory-model.md`, `module-system.md`, `stdlib.md` and
  `toolchain.md` are the specification proper, each owning one topic. `project-overview.md` is the
  entry point.
- **D1.3** Markdown wraps at 100 columns (AGENTS.md).
- **D1.4** Identifier conventions (not enforced by the compiler): modules, functions, variables,
  fields, struct and enum type names, and enum members are lower_case with underscores
  (`struct str_buf`, `enum color { red, green }`, `color.red`, `fn i32 parse_i64(...)`);
  module-level constants are UPPER_CASE (`i32 MAX = 64;`); a variable never takes its type's
  name (`point p`, never `point point`), because a local may shadow a module-level name (D7.9);
  a struct field may (`node* node;`), since fields live in no namespace a type could occupy.
  Rationale: user decision, matching C's `struct point` and the function and variable style.

## D2 Lexical structure

Owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).

- **D2.1** Source is UTF-8. Whitespace is space, tab, `\n`, `\r`. An optional leading BOM is
  skipped. Non-ASCII bytes are allowed only inside string literals and comments.
- **D2.2** Comments: `//` to the end of the line, and nothing else. There are no block comments,
  so there is no nesting question, no unterminated-comment failure mode, and no `a /*p`
  ambiguity; commenting out a region is a line-oriented operation every editor does. The
  adjacent two-byte sequence `/*` is a lexical error ("block comments are not supported, use
  `//`") rather than a division followed by a dereference, so the C habit fails loudly instead
  of parsing as something else; `a / *p`, with the operators separated, is that division and is
  legal. Rationale: user decision, and the same rule holds for the project's C sources
  (AGENTS.md).
- **D2.3** Identifiers: `[A-Za-z_][A-Za-z0-9_]*`, case-sensitive, no length limit. `_` is an
  ordinary identifier. Keywords cannot be identifiers.
- **D2.4** Keywords:
  `as bool break case cast char continue default defer do else enum extern f32 f64 false fn for
  i8 i16 i32 i64 if import mut new noreturn null own return sizeof string struct switch true u8
  u16 u32 u64 void while`.
  Reserved for future use, not usable as identifiers:
  `async await const match pub priv trait type union yield`.
- **D2.5** Integer literals: decimal `123`, hex `0x7F`, octal `0o17`, binary `0b1010`. `_` may
  appear between two digits (`1_000_000`, `0xFF_FF`), nowhere else. A decimal literal other than
  `0` may not start with `0` (no C-style octal). No suffixes.
- **D2.6** Float literals: digits `.` digits with optional exponent (`1.0`, `2.5e-3`), or digits
  with an exponent (`1e10`). `1.` and `.5` are not literals, and the integer part follows D2.5's
  leading-zero rule (`0.5` and `0e1` are fine, `09.5` is an error). No suffixes; `f32` values
  come from context (D4).
- **D2.7** Char literals: `'x'` where `x` is one printable ASCII character other than `'` or `\`,
  or one escape. A non-ASCII byte in a char literal is an error ("use a string").
- **D2.8** Escapes, in char and string literals: `\n \t \r \0 \\ \' \" \xHH` (exactly two hex
  digits). Anything else after `\` is an error.
- **D2.9** String literals: `"..."` with escapes; a raw newline inside is an error; no
  adjacent-literal concatenation; no raw strings.
- **D2.10** Operators and punctuation:
  `+ - * / % +% -% *% = += -= *= /= %= +%= -%= *%= &= |= ^= <<= >>= == != < <= > >= && || ! & | ^ ~
  << >> ++ -- ? : :: . -> .. ( ) [ ] { } , ; @`. Longest match wins (`+%=` before `+%` before `+`).
  `%` is never a prefix operator, so `+%` is unambiguous. `>>` and `<<` are single tokens.
- **D2.11** Nesting of blocks, parentheses, brackets, braces and type suffixes deeper than 256 is a
  compile error, so a recursive-descent compiler written in fort never needs an unbounded stack.

## D3 Types

Owner: `type-system.md`.

- **D3.1** Primitive types: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char void`. Sizes: 1, 2,
  4, 8 bytes for the integers, 4 and 8 for the floats, 1 for `bool` and `char`; alignment equals
  size. Pointers, function pointers, spans and strings align to 8; arrays to their element;
  structs to their most-aligned field. `void` is only a return type or the base of `void*`.
  There is no `byte` type.
- **D3.2** `char` is a distinct one-byte character type. It supports `== != < <= > >=` (ordered
  by unsigned byte value), `switch`, `cast` to and from integer types, and nothing else (no
  arithmetic, no bitwise operators). Char literals default to `char` (D4.3). Rationale: the user
  chose a distinct text type over `u8`.
- **D3.3** `bool` has exactly the values `true` and `false`. Conditions must be `bool`; there is no
  truthiness for integers or pointers (`p != null`, `x != 0`). `bool` supports `== != ! && ||`
  and `cast` to integer types (0 or 1). `cast` from an integer to `bool` is an error.
- **D3.4** Fixed arrays `T[N]`: `N` is a constant expression (D4.6) greater than 0. Value type:
  assignment, parameter passing and return copy all elements. Different `N` are different types.
  `a.len` is an untyped integer constant. Fixed arrays have no `.ptr`. Sizes are computed
  exactly: a type whose size would exceed `2^63 - 1` bytes (the range of an `i64` index) is a
  compile error, "type is too large", at the declaration that introduces it, reported like the
  infinite-size error of D3.8 rather than as an out-of-memory failure of the compiler. Amended
  2026-09-10 (T-011 review): the limit was unstated.
- **D3.5** Spans `T@` (the reference suffix `@`, D3.6; until 2026-09-10 spelled `T[]`) replace the
  earlier "dynamic array". A span is a fat pointer `{T* ptr; u64 len}`; whether it owns its
  elements is part of its type (`own`, D17); all spans of the same element type, element mutability
  (D5) and ownership are one type; the zero value is `{null, 0}`. `.len` (type `u64`) and `.ptr` (a
  pointer to the element type, carrying the element level's mutability: `node* mut@` gives `node*
  mut*`) are read-only pseudo-fields. Spans are produced by `new(T, n)` (D10.2, as `T mut@ own`),
  by taking a span (D6.9, always a view) and by the zero initializer `{}`. A span literal
  `{1, 2, 3}` does not exist. Amended 2026-09-10: the type was called a slice until then, and the
  operation slicing.
- **D3.6** Type suffixes read as follows. A reference suffix, `*` (pointer) or `@` (span,
  D3.5), applies to everything to its left, so a sequence of them reads inside-out: `node**` is a
  pointer to a pointer, `node*@` a span of pointers, `u8@*` a pointer to a span, `u8@@` a span of
  spans. Fixed-array suffixes form one group that reads outside-in like C declarators: `i32[3][4]`
  is three arrays of four, indexed `a[i][j]` with `i < 3`, `j < 4`. Reference suffixes may precede
  the array group, making arrays of references (`node*[16]` is sixteen pointers, `node@[4]` four
  spans), or follow it, making references to the whole array (`i32[4]*` points to an `i32[4]`,
  `i32[4]@` is a span of `i32[4]`, and `new(i32[4], n)` returns `i32[4] mut@ own`); no array suffix
  may follow a trailing reference suffix (`i32[4]*[2]` does not parse; wrap it in a struct). `u8@*`
  is the usual shape of an out-parameter (`fn bool read_file(string path, u8 mut@ own mut* out)`,
  D17.2). Suffixes after a function type apply to the function type: `fn i32(i32)[4]` is an array of
  four function pointers, `fn i32[4](i32)` returns an `i32[4]`. Amended 2026-09-10: spans were
  called slices and spelled `T[]`, read with the array group, so `i32[][4]` was the span of
  `i32[4]`.
- **D3.7** `string` is a distinct type: an immutable span of `char` (`{char* ptr; u64 len}`).
  Literals have type `string` and are stored in read-only memory with a trailing NUL that is not
  counted in `len`. Sub-strings are not NUL-terminated. Indexing yields `char`; a span of a
  `string` is a `string`; `.len` and `.ptr` (`char*`) exist; `==`/`!=` compare `len` then bytes,
  so the zero string equals `""`. Bytes are UTF-8 by convention and never validated. There is no
  `+`; the standard library concatenates and returns `string own`, the owned form (D17.12). Amended
  2026-09-10: spans were called slices (D3.5).
- **D3.8** Structs: `struct name { T1 f1; T2 f2; }` with no trailing semicolon, nominal typing,
  C/System V layout (fields in order, natural alignment, size rounded to alignment). No methods,
  no inheritance, no per-field `mut` at the field's own level (D5.5). An empty struct is an
  error. A struct may contain itself only through a pointer or span; value-containment cycles
  are "infinite size" errors.
- **D3.9** Enums: `enum color { red, green = 5, blue }`. Underlying type `i32`, size 4. Members
  are scoped: `color.red` everywhere, including `case` labels; `m.color.red` across modules.
  Values start at 0 and increment; an explicit value is a constant expression that may not refer
  to the enum itself; duplicate values are errors. Enums support `== !=`, `switch` (D7.7) and
  `cast` to and from any integer type (int-to-enum is unchecked). No ordering operators. A zeroed
  enum holds 0 even if 0 is not a member.
- **D3.10** Function types are written `fn R(P1, P2)` with parameter types only. Identity is
  structural over parameter types (including pointee mutability), return type and `noreturn`;
  binding-level `mut` on parameters is ignored. A function name used as a value, including a
  qualified `m.f`, has its function type; `&f` and `*f` are errors. `null` is a valid
  function-pointer value; calling it is
  undefined behavior. `==`/`!=` compare identity. Function pointers are in the C bootstrap's
  subset (`toolchain.md` 7.3): a function pointer is an ordinary `ptr` value and a call through
  one an ordinary `call` in LLVM IR (D19.2), so the bootstrap implements them.
- **D3.11** `void*` is an opaque pointer with no pointee level: no `*`, `->`, indexing or span
  expression. Conversion to and from any pointer, function pointer or `u64` requires `cast`.
- **D3.12** Type identity: primitives by name; structs and enums nominally; arrays by element type
  and length; spans, pointers and function types structurally, including mutability levels
  behind indirections (D5.2).
- **D3.13** Equality `==`/`!=` is defined on integers, floats, `bool`, `char`, enums, pointers
  (identity), function pointers (identity) and `string` (contents). It is a compile error on
  structs, fixed arrays and spans. This narrows type-system.md's earlier "all types support ==".
- **D3.14** Conversions. The only implicit conversion is dropping mutability (D5.4). Everything else
  is `cast(expr, Type)` (D6.4). Allowed casts: integer to integer (widening sign- or zero-extends by
  the source's signedness, narrowing truncates, same-width sign change reinterprets); integer to
  float (round to nearest); float to integer (truncate toward zero, saturate at the target's range,
  NaN becomes 0); float to float; `bool` to integer; `char` to and from integer; enum to and from
  integer; any pointer to any pointer or `void*` (mutability may be added, this is the cast-away-
  const escape); pointer to and from `u64`; function pointer to and from `void*`; among `string`,
  `char@`, `u8@`, `char mut@` and `u8 mut@` (a `mut` in the outermost position of a cast target is
  an error: a cast result has no binding); a span to a span of the same element type whose marks
  differ only in mutability, added or dropped at any level (the cast-away-const escape, as for
  pointers; amended 2026-09-10 from the T-011 review, which found the earlier `T@` to `mut T@`
  wording narrower than the rule); any cast that only drops mutability or ownership, at any level (a
  no-op, since the implicit conversions of D5.4 and D17.4 cover it); adding `own` to a pointer or
  span (adoption, D17.3); identity. The result of a cast is `own` exactly when its target type says
  `own`: an `own` source cast to a non-`own` target lends (the result is a view), a non-`own` source
  cast to an `own` target adopts, and an `own` lvalue cast to an `own` target is a copy that must be
  written `cast(move(x), ...)` (D17.5). Forbidden: integer to `bool`, any other span-to-span cast
  (the element type of a span never changes, because `len` counts elements), pointer to span,
  struct or array casts. Casts never trap: float to integer is emitted as `llvm.fptosi.sat` or
  `llvm.fptoui.sat`, whose saturating result is this rule (plain `fptosi` would be poison out of
  range, D19.2). Amended 2026-09-10: spans were called slices (D3.5).
- **D3.15** `sizeof(Type)` takes a type only, yields an untyped integer constant (D4). `sizeof` of
  `void` is an error. `sizeof(T@)` and `sizeof(string)` are 16; function pointers are 8; `bool`
  and `char` are 1; enums are 4. There is no `alignof` in v1.
- **D3.16** No type inference (`var`/`auto`) and no type aliases in v1.

## D4 Untyped constants and constant expressions

Owner: `type-system.md` (Constants), `core-language.md` (Literals).

- **D4.1** Integer, float and char literals are untyped constants. An untyped constant takes its
  type from context: the declared type of the variable being initialized or assigned, the other
  operand of a binary operator, the parameter type, the return type, the `case` operand type, or
  an index, span-bound or `new` count position (any integer type is fine there; a negative
  constant in such a position is a compile error). This is the Go model. A `cast` is not a
  context: in `cast(c, T)` an untyped `c` first takes its default type (D4.5) and is then
  converted to `T` with runtime semantics (D4.4). The count operand of a shift is not a context
  either: in `u64 m = 1 << n;` the untyped `1` takes `u64` from the declaration, whatever the
  type of `n`; with no enclosing context it takes its default type.
- **D4.2** Contextual conversion is checked at compile time. An untyped integer may become any
  integer type it fits in, or any float type. An untyped float may become only a float type,
  never an integer type even when integral (`i32 x = 2.0;` is an error). `u32 x = -1;` and
  `u8 b = 256;` are errors.
- **D4.3** A char literal is an untyped constant whose default type is `char`; in an integer
  context it takes that integer type (`cast(c, i32) - '0'` and `u8 b = 'a';` are fine). An
  integer literal never becomes `char` implicitly (`char c = 65;` is an error; use `cast`).
- **D4.4** Arithmetic among untyped constants folds at compile time: integer with integer stays an
  untyped integer (`1 / 2` is `0`, so `f64 d = 1 / 2;` is `0.0`); integer with float becomes an
  untyped float; `~c` on an untyped integer is `-c - 1`, so `u32 m = ~0;` is an error (write
  `0xFFFFFFFF`); constant `/` and `%` truncate toward zero exactly as at runtime (D6.13); `& | ^`
  on untyped integers operate on the infinite two's-complement extension of the values (Go's
  rule; `-1 & 0xFFFFFFFFFFFFFFFF` is `18446744073709551615`), and only `^` can leave the range;
  shifts among untyped constants fold exactly too, with a count in `0..63`, `<<` an exact
  multiplication and `>>` a floor division (`-3 >> 1` is `-2`), so `i32 x = 1 << 31;` is an
  error (2147483648 does not fit `i32`) while `cast(1 << 31, i32)` is `-2147483648` and
  `one << 31` on an `i32` variable `one` is `-2147483648` (D6.2). Untyped integers are evaluated
  exactly in the range `[-2^63, 2^64 - 1]`; any intermediate outside it, a shift count outside
  `0..63`, and constant division by zero, are compile errors. Untyped floats are
  evaluated as `f64`. `cast` on a constant has runtime semantics (`cast(0x80000000, i32)` is
  `-2147483648`, `cast(-1, u32)` is `4294967295`). A float constant that is not finite in the
  target type is an error.
- **D4.5** With no context at all (for example an argument to `print`), an untyped integer
  becomes `i32` if it fits, otherwise `i64`, otherwise it is an error; an untyped float becomes
  `f64`; a char literal becomes `char`.
- **D4.6** Constant expressions (required for array lengths, `case` labels, enum values and
  module-level initializers): literals, `true`, `false`, `null`, module-level immutable
  declarations with constant initializers (from any module), enum members, `sizeof`, `.len` of
  any expression of fixed-array type (the operand is not evaluated, so `m[i].len` is constant
  for `i32[3][4] m`), unary `- ! ~`, the binary arithmetic, wrapping, bitwise, shift,
  comparison and logical operators, `?:`, `cast` among numeric types, `char` and enums (so
  `cast(color.blue, i32) + 1` may size an array), parentheses, and struct or array literals whose
  leaves are constant expressions. Not constant: calls, `&` (except `&global` in module-level
  initializers, D7.10), field access, indexing, span expressions, `.len` of spans or strings,
  reads of `mut` globals, `null` in a `cast`. Typed constant folding respects the declared type and
  applies every checked-mode rule at compile time (overflow, shift count, division by zero,
  `MIN / -1` and `MIN % -1` are all compile errors in a typed constant): `i32 A = 2147483647;`
  then
  `A + 1` is a compile error, not a runtime trap. Constant references are evaluated lazily with
  cycle detection; `i32 A = B; i32 B = A;` is an error.

## D5 Mutability

Owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).

- **D5.1** Everything is immutable unless marked `mut`. This applies to variables, parameters,
  the targets of pointers and the elements of spans.
- **D5.2** Storage levels. A declared type is a chain of storage levels numbered from the
  binding inward: level 0 is the binding's own storage; level 1 is the storage reached through
  the outermost indirection (the `*` or `@` whose value the binding holds); level 2 the storage
  reached through the next indirection, and so on. With the suffix-reading rules of D3.6, the
  outermost indirection is always the last reference suffix: of `node**` the last `*` (level 1
  holds a `node*`, level 2 a `node`), of `node*@` the `@` (level 1 holds `node*` elements, level
  2 the nodes), of `i32@@` the last `@`, and of `u8@*` the trailing `*` (level 1 holds the span
  header, level 2 the bytes). Fixed arrays and structs do not add a level: their elements
  and fields share the storage of the value that contains them. `string` has a single level (its
  characters are never mutable). `void*` has a single level (D3.11). Amended 2026-09-10: spans
  were called slices (D3.5).
- **D5.3** Placement rule. A `mut` marks the storage of the type element it follows: after the base
  type, values of that type (`node mut*` points to writable nodes, `u8 mut@` is a span of writable
  bytes); after a `*`, the pointer that suffix introduces, that is, the storage holding it (`node*
  mut p` is rebindable); after an `@`, the span header (`u8@ mut s`); after a fixed-array suffix,
  the array, whose elements share its storage (D5.2), so `i32[4] mut a` marks both and `i32 mut[4]`
  is an error ("mark the array after its length"). Nothing precedes the base type: `mut node* p` is
  an error ("write `node mut* p` or `node* mut p`"). Each storage level has exactly one position, so
  every type has one spelling, and a doubled marker does not parse. The outermost position is the
  binding's own storage, so the `mut` immediately before the name says the binding is assignable,
  for `i32 mut x`, `node* mut p` and `u8@ mut s` alike; `string mut s` is rebindable and `string`
  has no element position (D5.2); `void* mut p` is legal and `void mut*` is not (D3.11).

  | Declaration          | rebind `p = ...`  | write through `*p`, `p->f`, `p[i]` |
  |----------------------|-------------------|------------------------------------|
  | `i32 mut x`          | yes               | not applicable                     |
  | `point mut q`        | yes (and fields)  | not applicable                     |
  | `i32[4] mut a`       | yes (and elements)| not applicable                     |
  | `node* p`            | no                | no                                 |
  | `node* mut p`        | yes               | no                                 |
  | `node mut* p`        | no                | yes                                |
  | `node mut* mut p`    | yes               | yes                                |
  | `i32@ s`             | no                | elements: no                       |
  | `i32@ mut s`         | yes               | elements: no                       |
  | `i32 mut@ s`         | no                | elements: yes                      |
  | `node* mut@ t`       | no                | slots: yes, pointees: no           |
  | `node mut*@ t`       | no                | slots: no, pointees: yes           |
  | `node* mut* pp`      | no                | `*pp`: yes, `**pp`: no             |
  | `node mut** pp`      | no                | `*pp`: no, `**pp`: yes             |
  | `u8 mut@ mut* out`   | no                | `*out`: yes, bytes: yes            |
  | `u8@ mut* out`       | no                | `*out`: yes, bytes: no             |
  | `node*[4] mut t`     | yes (and slots)   | pointees: no                       |
  | `string mut s`       | yes               | never                              |

  Rationale: one rule with no exceptions, C's east-const (`int const x`, `node const* p`,
  `node* const p`) with the default inverted, and the same rule places `own` (D17.2); because
  reference suffixes read inside-out (D3.6), the binding's marker sits next to the name in every
  declaration, and no combination is unspellable. Amended 2026-09-10: until then a `mut` before
  the base type marked every level including the binding and a postfix `mut` marked the storage
  holding that pointer or header, which made the front `mut` mean the variable for scalars and
  the data for pointers, swapped C's positions, and left "writable target, fixed binding"
  unspellable. Amended 2026-09-10: spans were called slices (D3.5).
- **D5.4** Dropping mutability is the one implicit conversion. Level 0 (the receiving binding) is
  unconstrained. For a level `k >= 1`, mutability may be dropped only if every level between 1 and
  `k - 1` is immutable in the target type. So `node mut* mut@` converts to `node*@` and to `node
  mut*@`, and converting it to `node* mut@` is refused (a mutable slot could then hold a pointer to
  what the source still sees as a mutable node). This closes the C `T** -> const T**` hole with a
  short recursive check. Adding mutability requires `cast` (D3.14). Dropping `own` (D17.4) is the
  other implicit conversion and follows the same monotone shape.
- **D5.5** Struct fields. A field's own storage is as mutable as the struct value that contains it
  (level 0 of the field is inherited from the access path), so the outermost position of a field
  type never carries `mut`: `i32 mut count` and `node* mut next` are errors ("a field's own storage
  follows its struct"), while `node mut* next` marks the node behind the field. A return type has no
  binding, so its outermost position never carries `mut` either: `fn node mut* find()` is fine, `fn
  node* mut find()` and `fn i32 mut f()` are errors.
- **D5.6** Parameters. `mut` on a parameter follows D5.3; in the outermost position (`i32 mut n`,
  `node* mut p`) it makes the callee's local copy assignable. Function-type identity ignores that
  position (D3.10).
- **D5.7** Mutability of an lvalue (D6.7): a variable has its level-0 bit; `*p` and `p->f` have
  level 1 of `p`'s type; `e.f` and `e[i]` on a fixed array have the mutability of `e`; `s[i]` on
  a span has level 1 of `s`'s type; `str[i]` is immutable. Assignment, compound assignment,
  `++`, `--` and `&` producing a `T mut*` all require a mutable lvalue.
- **D5.8** `&e` has type `T*` where the level-1 bit is the mutability of `e` and deeper levels come
  from `e`'s type. `new` returns the storage it allocates writable at every level and the reference
  it creates `own`, with no outermost `mut` (an rvalue has no binding): `new(T)` returns `T mut*
  own`, `new(T, n)` returns `T mut@ own` (D17.3).
- **D5.9** Shallow model. Immutability of a variable never propagates through a pointer or span
  it contains; the levels behind an indirection are fixed by the type. `node n` with a field
  `node mut* next`: `n.value = 1` is an error, `n.next->value = 1` is allowed.

## D6 Expressions and evaluation

Owner: `core-language.md` (Expressions).

- **D6.1** Precedence, highest first: primary (`()` `[]` `.` `->` calls, span expressions,
  `cast`, `sizeof`, `new`, struct and array literals); unary (`! ~ - * &`); `* / % *%`; `+ - +% -%`;
  `<< >>`; `< <= > >=`; `== !=`; `&`; `^`; `|`; `&&`; `||`; `?:` (right-associative).
  Assignment is a statement, not an expression (D7.2). There is no comma operator and no unary
  `+`.
- **D6.2** Operand rules. Arithmetic `+ - * / %` and wrapping `+% -% *%`: both operands the same
  integer type, or the same float type (`%` and the wrapping forms are integer-only). Unary `-`:
  signed integers and floats only. Bitwise `& | ^ ~`: integers only, same type. Shifts `<< >>`:
  left operand any integer type, right operand any integer type or untyped constant; the result
  has the left operand's type; a constant count that is negative or at least the width of the
  left operand's type is a compile error; `>>` is arithmetic for signed and logical for unsigned
  types;
  `<<` discards the bits shifted out and never checks for overflow (`1 << 31` on `i32` is
  `-2147483648` in both build modes).
  Comparisons: same type, ordering only on integers, floats and `char`. `! && ||`: `bool` only.
  Every mixed-type operation is an error; there is no promotion, not even for `u8`/`i8`.
  "Same type" for comparison and `?:` operands means identical, mutability levels included;
  the implicit drop of D5.4 applies only to initialization, assignment, argument passing and
  `return`. Ownership is the exception: `==`, `!=` and `?:` operands lend, so an `own` and a
  non-`own` operand of the same underlying type compare, and `?:` yields an `own` value only
  when both operands are `own` rvalues or `null` (D17.4).
- **D6.3** Evaluation order is left to right for operands, arguments, and struct and array literal
  fields. `&&`, `||` and `?:` evaluate only what they need. For an assignment the target's
  address, including any index and its bounds check, is computed before the right-hand side;
  compound assignment computes the target once. Temporaries live until the end of the enclosing
  statement.
- **D6.4** Casts are `cast(expr, Type)`, a keyword form so the parser never has to guess whether a
  parenthesized name is a type. Semantics in D3.14. Rationale: user decision.
- **D6.5** Struct literals `point{1, 2}` (positional, every field, in order) and
  `point{.x = 1, .y = 2}` (designated, any order, omitted fields zeroed, no mixing with positional,
  no duplicates; designated form for structs only) are expressions. `point{}` is all-zero.
  Typed array literals `i32[3]{1, 2, 3}`
  must have exactly `N` elements or be `{}`. A bare `{...}` is allowed only as the initializer of
  a declaration (local, global, `for` init) whose type is a struct or array, and nested inside
  another literal; `= {}` zero-initializes any aggregate, span, string or enum; `i32 x = {};`
  is an error. Trailing commas are allowed in brace lists and enum bodies, not in parameter or
  argument lists. `IDENT {` is never a block because every control-flow condition is
  parenthesized and every body is braced.
- **D6.6** `?:` requires a `bool` condition and two operands of one type; untyped constants adopt
  the other operand's type.
- **D6.7** Lvalues: variables and parameters, module-level constants and globals, `*p`, `p->f`,
  `e.f` where `e` is an lvalue, `e[i]` where `e` is an lvalue fixed array or any span or string
  expression, and parenthesized lvalues (a constant is an immutable lvalue: addressable and
  the operand of a span expression, never assignable). `.len` and `.ptr` are never lvalues. Field
  access and indexing on an rvalue struct or array are allowed and yield rvalues (copied through a
  temporary). `&e` requires an lvalue.
  Returning the address of a local or a span of a local array is not diagnosed (documented
  undefined behavior, as in C).
- **D6.8** Indexing `e[i]`: `e` is a fixed array, span or string; `i` is any integer type or an
  untyped constant. Signed indices are sign-extended, unsigned zero-extended, and one unsigned
  comparison against the length catches negatives. Out of range is a runtime error (D10.6); a
  constant index out of range for a fixed array is a compile error. Pointers cannot be indexed.
- **D6.9** Span expressions `e[lo..hi]`, `e[lo..]`, `e[..hi]`, `e[..]`: `e` is a fixed array (lvalue
  only), a span, or a string; bounds are any integer type or untyped constants; the result is a
  span (or string) whose mutability is that of `e`'s elements. The runtime check is
  `0 <= lo <= hi <= len`, relative to the operand, not the original allocation. `p[lo..hi]` on a
  `T*` or `T mut*` produces a `T@` or `T mut@` with no check; this is the explicit unsafe
  escape for foreign memory. No span can be taken of a `void*`. Amended 2026-09-10: the operation
  was called slicing and its result a slice (D3.5).
- **D6.10** `->` is `(*p).f` and is required for pointers; `.` on a pointer is an error with a
  hint. Through a pointer to a span or string, `->` also reaches the `.len` and `.ptr`
  pseudo-fields (`out->len`). Indexing through a pointer to an array or span is written
  `(*p)[i]`, never `p[i]` (D10.4). Rationale: C familiarity and an explicit dereference.
- **D6.11** Calls: arguments are matched by position; no defaults, no named arguments, no
  overloading, no variadics. An `own` parameter takes ownership of its argument (D17.5): an
  `own` lvalue argument must be written `move(x)`, an `own` rvalue passes as it is. A function
  name, a function-pointer-typed expression and a qualified name `mod.f` are callable. A
  `noreturn` call is a terminating statement (D8.4).
- **D6.12** Floats follow IEEE 754: `NaN != NaN`, `-0.0 == 0.0`, division by zero yields
  infinities or NaN and never traps. There are no infinity or NaN literals; the standard library
  provides bit casts. `% ++ -- ~ & | ^ << >>` are errors on floats.
- **D6.13** Integer division truncates toward zero and `%` takes the sign of the dividend, as on
  x86-64 and in C. Division by zero, and `MIN / -1` and `MIN % -1`, are runtime errors in every
  build mode, checked explicitly at every width.

## D7 Statements

Owner: `core-language.md` (Statements).

- **D7.1** Declarations: `Type name = init;` and `mut Type name = init;`, one declarator per
  declaration, initializer mandatory (no definite-assignment analysis). A declaration is
  recognized as a type-looking prefix followed by an identifier (`grammar.md`, Disambiguation).
- **D7.2** Assignment `lv = e;`, compound assignment `lv op= e;` for `+ - * / % +% -% *% & | ^ <<
  >>`, and postfix `lv++;` `lv--;` (integer types only) are statements. They are not
  expressions: `a = b = c;` and `if (x = 5)` do not parse.
- **D7.3** Expression statements are calls only. `a * b;`, `x;` and `point{1, 2};` are errors. A
  call's result may be discarded. The empty statement `;` is an error. A bare block `{ ... }` is
  a statement and a scope.
- **D7.4** `if (cond) { } else if (cond) { } else { }`: parenthesized `bool` condition, mandatory
  braces on every branch. Rationale: mandatory braces remove the dangling-else and "goto fail"
  bug classes.
- **D7.5** Loops. `while (cond) { }`; `do { } while (cond);` (variables declared in the body are
  not visible in the condition); `for (init; cond; step) { }` where `init` is one declaration, an
  assignment statement, a call, or empty, `cond` is `bool` or empty (true), `step` is an
  assignment, `++`, `--`, a call, or empty; `for (;;)` is legal. The induction variable must be
  declared `mut` like any other (`for (mut i32 i = 0; i < n; i++)`); there is no exception.
  Range loop `for (T x : coll) { }` and `for (mut T x : coll) { }` where `coll` is a fixed array,
  span or string expression evaluated once before the loop (a fixed array is copied as a
  value); `x` is a fresh copy of each element, taken at the start of its iteration. `break` and
  `continue` target the innermost enclosing loop (`continue` in a `for` runs `step`). There is no
  labeled `break`.
- **D7.6** `switch (e) { case a, b: ... default: ... }`: `e` is an integer, `char` or enum type
  (not `bool`, not `string`, not a pointer); an untyped constant operand takes its default type
  (D4.5). Labels
  are constant expressions convertible to `e`'s type, no duplicates after evaluation; at most one
  `default`, in any position. Each case body is an implicit block scope with an implicit `break`
  at its end; there is no fallthrough; an empty case body does nothing (use `case a, b:` to share
  a body). `break` inside a case exits the switch (C semantics; the "break inside switch inside
  loop" trap is documented); `continue` targets the enclosing loop.
- **D7.7** A `switch` over an enum with no `default` must list every member; otherwise it is a
  compile error. Rationale: adding a member then finds every switch that needs updating.
- **D7.8** `defer` followed by an assignment, a `++`/`--` statement, a call statement, or a
  block (`grammar.md`, `defer_stmt`). The deferred code runs when the enclosing block is
  exited by any path: falling off the end, `return`, `break`, `continue`. The set of deferred
  statements that run at an exit is static: those textually before the exit in each exited block,
  innermost block first, in reverse order within a block. Nothing is captured at `defer` time; the
  statement is ordinary code executed at exit: after `defer del(p);`, a later `del(p);
  p = move(q);` makes the deferred statement free `q`. `return e`
  evaluates `e` before deferred code runs, so deferred code cannot change the returned value.
  `return`, `break` and `continue` inside deferred code, and `defer` at module level, are errors.
  Runtime errors (D11.4) do not run deferred code. Because `return x` of an `own` local empties
  `x` before the deferred code runs (D17.5, D17.6), `defer del(buf);` followed later by
  `return buf;` frees `buf` on every path except the one that hands it to the caller. Rationale:
  fixes the multi-return cleanup pitfall from memory-model.md with a purely static expansion, no
  runtime list.
- **D7.9** Scoping and shadowing. One namespace per module holds functions, structs, enums,
  constants, globals, externs and import bindings; any collision is an error. Lookup goes from the
  innermost block outward, then the module namespace, then the universe (D12). A local or
  parameter may not reuse the name of any enclosing local or parameter. It may shadow a
  module-level name (including an import binding) or a universe name, which is then
  inaccessible within its scope; a module-level declaration may likewise shadow a universe name.
  Rationale: otherwise `import std::io;` would forbid a parameter named `io` anywhere in the
  module. Enum members are not in the module namespace (D3.9). Sibling scopes may reuse names.
  A local's scope starts after its own declaration (`i32 x = x;` is an error).
- **D7.10** Module-level declarations. `Type NAME = init;` is a compile-time constant: it lives in
  read-only memory, is addressable, and is usable in array lengths and `case` labels.
  `mut Type g = init;` is a global in writable memory. Initializers must be constant expressions
  (D4.6) extended with `null`, function names, `&` of a module-level declaration from any module,
  and struct or array literals of those. No calls and no reads of `mut` globals, so there is no
  initialization order. Top-level declarations are order-independent within a module (no forward
  declarations); struct sizes and constant values are resolved lazily with cycle detection.
- **D7.11** `return e;` in a `void` function and `return;` in a non-`void` function are errors.
  A non-`void` function must end in a terminating statement (D8.4).

## D8 Functions

Owner: `core-language.md` (Functions).

- **D8.1** Declaration syntax: `fn ReturnType name(Type p1, Type p2) { ... }`, with `void` for no
  result: `fn void main() { }`. Function-pointer types read the same way: `fn i32(i32, i32)`.
  Rationale: user choice ("fn <ret> name(params)"); the keyword makes top-level and
  statement-level parsing unambiguous while the declaration still reads like C.
- **D8.2** Parameters are passed by value: primitives, pointers, spans and strings by copying the
  scalar or the fat pointer; structs and fixed arrays by copying the whole value. Results are
  returned by value likewise. There are no reference parameters; use `mut T*`.
- **D8.3** No nested functions, closures, overloading, default arguments, variadics or methods.
  Recursion is allowed; depth is bounded only by the OS stack.
- **D8.4** Terminating statements: `return`; a call to a `noreturn` function or to `panic`; an
  `if` with an `else` whose branches both terminate; `while (true)`, `for (;;)` or a `for` with an
  empty condition, with no `break` targeting it; a `switch` all of whose cases terminate and
  that either has a `default` or is an exhaustive enum switch (D7.7); a block whose last
  statement terminates. A non-`void` function body must end in a terminating statement or it is
  a compile error ("missing return", reported at the body's closing brace). Rationale: catching
  this at compile time is a core "better than C" promise, and the structural rule is a few dozen
  lines to implement.
- **D8.5** `noreturn` is a return type: `fn noreturn fatal(string msg) { ... }`. Such a function
  may not contain `return` and must end in a terminating statement; the compiler emits a trap
  after its body and after every call to it. `panic` and `sys.exit` are `noreturn`. Rationale:
  without it every error-reporting helper forces a dead `return` after each call.
- **D8.6** Entry point: the module given to `fort` must define `fn i32 main()` or
  `fn i32 main(string@ args)`. `args[0]` is the program name; each element is NUL-terminated
  because it comes from `argv`. The return value is the exit status. A `main` returning `void`
  is an error. A `main` in any other module is an ordinary function.

## D9 Modules, namespaces and FFI

Owner: `module-system.md`.

- **D9.1** One module per file; the module path is the file path relative to a search root, with
  `::` separating segments and `.ft` dropped: `std::io` is `<std>/io.ft`, `util::strings` is
  `<root>/util/strings.ft`. Every segment must be an identifier that is not a keyword, so the
  earlier `std::string` is `std::str`. The entry file is the exception: it is named on the command
  line rather than reached by an import path, so its base name need not be an identifier, and a name
  that is not one simply cannot be imported by anything (`007_case.ft` is the module `007_case`,
  whose name reaches the generated module only inside a quoted symbol, D9.7). Amended 2026-09-10:
  module-system.md required the entry base name to be a segment, which would have rejected every
  test file D14.4 names `NNN_name.ft`.
- **D9.2** Search roots, in order: the directory containing the entry file; each `-I` directory;
  the standard library directory. The first segment `std` is reserved for the standard library
  directory. The current working directory is never searched. Import paths are root-relative
  (a file in `util/` imports its sibling as `util::other`). A module's identity is the real path
  of its file; reaching one file through two different paths is an error.
- **D9.3** Import forms, only at the top of a file before any declaration:
  `import a::b;` binds the short name `b` to the module; `import a::b as c;` renames it;
  `import a::b::sym;` binds the symbol `sym` from module `a::b`; `import a::b::sym as alias;`;
  `import a::b::{s1, s2 as t};` is sugar for independent symbol imports of `s1` and `s2` from
  module `a::b` (the prefix must be a module; the braces never name modules). Resolution of a
  path `p::last`: the module reading (`p/last.ft` exists) and the symbol reading (`p.ft` exists
  and declares `last`) are both tried; exactly one must succeed, otherwise the import is an
  error: "not found" when neither reading succeeds, "has no declaration named" when only `p.ft`
  exists but lacks `last`, "ambiguous" when both succeed. No wildcard imports. A duplicate
  binding is an error;
  importing the same module under two names is allowed; import bindings are not re-exported.
- **D9.4** Qualified access uses a dot in expressions and in type positions: `io.read_file(p)`,
  `math.vector v = ...;`, `m.color.red`. Rationale: consistent with field access, and the
  resolver knows which identifiers are modules.
- **D9.5** Circular imports are a compile error even though whole-program compilation would
  permit them. Consequence, documented: mutually referential types must live in one module.
- **D9.6** Everything at module level is exported in v1. Visibility modifiers are deferred.
- **D9.7** Symbol names in the generated code are the module path joined with dots plus the
  declaration name: `std.io.read_file`, `main.main`. Dots are legal in ELF symbols and cannot
  appear in identifiers, so the scheme is injective. Runtime symbols are prefixed `fort_rt_`;
  the compiler emits `fort_entry` in the entry module (D11.6). `extern` names are unmangled.
  A dotted name is quoted in LLVM IR (`@"std.io.read_file"`), which is spelling only: the ELF
  symbol is unchanged, and quoting every dotted name keeps the emitter free of per-name
  analysis. Fort functions, constants and globals are `dso_local` with the default external
  linkage (D9.6), so fort-to-fort calls are direct and fort data is addressed PC-relative;
  `extern` and `fort_rt_*` symbols are not `dso_local` and are reached through the procedure
  linkage and global offset tables. Amended 2026-09-10 with D19: the assembler directives that
  spelled this became IR linkage words.
- **D9.8** `extern fn i64 write(i32 fd, void* buf, u64 n);` declares a C function with the
  System V x86-64 ABI. Extern signatures may use only integers, floats, `bool`, `char`, enums
  (passed as `i32`), pointers and function pointers: no spans, strings, structs or arrays, and
  no variadics. Every extern function is declared and called through a variadic LLVM function
  type (`declare i32 @printf(ptr, ...)`, called as `call i32 (ptr, ...) @printf(...)`), which
  makes the caller pass the vector-register count the ABI requires of callers of variadic
  functions, so a fixed-prototype declaration of a variadic C function is safe; a non-variadic
  callee ignores that count, so the same declaration is ABI-identical for it (D19.2). Extern
  call sites are `nobuiltin`, so no library-call rewriting replaces a symbol the program
  declared. Narrow integers and `bool` are normalized with zero- or sign-extension on both sides
  of the boundary, expressed as the `zeroext` and `signext` parameter and result attributes of
  D9.9, which normalize on both sides of the call by construction. C `char*` maps to `char*`
  (or `u8*`); `size_t` to `u64`; `ssize_t` and
  `off_t` to `i64`; `mode_t` to `u32`; `int` to `i32`; `long` to `i64`; `double` to `f64`. The
  same C symbol may be declared `extern` in several modules provided the signatures are
  identical, `own` qualifiers included (D17.13). Fort `char` is C's `unsigned char` at the
  boundary (`i8 zeroext`, D3.2). Amended 2026-09-10 with D19: the compiler itself set `al` to
  the vector-register count before every extern call, and extended narrow values by hand.
- **D9.9** Internal calling convention (v1 simplification): integers, pointers, `bool`, `char`,
  enums, function pointers and floats are passed and returned in registers per System V; every
  aggregate (struct, fixed array, span, `string`) is passed by a hidden pointer to a caller-made
  copy and returned through a hidden result pointer. Struct layout stays C-compatible, so
  pointer-based interop works. A fort function is usable as a C callback exactly when its
  signature is extern-legal, function-pointer parameters included (D3.10). In LLVM IR an
  aggregate argument is a plain `ptr` parameter, never `byval`, and an aggregate result is a
  leading `ptr sret(%T)` parameter on a function returning `void`; a span or `string` stays one
  hidden pointer and is never split into two scalars, so `fort_entry`'s C prototype
  (`const struct fort_span*`, D11.6) is literally true. `bool`, `char`, `u8` and `u16`
  parameters and results carry `zeroext`, `i8` and `i16` carry `signext`, and nothing wider
  carries an extension attribute, in fort and extern signatures alike (D9.8). Amended
  2026-09-10 with D19: the register-level spelling of the same convention is now LLVM's job, and
  the prototype read `const struct fort_slice*` while spans were called slices (D3.5).
- **D9.10** Whole-program compilation: the compiler walks the import closure from the entry file,
  type-checks every module, emits one LLVM IR module (D19.1), and runs `--cc` over it once to
  compile and link it with the runtime (D14.3). Interface files, separate compilation, a module
  cache and incremental rebuilds are deferred. Amended 2026-09-10 with D19: the compiler emitted
  one assembly file that the system C compiler assembled and linked.

## D10 Memory and runtime checks

Owner: `memory-model.md`.

- **D10.1** Stack: locals, parameters, fixed arrays and struct values; freed at scope exit. Heap:
  only through `new`; freed only through `del`.
- **D10.2** `new(T)` returns `T mut* own` to one zero-initialized `T`, for every `T` including an
  array (`new(u8[4])` is a `u8[4] mut* own`); `new(T, n)` returns `T mut@ own` of `n` zero-
  initialized elements (D17.3), where `n` is any integer type; a negative `n`, a size that
  overflows, or allocation failure is a runtime error; `n == 0` is allowed and yields a non-null
  pointer (the runtime allocates at least one byte). `new(T{...})`, `new(T@)` and `new(void)` are
  errors, and inside `new(...)` a `mut` never parses while an `own` parses only after a `*` (D17.3),
  so `new(T mut)` and `new(string own)` do not parse; `new(T*)` allocates one pointer slot and is
  legal. Rationale for zero-initialization: keeps "no undefined values" true at the cost of one
  `calloc`; the earlier "uninitialized" text is withdrawn. Amended 2026-09-10: the count was written
  inside the type (`new(T[n])`), which left no spelling for one array object, and a span was called
  a slice (D3.5).
- **D10.3** `del(x)` frees the allocation designated by its operand, which must have an `own`
  type; the full rules, including that `del` empties an lvalue operand and that `del` of a view,
  sub-span, `.ptr`, stack address or literal is a compile error, are D17.9. Allocation has no
  header, so `new`/`del` and C `malloc`/`free` are interchangeable; memory from C is adopted with
  `cast` (D17.3).
- **D10.4** No pointer arithmetic: `p + 1`, `p++` and `p[i]` are errors. The only ways to get a
  pointer are `null`, `&`, `new`, `.ptr`, `cast`, a function name, and calls; the only way to
  get a span from a raw pointer is the two-bound form `p[lo..hi]` (D6.9); `p[lo..]`, `p[..hi]`
  and `p[..]` are errors because a pointer has no length.
- **D10.5** `null` is the zero pointer and function-pointer value. `== null` and `!= null` are
  allowed only on pointers, `void*` and function pointers; spans and strings compare `.len`
  or `.ptr`. `null` has no type of its own: it is usable only where a pointer, `void*` or
  function-pointer type is expected, so `print(null)` and `null == null` are errors.
  Dereferencing `null` is undefined behavior (a segfault in practice).
- **D10.6** Bounds checks are performed on every index and span operation, in every build mode;
  `--no-bounds-check` disables them for benchmarking and is documented as unsafe.
- **D10.7** Undefined behavior in v1 is limited to: using a view, or a copy made before a
  `move` or `del`, after the allocation was freed; `del` of adopted memory (D17.3) that is not
  the start of an allocation; dereferencing `null` or a dangling pointer; writing through a cast
  that added mutability into read-only memory, `p[lo..hi]` beyond the object, calling a null
  function
  pointer, and data races. Everything else is defined or a diagnosed error. In particular there
  is no strict-aliasing rule: reading an object through a pointer to another type of the same
  size (`*cast(&x, u64*)` for an `f64 x`) is defined and yields the bit pattern.
- **D10.8** Frames larger than one page are probed so that a large local array plus recursion
  faults instead of skipping the guard page. The probing is requested with the
  `"probe-stack"="inline-asm"` attribute on every fort definition, so a module produced by `-S`
  carries the guarantee whatever the driver line is (D19.1). Amended 2026-09-10 with D19: the
  compiler emitted the page touches itself.

## D11 Build modes and the runtime contract

Owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).

- **D11.1** Two build modes. **Checked**, the default: `+ - *`, unary `-`, `++ --` and the
  compound assignments trap on overflow for signed and unsigned integers; a shift count that is
  negative or at least the operand width traps; storing over a live `own` value traps (D17.11).
  **Release** (`fort --release`): overflow wraps in two's complement, the shift count is taken
  modulo the width, and owned values are overwritten without a check. Programs must not rely on
  either behavior for correctness. Rationale: user decision ("trap in debug, wrap in release").
- **D11.2** `+% -% *%` (and `+%= -%= *%=`) wrap in both modes. They exist so hashes, checksums and
  counters can be written once and behave identically in both modes. Recommended addition,
  flagged for the user.
- **D11.3** Division by zero and `MIN / -1` (D6.13) are runtime errors in both modes.
- **D11.4** Runtime error contract: the runtime flushes buffered output, writes one line to
  stderr, and calls `abort()`, so the process dies with SIGABRT (status 134 under a shell).
  Formats: `<file>:<line>:<col>: runtime error: <message>` for checks (bounds, overflow, shift,
  division, allocation, ownership), `<file>:<line>:<col>: panic: <message>` for `panic`, and
  `<file>:<line>:<col>: assertion failed: <expression text>` for `assert`. Deferred code does not
  run. The check messages are fixed, with the offending values in decimal:

  | Check                              | Message                                            |
  |------------------------------------|----------------------------------------------------|
  | index                              | `index 5 out of range for length 3` (`-1` prints signed) |
  | span bounds                        | `span bounds 2..7 out of range for length 3`       |
  | overflow of `+ - *`, `++`, `--`, unary `-` | `integer overflow`                         |
  | shift count                        | `shift count 64 out of range for i64`              |
  | division or remainder by zero      | `division by zero`                                 |
  | `MIN / -1`, `MIN % -1`             | `division overflow`                                |
  | negative `new` count               | `negative allocation count -1`                     |
  | `new` size overflow                | `allocation size overflow`                         |
  | allocation failure                 | `out of memory`                                    |
  | overwriting a live `own` value (D17.11) | `overwriting owned value`                     |

  The end of a `noreturn` function is guarded by a trap (SIGILL, no message, D19.7), since a
  conforming body never reaches it. `<file>` is the path the compiler opened (search
  root as given plus the relative module path); the column of a check is that of its operator
  token, or of the builtin's name for `new`, `assert` and `panic`; the `assert` text is the
  source text of the expression, verbatim. Amended 2026-09-10: the bounds message read `slice
  bounds ...` while spans were called slices (D3.5).
- **D11.5** Output buffering: `print`/`println` write to a runtime buffer for stdout;
  `eprint`/`eprintln` are unbuffered; `fprint`/`fprintln` use one runtime buffer per descriptor,
  and `fprint(1, ...)` shares the stdout buffer with `print`. An `extern` write to a descriptor
  bypasses the buffers. Buffers flush when full, at exit, and before any runtime error. The
  runtime exports
  `fort_rt_flush(i32 fd)` and `fort_rt_flush_all()`; `io.close` and `io.flush` call the former,
  which is how a library call flushes a buffer the runtime owns.
- **D11.6** Process start: the C runtime owns `main(argc, argv)`, builds `string@ args`, calls
  the compiler-emitted `fort_entry(args)`, flushes, and exits with `status & 0xFF`.
  `fort_entry` takes the argument span by pointer and is the one compiler-emitted exception to
  D9.8's ban on aggregates at the C boundary; `toolchain.md` fixes its C prototype and the names
  of every other runtime entry point. Amended 2026-09-10: the prototype took a `const struct
  fort_slice*` and the failure was `fort_rt_fail_slice`, while spans were called slices (D3.5).
- **D11.7** Value formatting by the print family: integers in decimal; `bool` as `true`/`false`;
  `char` as its byte; `u8` as a number; enums as the member name, or the number if no member
  matches; pointers, `void*` and function pointers as `0x` plus lowercase hex (`0x0` for
  `null`); `string` as its bytes; floats as the shortest decimal that round-trips in the
  argument's own type (`f32` or `f64`), `%g`-style (exponent form below 1e-4 or at 1e17 and
  above, the exponent written as `e`, a sign, and at least two digits: `1e+21`, `1.5e-07`),
  with `.0` appended when the text has neither `.` nor `e`; `inf`, `-inf`, `nan`. No separators
  are inserted between arguments; each argument is evaluated and written in turn, left to
  right.

## D12 Builtins

Owner: `core-language.md` (Builtins).

- **D12.1** Keywords with type operands: `new(T)`, `new(T, n)`, `sizeof(T)`, `cast(e, T)`.
- **D12.2** Universe-scope functions with ordinary call syntax and special typing. They may be
  shadowed by a module-level or local declaration (D7.9) and cannot be used as values:
  - `del(x)`: frees an `own` operand and empties it (D17.9);
  - `move(lv)`: yields an owning lvalue's value and empties it (D17.6); the one universe
    function that yields a value, and that value must be used (`move(x);` as a statement is an
    error, D17.8);
  - `assert(cond)`: `bool` argument; failure is a runtime error with the expression text;
  - `panic(msg)`: `string` argument; `noreturn`;
  - `print(...)`, `println(...)`: zero or more arguments of integer, float, `bool`, `char`, enum,
    pointer or `string` type to stdout, `println` appends `\n`;
  - `eprint(...)`, `eprintln(...)`: the same to stderr;
  - `fprint(fd, ...)`, `fprintln(fd, ...)`: the same to the descriptor `fd` (`i32`).
  Each argument compiles to one per-type runtime call. An untyped constant argument takes its
  default type (D4.5). Universe functions other than `move` yield no value: they are usable
  only as call statements (including as `defer` operands and in `for` init and step positions).
  `assert` is active in both build modes.

## D13 Standard library scope

Owner: `stdlib.md`.

- **D13.1** The standard library is written in fort on top of `extern` declarations, plus the C
  runtime (`fort_rt_*`), which is permanent and is not a self-hosting goal.
- **D13.2** v1 modules: `std::sys` (exit, args, errno), `std::libc` (thin libc externs, named
  so that its short name does not collide with the common parameter name `c`), `std::mem`
  (copy, fill, equal), `std::io` (descriptors, read/write whole files and streams, close),
  `std::str` (compare, search, classify, parse integers, duplicate, NUL-terminated copies for
  C), `std::strbuf`
  (growable byte buffer), `std::vec` (`ptr_vec`, `int_vec`, the non-generic pattern), `std::strmap`
  (string-keyed open-addressing table), `std::math` (float bit casts, abs/min/max per type).
- **D13.3** Error handling idiom (the earlier TBD): functions return `bool` or an error enum, with
  results delivered through `mut T*` out-parameters; `-1`/`null` sentinels where conventional;
  `panic` for programming errors; `defer` for cleanup. No `Result` type in v1.
- **D13.4** The stdlib never passes spans, strings or structs across an `extern` boundary; it
  unpacks `.ptr` and `.len`. Functions that hand a path to C copy it into a NUL-terminated buffer.
  Amended 2026-09-10: spans were called slices (D3.5).
- **D13.5** Ownership in the library (D17): every function that allocates returns an `own`
  value (`str.dup`, `str.concat` and `strbuf.take` return `string own`, exact length, no NUL;
  `str.to_cstr` returns `char mut@ own` with a trailing NUL) or delivers it through an `own`
  slot (`io.read_file_bytes`); containers hold their
  storage as `own` fields (`str_buf { u8 mut@ own data; u64 len; }`, `ptr_vec`, `int_vec`,
  `str_map`) and expose a `free` function that `del`s them; out-parameters that receive
  ownership are pointers to `own` slots (`u8 mut@ own mut* out`), which the caller initializes to
  `{}` or `null`; `sys.args()` and every `view`-style accessor return borrowed values.

## D14 Toolchain and test conventions

Owner: `toolchain.md`.

- **D14.1** `fort [options] entry.ft`. Options: `-o <file>` (default `a.out`), `-S` (stop after
  emitting `<entry>.ll`, D19.1), `-c` (stop after the object file), `-I <dir>` (repeatable),
  `--std-dir <dir>` (default `$FORT_STD_DIR`, else `std` beside the binary),
  `--release` (D11.1), `--no-bounds-check` (D10.6), `-l<lib>` (passed to the linker),
  `--cc <path>` (default `clang`; it must be a clang, since it compiles LLVM IR),
  `--target <triple>` (default `x86_64-linux-gnu`, passed to `--cc` as `--target=<triple>`),
  `-Xcc <arg>` (repeatable, passed to `--cc` verbatim after the compiler's own arguments),
  `--help`, `--version`. Exit status: 0 success, 1 compile error, 2 usage, toolchain (`--cc`
  failed) or internal error; usage and toolchain errors are printed as `fort: error: <message>`.
  Amended 2026-09-10 with D19: `-S` emitted `<entry>.s`, `--cc` defaulted to `cc`, and
  `--target` and `-Xcc` did not exist.
- **D14.2** Diagnostics: `<file>:<line>:<col>: error: <message>` on stderr, one per line,
  optionally followed by `note:` lines. Errors without a position in the file (a missing
  `main`) use `1:1`. A lexical error is reported and lexing resumes at the start of the next
  line, dropping the line it stands on with the tokens already lexed on it, so a file reports at
  most one lexical diagnostic per line and its token stream still covers the rest of the file and
  ends at the end of it; the file is parsed from those tokens, so an editor keeps the declarations
  after a half-typed literal, and the parser recovers from whatever construct the missing line
  broke. A file with a lexical error is not checked.
  After a syntax error the parser reports it, skips to the next statement, clause, field or
  declaration boundary and parses on, so a file reports one diagnostic for each construct that
  failed and not only for the first: at most 20 per file counted across its lexical and its
  syntax errors together, never two in a row at one position,
  and none for a construct whose tokens an earlier skip had already dropped. The skipped tokens
  are held in the tree as an error node that every later pass skips. A file with a syntax error
  is parsed whole and not checked. Every module of the closure is checked in dependency order; a
  declaration whose check failed has the error type, which silences every later diagnostic
  involving it, so an importer sees only its own errors. Positions of errors that concern a
  whole construct: "missing return" and a non-exhaustive enum `switch` are reported at the
  closing brace of the body or `switch`; an infinite-size struct at its `struct` keyword; a
  shadowing error at the inner declaration ("'n' shadows a parameter", "'n' shadows an
  enclosing local"). The compiler never emits warnings in v1. Amended
  2026-09-10 with D20: a syntax error stopped the file after one diagnostic with no recovery,
  and only the semantic errors of the first module that had any were reported. Amended
  2026-09-10 with T-062: a lexical error stopped the compilation of the file after one
  diagnostic, the file was never parsed, and the cap of 20 counted syntax errors alone.
- **D14.3** Generated code is LLVM IR (D19.1), compiled and linked by `--cc` in one invocation,
  `<cc> --target=<triple> -O1 -fPIE -pie -Wno-override-module -o <out> <entry>.ll
  <std-dir>/fort_rt.o [-l<lib>...] [<-Xcc args>...]`, `-O2` in place of `-O1` under `--release`
  and `-c` before `-o` when the compiler stops at the object; the executable is
  position-independent and the runtime object is compiled for the same triple. Amended
  2026-09-10 with D19: generated code was GNU assembly, assembled and linked by the system C
  compiler.
- **D14.4** Language tests live under `test/lang/`: `run/<area>/NNN_name.ft` (compile, run,
  compare), `fail/<area>/NNN_name.ft` (must not compile), where `<area>` is one of `lexical
  constants operators casts mutability ownership declarations control switch defer functions
  structs enums arrays spans strings pointers globals builtins errors modes modules ffi stdlib`,
  plus
  `run/modules/<name>/main.ft` and
  `fail/modules/<name>/main.ft` for multi-file tests (the harness compiles `main.ft` with the
  directory as root and collects `error` annotations from every `.ft` file in it), `ffi/*.c`
  helpers, `programs/*.ft` for larger programs. The harness invokes `fort` on the test file
  itself (`main.ft` for multi-file tests) with `test/lang` as the working directory, so `<file>`
  in diagnostics and runtime errors is the path relative to `test/lang`. Directory tests exist
  only under `modules`. Verdicts: PASS, FAIL (the program or compiler misbehaved), ERROR (the
  compiler exited 2, crashed, timed out, or the harness could not link or start the program),
  XFAIL and XPASS for tests listed in `xfail.txt` (an ERROR on a listed test is XFAIL too).
  Compiler unit tests in C live in `test/` using `test.h`.
- **D14.5** Test file directives, all at the top of the file (`//!`) except `error`; in a
  multi-file test only `main.ft` carries directives and sibling modules carry none:
  - `//! run` or `//! fail` (required, first line);
  - `//! flags: --release` (extra compiler flags);
  - `//! args: a b c`; `//! link: ffi/helpers.c` (repeatable, relative to `test/lang`);
  - `//! stdin:` followed by `//< ` lines;
  - `//! stdout:` followed by `//| ` lines: the expected output is each line's text after
    `//| ` followed by a newline (a bare `//|` is an empty line); compared exactly, trailing
    spaces included; output without a final newline cannot be expressed, use `println`;
  - `//! exit: N` (default 0) or `//! abort` (expect SIGABRT);
  - `//! stderr: <substring>` (repeatable; each must appear in stderr); `flags`, `args`,
    `exit`, `abort`, `stdin` and `stdout` appear at most once, `link`, `stderr` and `error-any`
    any number of times; a `fail` test carries at least one `error` or `error-any`;
  - in `fail` tests, `//! error: <substring>` at the end of the offending line; every such line
    must produce a diagnostic on that line containing the substring, and no unannotated
    diagnostic may occur; `//! error-any: <substring>` at the top for errors without a useful
    line (for example circular imports).
- **D14.6** Coverage target from the prompt: about three lines of test for each line of compiler.
  The implementation plan will size the corpus; the seed tests written in this phase establish
  the format and one example per feature area.

## D15 Not in v1

Deferred deliberately. The specification lists each with the idiom to use instead.

Generics; untagged unions (idiom: a fat struct with a kind field); tagged unions and `Result`
(idiom: D13.3); methods; closures and nested functions; variadic functions; overloading; default
and named arguments; visibility modifiers; type aliases; integer-range `for`; struct, array and
span equality; definite-assignment analysis (idiom: initialize with `{}` or a sentinel);
alignment and packed attributes (idiom: an opaque `u8[N]` field and a C shim); separate
compilation and interface files; conditional compilation; labeled `break` (idiom: a flag or a
helper function); raw strings; a blank identifier; compile-time function evaluation; `alignof`;
`sizeof(expr)`; array suffixes after a trailing pointer suffix (`i32[4]*[2]`, idiom: a struct);
string `switch`; linear ownership, that is compile-time detection of leaks and of use after
`move` (idiom: `defer del`, and the zeroing that `move` and `del` leave behind, D17); `goto`
(never). Amended 2026-09-10: spans were called slices (D3.5).

Deferred on the toolchain side (user decision, 2026-09-10): building the module in process
through the LLVM C API, and everything that would come with it (a JIT, per-function control of
the pass pipeline, layout and target queries answered by LLVM rather than by the compiler's own
tables). The compiler appends IR text instead (D19.1) with one helper per instruction shape, so
that a later move to the API is a mechanical substitution rather than a rewrite; the idiom
meanwhile is `opt` and `llc` on the `-S` output.

## D16 Hazards not to re-litigate

Findings from the design reviews that look like bugs but are deliberate.

- Level-0 mutability is excluded from the monotone drop rule (D5.4) on purpose: the receiving
  variable's own mutability cannot alias anything.
- `.len` of a span is `u64` and nothing converts implicitly, so `for (u64 mut i = 0; ...)` is the
  idiom; `.len` of a fixed array is an untyped constant precisely to soften this. Amended
  2026-09-10: spans were called slices (D3.5).
- `print('a')` prints `a` and `print(cast('a', u8))` prints `97`, because `char` is distinct.
- `new(T)` returns `T mut* own` (D17.3); the earlier example `Point* p = new(Point);
  p->x = 10;` is now an error and must read `point mut* own p`.
- `for (i32 i = 0; ...)` is an error; the induction variable needs `mut`.
- Unsigned subtraction traps in checked mode (`len - 1` on an empty span); test before
  subtracting or use `-%` when wrapping is intended.
- `defer` captures nothing; after `defer del(p);`, a later `del(p); p = move(q);` frees `q`.
- `break` inside a `switch` inside a loop exits the switch, not the loop.
- Symbol mangling uses dots, not double underscores, because `a__b` is not injective.
- `extern` signatures exclude aggregates so the compiler does not need System V aggregate
  classification in v1.
- Generated code must be position-independent: `--cc` is invoked with `-fPIE -pie` (D14.3) and
  the module names no absolute address; do not rely on `-no-pie`.
- The emitter appends LLVM IR text and never links or calls libLLVM (D19.1); the C API is
  deferred (D15), so no build of the compiler ever needs LLVM's headers or libraries.
- Never emit `nsw`, `nuw` or `exact`. Release mode's wrapping is defined (D11.1), so a
  poison-producing flag would be a miscompile waiting to happen, and in checked mode the check
  has already proved the absence of overflow, so the flag would buy nothing.
- Never emit a fast-math flag (`fast`, `nnan`, `ninf`, `reassoc` and the rest) on a float
  instruction: floats are IEEE 754 with `NaN != NaN` and no reassociation (D6.12).
- Never emit `!tbaa`. There is no strict-aliasing rule (D10.7), and emitting no type-based alias
  metadata is what makes that true by construction, without `-fno-strict-aliasing` on the `--cc`
  line.
- `own` and `mut` follow one placement rule (D5.3, D17.2): each marks the reference or storage
  of the type element it follows, and nothing precedes the base type. There is no front position
  to give a second meaning to.
- `move` and `del` empty an immutable binding; that is ownership ending, not an assignment
  (D17.6).
- `node mut* n = new(node);` is an error: an owning temporary must land in an `own` place
  (D17.8).


## D17 Ownership

Owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
`del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
statements). Added after the v1 design review at the user's request; wherever an earlier
decision or document says ownership is "by convention", this section supersedes it.

- **D17.1** `own` is a qualifier on reference types: `T* own`, `void* own`, `T@ own`,
  `string own`. It states that the reference designates the start of a live allocation obtained
  from `new` (or adopted with `cast`, D17.3) and that `del` on it is meaningful. It is erased at
  run time (same bits, layout and ABI) and is part of type identity: `node* own` and `node*` are
  different types, as are `fn void(node* own)` and `fn void(node*)`. `own` on a non-reference
  type (`i32 own`, `point own`, `i32[4] own`) or on a function-pointer type is an error; an
  array or struct that *contains* an `own` reference is an owning aggregate (D17.7).
- **D17.2** Placement. An `own` follows a `*` or an `@` and marks the reference that suffix
  introduces as owning its target; `string`, a reference without a suffix (D3.7), takes it directly
  (`string own name`). It precedes `mut` in the position (`node* own mut p`, D5.3), never follows a
  non-reference base type or a fixed-array suffix (`node own*` and `node*[4] own` are errors; `node*
  own[4] t` is four owning pointers), and nothing precedes the base type, so every type has one
  spelling. The outermost reference is the one the binding holds, so the `own` before the name says
  the binding owns what it refers to: `node* own p` and `u8@ own buf` are what `del(p)` and
  `del(buf)` require (D17.9). Each `own` marks one reference only: the safe failure mode for `node*
  mut@ own kids` is that `del(kids[i])` does not compile when the nodes belong to someone else (an
  arena, say). Amended 2026-09-10 with D5.3: until then an `own` before the base type marked the
  outermost reference and `node* own p` was an error. Amended 2026-09-10, separately: spans were
  called slices (D3.5).

  | Declaration                    | Meaning                                                  |
  |--------------------------------|----------------------------------------------------------|
  | `u8 mut@ own buf`              | owned span of writable bytes                             |
  | `u8@ own data`                 | owned span, read-only through this binding               |
  | `node mut* own n`              | owned writable node; `n` itself is fixed                 |
  | `node* mut@ own items`         | owned span of borrowed pointers, entries assignable      |
  | `node mut* own mut@ own kids`  | owned span of owned writable nodes, entries assignable   |
  | `node* own@ view`              | borrowed span of owned nodes                             |
  | `u8 mut@ own mut* out`         | borrowed pointer to an owned slot (an out-parameter)     |
  | `string own name`              | owned immutable characters (`str.dup`, `strbuf.take`)    |

- **D17.3** Producers. `new(T)` yields `T mut* own` and `new(T, n)` yields `T mut@ own`, for any `T`
  that is not itself `own`, `mut` or a reference to `void` (`new(u8[4], n)` is `u8[4] mut@ own`,
  `new(node* own, n)` is `node mut* own mut@ own` whose slots are null; the result is always `own`
  and writable at every level, D5.8); standard-library functions that allocate return `own` (D13.5).
  `cast` may add `own` to a pointer or span, adopting memory that came from C (`cast(p, u8 mut*
  own)` for a `void*` from an extern that does not say `own`, the same unsafe escape as adding
  `mut`), and may drop it; the target type of a cast decides (D3.14). Span expressions (`buf[..]`,
  `buf[lo..hi]`) and `.ptr` always yield views, as do `&`, literals and the runtime's `args`.
  Amended 2026-09-10: the count moved out of the type, `new(T[n])` to `new(T, n)`.
- **D17.4** Lending. `own X` converts implicitly to `X` wherever a value meets an expected type,
  like dropping `mut` (D5.4); the two drops combine (`u8 mut@ own` to `u8@`). Dropping `own`
  at a level `k` is allowed only if every level between 1 and `k - 1` is immutable in the target
  (the D5.4 shape: otherwise `node* mut@ w = kids; w[0] = &local;` would let `del(kids[0])`
  free a stack address) and only if no outer level keeps `own` (`node mut* own mut@ own` to
  `node mut* mut@ own` is an error, since the inner objects would then be owned by nobody); lend the
  whole thing instead. Operands of `==`, `!=` and `?:` lend, so `own` never blocks a comparison;
  `?:` yields `own` only when both operands are `own` rvalues or `null` (D6.2).
- **D17.5** Transfer. Copying an `own` **lvalue** into an `own` place (a declaration's
  initializer, an assignment, an `own` parameter, an `own` element or field of a literal, a
  `return` operand that is not a local) requires `move(lv)`. An `own` **rvalue** (`new(...)`, a
  call result, `move(...)`, `cast(...)` to an `own` type) flows into an `own` place without it.
  `return x` where `x` is a local variable or parameter of `own` type is an implicit `move`.
- **D17.6** `move(lv)` is a universe function (D12.2). Its operand is an lvalue of owning type
  (an `own` reference or an owning aggregate, D17.7); it yields the operand's value and sets the
  operand to its zero value (`null`, `{null, 0}`). Emptying is not an assignment: the binding
  need not be `mut`, and after the move it still cannot be assigned to unless it is. When the
  operand is reached through an indirection (`*p`, `p->f`, `s[i]`), that level must be mutable,
  because the move changes storage that others can see: `move(v[0])` on a `node* own@ v` is an
  error, since nothing may be taken out of what was only lent. Fields and elements of a local
  value count as the local. Moving a zero value yields a zero value. `move` and `del` of a
  module-level constant (D7.10) are errors: it lives in read-only memory.
- **D17.7** Owning aggregates. A struct or fixed array that contains an `own` reference by value
  (directly or through nested aggregates) is owning. Copying an owning lvalue into an owning
  place (initialization, assignment, a by-value parameter, a literal element) requires `move`;
  returning a local owning value is an implicit move. Functions therefore take `vec*` or
  `vec mut*`. `del` of an aggregate is an error: `del` is shallow, and a struct frees its own
  fields.
- **D17.8** Temporaries must land. An `own` rvalue may only be bound to an `own` place, passed
  to an `own` parameter, or `del`ed. Anything else is a compile error ("owning temporary would
  leak"), because nothing could ever `del` it: converting or casting it to a non-`own` type
  (`node mut* n = new(node);`, `use(str.dup(x))`, `cast(new(node), node*)`), taking a span of it,
  indexing or taking `.ptr` of it (`new(u8, 8)[..4]`, `new(i32, 2)[0]`), accessing a field of an
  owning aggregate rvalue, and discarding it as an expression statement (`move(x);`, `str.dup(s);`).
- **D17.9** `del(x)` requires an `own` operand of any mutability: an `own` pointer, `void* own`,
  `own` span or `string own`, as an lvalue or an rvalue. On an lvalue, `del` empties the operand
  under the rules of D17.6, with the same mutability requirement through indirections; on an
  rvalue it only frees. `del(null)` (the literal adopts `void* own`) and `del` of a zero span or
  string are no-ops, so
  `del(buf); del(buf);` frees once, and a use after `del` or `move` dereferences `null`. `del`
  of a view, a sub-span, a `.ptr`, a stack address or a literal is a compile error, because
  none of them has an `own` type. This supersedes the earlier "del does not null its argument".
- **D17.10** Loops. The collection expression of a range `for` lends: an owning collection
  (an `own` span, or an owning fixed array) is iterated in place, never moved or copied. The
  loop variable's type is the element type with its outermost `own` removed (`for (node mut* c
  : kids)` over `node mut* own mut@ own kids`); declaring it `own` is an error, and elements that
  are owning aggregates cannot be copied into a loop variable at all, so such a collection is
  iterated by index. Moving an element out is written explicitly, `move(kids[i])`.
- **D17.11** Overwrite check. In checked builds (D11.1), storing into an `own` reference-typed
  lvalue whose current value is not the zero value is a runtime error, `overwriting owned
  value` (D11.4), because the previous allocation would leak. `del` and `move` leave zero
  behind, so `del(v.data); v.data = new(...)`, `a = move(b)` after `move(a)`, and initialization
  from `{}` or `null` all pass. The check runs after the right-hand side is evaluated,
  immediately before the store, and is reported at the `=` token. Release builds store without
  checking. Assignments of owning aggregates are not checked field by field.
- **D17.12** Strings. `string own` is an owned, immutable character sequence: `str.dup`,
  `str.concat` and `strbuf.take` return it; literals, sub-strings and `sys.args()` are `string`.
  `del(string own)` is legal and `del(string)` is not. A string built in a `u8 mut@ own`
  becomes a `string own` with `cast(move(buf), string own)` (the target says `own`, so the
  source must be moved, D3.14); `cast(buf, string)` lends a view instead.
- **D17.13** `own` may appear in `extern` signatures. It is erased, and it documents the C side's
  convention: `extern fn void* own malloc(u64 n);` (`void*` has no target level, so no `mut` after
  `void`, D3.11), `extern fn void free(void* own p);`. Signature identity includes `own` (D9.8).
- **D17.14** Not tracked, exactly as in C: a view, or a copy made before a `move` or `del`,
  used after the allocation was freed; an `own` value that is never freed; two `own` copies made
  through `cast`. The linear check that would make leaks and use after `move` compile errors is
  deferred (D15); this design is its intended base and adds no syntax it would not need.

## D18 Float printing in the runtime

Owner: `toolchain.md` (5.1 entry points). Names the entry points that produce D11.7's float text
and settles what D11.7 leaves to the runtime.

- **D18.1** The runtime exports exactly two float entry points, `fort_rt_print_f32(i32 fd, f32 v)`
  and `fort_rt_print_f64(i32 fd, f64 v)`. The print family (D12.2) calls one of them per float
  argument and passes the value in the argument's own type: an `f32` is never widened to `f64`
  first, because the digits printed depend on the type (D11.7).
- **D18.2** Shortest round-trip. The digits are the shortest decimal that reads back as the value
  in that type and, among the strings of that length that read back as it, the one nearest the
  value, ties going to the even last digit. That text is what this decision fixes; how a runtime
  arrives at it is an implementation matter, described for the C runtime in `toolchain.md` 5.1.
  The layout around the digits is the runtime's own work, so the bytes are D11.7's and not a C
  library's `%g`.
- **D18.3** Layout. D11.7's choice of form is decided on the decimal exponent `e` of the leading
  digit: exponent form when `e < -4` or `e >= 17`, fixed form otherwise, so `1e-4` prints
  `0.0001` and `1e16` prints `10000000000000000.0`. `-0.0` prints `-0.0`, its sign taken from the
  sign bit (D6.12: `-0.0 == 0.0`, so no comparison can). A NaN prints `nan` whatever its sign bit
  and payload, since D11.7 lists `inf`, `-inf` and `nan` and no `-nan`.
- **D18.4** No other float entry point. The conversions of D3.14 (float to integer, saturating
  with NaN to zero, and integer to float) are emitted in the module the compiler generates
  (D19.1), not called for, so the list in `toolchain.md` 5.1 stays complete (D11.6).

## D19 Target: LLVM IR

Decided 2026-09-10, before any code-generation ticket had started; the earlier target, x86-64 GNU
assembly, survives only in the history of this file and of `toolchain.md`.

- **D19.1** The compiler emits one textual LLVM IR module (`.ll`, LLVM 18 syntax, opaque
  pointers) for the whole program (D9.10), built by string appending in one forward pass, and
  hands it to clang (D14.3). The compiler never links libLLVM or calls its C or C++ API: the
  bootstrap stays a dependency-free C11 program and the self-hosted compiler needs no foreign
  bindings. The module carries `target triple = "x86_64-unknown-linux-gnu"` and no datalayout,
  module flags, comments or `source_filename`. Every emitted module must pass `opt
  -passes=verify`; the language-test harness checks that (`run_tests.py --verify-ir`) and the
  pipeline test checks its hand-written samples under `test/ir/`, which are the reference for
  the form of a module until the contract below says otherwise.
- **D19.2** Type mapping. `i8 i16 i32 i64` and `u8 u16 u32 u64` are the LLVM types `i8 i16 i32
  i64`: signedness lives in the instruction (`sdiv` against `udiv`, `sext` against `zext`,
  `icmp slt` against `icmp ult`) and never in the type (D3.1). `char` is `i8` compared and
  extended as unsigned (D3.2). `bool` is `i1` as a value and `i8` in memory (D3.3), so a `bool`
  field or global has the layout and the two values of C's `_Bool`: every load of a `bool` place
  is a `load i8` and a `trunc`, every store a `zext` and a `store i8`. `f32` and `f64` are
  `float` and `double`. Every pointer, `void*` and function pointer is the opaque `ptr` (D3.11,
  D3.10); the pointee type is the compiler's business and appears only on the instructions that
  need it, so a function pointer is a `ptr` and a call through one an ordinary `call`. `T[N]` is
  `[N x T]` (D3.4); a span and `string` are the one type `%fort.span = type { ptr, i64 }`
  (D3.5, D3.7); a struct is `%struct.<dotted name>` with its fields in declaration order and
  never `packed`, because LLVM lays that type out exactly as C does (D3.8); an enum is `i32`
  (D3.9); `void` is `void` and only a result type. Float to integer uses the saturating
  intrinsics of D3.14 and every other conversion the obvious cast instruction. Amended 2026-09-10:
  the IR type was `%fort.slice` while spans were called slices (D3.5).
- **D19.3** Aggregates live in memory. Only scalars are SSA values: a struct, fixed array, span
  or `string` always occupies a place (an `alloca`, a global, or memory reached through a `ptr`)
  and is never loaded or stored as one value, so the emitter never writes `insertvalue` or
  `extractvalue` on a fort aggregate (its only `extractvalue` takes apart the `{iN, i1}` that an
  overflow intrinsic returns, D19.6). Copying an aggregate is `llvm.memcpy`, zeroing one is
  `llvm.memset` and reaching a field or element is `getelementptr`. This is D9.9's model spelled
  in IR, and it is what keeps code generation one tree walk with a destination place per
  expression (D19.1). Amended 2026-09-10: spans were called slices (D3.5).
- **D19.4** Entry-block allocas and SSA discipline. Every local, parameter copy and compiler
  temporary is an `alloca` in the entry block, before any other instruction and in declaration
  order, because LLVM's promotion passes look only there; no `alloca` is variable-length (array
  lengths are constants, D3.4, and `new` is the heap, D10.2). The emitter builds no `phi` and
  carries no value across a merge point: a temporary is used only in the block that defines it
  or in a block that block dominates, and `&&`, `||` and `?:` short-circuit through a stack slot
  (D6.3) that the optimizer promotes. Control flow is explicit blocks: `br` for `if`, `while`,
  `for`, `break` and `continue`; an LLVM `switch` with one case per label and a default block
  for a fort `switch` (D7.7); a fresh block for the statements D14.2 allows after a terminating
  one. Together with D19.3 this is what keeps the module verifier-clean without any analysis in
  the emitter.
- **D19.5** The emitted text is a function of the program alone, so that a self-hosted compiler
  reaches a fixpoint: stage2 and stage3 must emit byte-identical modules for the same sources in
  both build modes, and the bootstrap script compares them with `cmp` over `-S` output, with
  `diff` as the debugging output. Hence: every value, parameter and block is named, so LLVM
  never numbers anything implicitly; per function and reset at each definition, instruction
  results are `%t<N>` in emission order, blocks are `%L<N>` in creation order with the entry
  block always literally `entry`, locals are `%<ident>.<slot>` by the local's index in the
  function, parameters arrive as `%<ident>.in`, an aggregate result pointer is `%ret.sret` and
  compiler-made places are `%tmp<K>` from a third per-function counter. A name that embeds a
  fort identifier always contains a dot and a name the compiler invents never does, which is
  what makes collisions impossible: an identifier cannot contain a dot (D2.3), so a local named
  `tmp` is `%tmp.0` and never `%tmp0`, and `%ret.sret` cannot be a local named `ret`, whose
  names are `%ret.<slot>` and `%ret.in`.
  Module-level counters (`@.str.<N>`, `@.file.<N>`) are assigned on first use and never
  deduplicated by content; enum tables are keyed by the mangled name. Order is by construction
  and never by iteration over a hash table: modules in dependency order, declarations in source
  order, runtime declarations in the fixed order of `toolchain.md` 5.1, intrinsics in a fixed
  table order, `extern` declarations in first-use order, attribute groups at fixed indices with
  unused indices simply absent. Nothing in the text depends on the environment: no timestamps,
  no compiler version, no comments, no `!llvm.ident`, and no path other than the ones D11.4
  prints, which `@.file.<N>` holds exactly as the compiler opened them, so comparing two stages
  means invoking them identically (same working directory, same arguments) rather than expecting
  path-free text. An integer constant is printed in decimal without padding and with the
  signedness of its fort type (`store i8 -1` for an `i8`, `store i8 255` for a `u8`, and `i64`
  MIN as `-9223372036854775808`); a float constant is printed as the LLVM hex literal of its
  `double` bit pattern, an `f32` constant converted to `double` first, so no decimal rounding
  can differ between two stages.
- **D19.6** Checks and failure blocks. Every runtime check (D10.6, D11.1, D11.3, D17.11)
  computes one `i1` that is true on failure and branches with the failure block as the first
  label; `assert` is the exception, since its operand is already the success condition (D12.2).
  Failure blocks are emitted after every normal block of the function, in ascending label order;
  each holds exactly one call to the `toolchain.md` 5.1 entry point, with the offending values,
  the file constant and the line and column of D11.4's position rule, followed by `unreachable`.
  The failure entry points are declared `cold noreturn nounwind`: `noreturn` is truthful, since
  every one of them is `_Noreturn` and aborts, and `cold` lays the block out of line, which is
  what the out-of-line failure stubs used to do. `--no-bounds-check` removes exactly the index
  and span branches (D10.6).
- **D19.7** The trap D8.5 requires after the body of a `noreturn` function and after every call
  to one is `call void @llvm.trap()` followed by `unreachable`. `llvm.trap` is `ud2` on x86-64,
  so D11.4's SIGILL with no message is unchanged; `unreachable` alone is not a trap, since LLVM
  is free to let control fall through it, which is why the call is emitted and not just the
  terminator.

## D20 Editor support

Decided 2026-09-10. Owner: `toolchain.md` (4). An editor underlines the construct a diagnostic is
about and jumps to the name a declaration introduces, so both are recorded from the parser on.
Only this preamble and D20.4 are decided here; the check mode, the JSON form, the identifier index
and the language server (D20.1 to D20.3, D20.5) land with the check mode, after the checker.

- **D20.4** Ranges. A position is a range: from the first byte of its first token to one past the
  last byte of its last token, the start inclusive and the end exclusive, both 1-based byte
  columns with a tab counting as one column (D14.2). No token spans lines (D2.9), so the range of
  a single token ends on the line it starts on. Every syntax-tree node carries the range from its
  own anchor token, which is its first token except on the nodes named after an operator, where it
  is that operator (`toolchain.md` 4), to the end of the last token of the construct; the
  runtime-error position of `toolchain.md` 4 is the start of that range and is unchanged. A node
  whose start cannot move over the opening delimiter of the construct does not stretch over the
  closing one either: a parenthesized expression is the inner expression's own node, anchored at
  its own first token or at its operator, so it covers neither parenthesis rather than covering
  the `)` alone and reading as a range that begins inside the text it is about. Every node that a
  name declares or mentions also carries the range of that name token, and a node without a name
  carries the empty range for it. A node with no token of its own, such as the implicit block of a
  case body (D7.6), begins as the empty range where the construct it stands for begins. Extending
  a range moves its end to the later of the two ends and never moves its start, so extending it
  again with a token it already covers changes nothing and a node anchored at its operator never
  ends up with an end before its start. An error without a position in the file is the empty range
  at 1:1 (D14.2). The text form of D14.2 prints the start only, so no diagnostic text changes.

## Ready-to-implement checklist

- [x] Every TBD in the original notes has a decision above.
- [ ] `grammar.md` covers every construct named here.
- [ ] Each specification document cites the decisions it implements and contains no "TBD",
      "pending" or "not finalized".
- [ ] Every operator, keyword and builtin appears in at least one seed test under `test/lang/`.
- [ ] Runtime behavior of every check is stated for both build modes.
- [ ] ABI, layout and mangling are stated.
- [ ] Standard library signatures are fixed.
- [ ] CLI, diagnostics and test conventions are fixed.
- [ ] Ownership (D17) is reflected wherever a document mentions `new`, `del`, heap memory or a
      function that allocates, and every `own` rule has a run test and a fail test.
