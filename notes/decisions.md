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
  with an exponent (`1e10`). `1.` and `.5` are not literals. No suffixes; `f32` values come from
  context (D4).
- **D2.7** Char literals: `'x'` where `x` is one printable ASCII character other than `'` or `\`,
  or one escape. A non-ASCII byte in a char literal is an error ("use a string").
- **D2.8** Escapes, in char and string literals: `\n \t \r \0 \\ \' \" \xHH` (exactly two hex
  digits). Anything else after `\` is an error.
- **D2.9** String literals: `"..."` with escapes; a raw newline inside is an error; no
  adjacent-literal concatenation; no raw strings.
- **D2.10** Operators and punctuation:
  `+ - * / % +% -% *% = += -= *= /= %= +%= -%= *%= &= |= ^= <<= >>= == != < <= > >= && || ! & | ^ ~
  << >> ++ -- ? : :: . -> .. ( ) [ ] { } , ;`. Longest match wins (`+%=` before `+%` before `+`).
  `%` is never a prefix operator, so `+%` is unambiguous. `>>` and `<<` are single tokens.
- **D2.11** Nesting of blocks, parentheses, brackets, braces and type suffixes deeper than 256 is a
  compile error, so a recursive-descent compiler written in fort never needs an unbounded stack.

## D3 Types

Owner: `type-system.md`.

- **D3.1** Primitive types: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char void`. Sizes: 1, 2,
  4, 8 bytes for the integers, 4 and 8 for the floats, 1 for `bool` and `char`; alignment equals
  size. Pointers, function pointers, slices and strings align to 8; arrays to their element;
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
  `a.len` is an untyped integer constant. Fixed arrays have no `.ptr`.
- **D3.5** Slices `T[]` replace the earlier "dynamic array". A slice is a fat pointer
  `{T* ptr; u64 len}`; whether it owns its elements is part of its type (`own`, D17); all slices
  of the same element type, element mutability (D5) and ownership are one type; the zero value
  is `{null, 0}`. `.len` (type `u64`) and `.ptr`
  (a pointer to the element type, carrying the element level's mutability: `node* mut[]` gives
  `node* mut*`) are read-only pseudo-fields. Slices are produced by `new(T[n])` (D10.2, as
  `own mut T[]`), by slicing (D6.9, always a view) and by the zero initializer `{}`. A slice
  literal `{1, 2, 3}` does not exist.
- **D3.6** Type suffixes come in three groups, left to right. `*` suffixes directly after the
  base type make pointers to the base (`node*`, `node**`). Array and slice suffixes then apply
  outside-in like C declarators: `node*[16]` is an array of 16 pointers, `i32[3][4]` is indexed
  `a[i][j]` with `i < 3`, `j < 4`, `i32[][4]` is a slice of `i32[4]`, and `new(i32[n][4])`
  returns `mut i32[][4]`. `*` suffixes after the array group make pointers to the whole array or
  slice type: `i32[4]*` points to an `i32[4]`, `u8[]*` points to a slice, which is the usual
  shape of an out-parameter (`fn bool read_file(string path, mut u8[] own* out)`, D17.2). No
  array suffix may follow a trailing `*` (wrap such a type in a struct). Suffixes after a
  function type apply
  to the function type: `fn i32(i32)[4]` is an array of four function pointers, `fn i32[4](i32)`
  returns an `i32[4]`.
- **D3.7** `string` is a distinct type: an immutable slice of `char` (`{char* ptr; u64 len}`).
  Literals have type `string` and are stored in read-only memory with a trailing NUL that is not
  counted in `len`. Sub-strings are not NUL-terminated. Indexing yields `char`; slicing yields
  `string`; `.len` and `.ptr` (`char*`) exist; `==`/`!=` compare `len` then bytes, so the zero
  string equals `""`. Bytes are UTF-8 by convention and never validated. There is no `+`; the
  standard library concatenates and returns `own string`, the owned form (D17.12).
- **D3.8** Structs: `struct name { T1 f1; T2 f2; }` with no trailing semicolon, nominal typing,
  C/System V layout (fields in order, natural alignment, size rounded to alignment). No methods,
  no inheritance, no per-field `mut` at the field's own level (D5.5). An empty struct is an
  error. A struct may contain itself only through a pointer or slice; value-containment cycles
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
  undefined behavior. `==`/`!=` compare identity.
- **D3.11** `void*` is an opaque pointer with no pointee level: no `*`, `->`, indexing or slicing.
  Conversion to and from any pointer, function pointer or `u64` requires `cast`.
- **D3.12** Type identity: primitives by name; structs and enums nominally; arrays by element type
  and length; slices, pointers and function types structurally, including mutability levels
  behind indirections (D5.2).
- **D3.13** Equality `==`/`!=` is defined on integers, floats, `bool`, `char`, enums, pointers
  (identity), function pointers (identity) and `string` (contents). It is a compile error on
  structs, fixed arrays and slices. This narrows type-system.md's earlier "all types support ==".
- **D3.14** Conversions. The only implicit conversion is dropping mutability (D5.4). Everything
  else is `cast(expr, Type)` (D6.4). Allowed casts: integer to integer (widening sign- or
  zero-extends by the source's signedness, narrowing truncates, same-width sign change
  reinterprets); integer to float (round to nearest); float to integer (truncate toward zero,
  saturate at the target's range, NaN becomes 0); float to float; `bool` to integer; `char` to
  and from integer; enum to and from integer; any pointer to any pointer or `void*` (mutability
  may be added, this is the cast-away-const escape); pointer to and from `u64`; function pointer
  to and from `void*`; among `string`, `char[]`, `u8[]`, `mut char[]` and `mut u8[]` (a
  binding-level `mut` in a cast target is an error); `T[]` to `mut T[]` and `mut T[]` to `T[]`;
  any cast that only drops mutability or ownership, at any level (a no-op, since the implicit
  conversions of D5.4 and D17.4 cover it); adding `own` to a pointer or slice (adoption, D17.3);
  identity. The result of a cast is `own` exactly when its target type says `own`: an `own`
  source cast to a non-`own` target lends (the result is a view), a non-`own` source cast to an
  `own` target adopts, and an `own` lvalue cast to an `own` target is a copy that must be written
  `cast(move(x), ...)` (D17.5). Forbidden:
  integer to `bool`, any other slice-to-slice cast (the element
  type of a slice never changes, because `len` counts elements), pointer to slice, struct or
  array casts. Casts never trap.
- **D3.15** `sizeof(Type)` takes a type only, yields an untyped integer constant (D4). `sizeof` of
  `void` is an error. `sizeof(T[])` and `sizeof(string)` are 16; function pointers are 8; `bool`
  and `char` are 1; enums are 4. There is no `alignof` in v1.
- **D3.16** No type inference (`var`/`auto`) and no type aliases in v1.

## D4 Untyped constants and constant expressions

Owner: `type-system.md` (Constants), `core-language.md` (Literals).

- **D4.1** Integer, float and char literals are untyped constants. An untyped constant takes its
  type from context: the declared type of the variable being initialized or assigned, the other
  operand of a binary operator, the parameter type, the return type, the `case` operand type, or
  an index, slice-bound or `new` count position (any integer type is fine there; a negative
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
  `0xFFFFFFFF`); constant `/` and `%` truncate toward zero exactly as at runtime (D6.13); shifts
  among untyped constants fold exactly too, so `i32 x = 1 << 31;` is an error (2147483648 does
  not fit `i32`) while `cast(1 << 31, i32)` is `-2147483648` and `one << 31` on an `i32` variable
  `one` is `-2147483648` (D6.2). Untyped integers are evaluated exactly in the range
  `[-2^63, 2^64 - 1]`; any
  intermediate outside it, and constant division by zero, are compile errors. Untyped floats are
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
  initializers, D7.10), field access, indexing, slicing, `.len` of slices or strings, reads of
  `mut` globals, `null` in a `cast`. Typed constant folding respects the declared type:
  `i32 A = 2147483647;` then
  `A + 1` is a compile error, not a runtime trap. Constant references are evaluated lazily with
  cycle detection; `i32 A = B; i32 B = A;` is an error.

## D5 Mutability

Owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).

- **D5.1** Everything is immutable unless marked `mut`. This applies to variables, parameters,
  the targets of pointers and the elements of slices.
- **D5.2** Storage levels. A declared type is a chain of storage levels numbered from the
  binding inward: level 0 is the binding's own storage; level 1 is the storage reached through
  the outermost indirection (the `*` or `[]` whose value the binding holds); level 2 the storage
  reached through the next indirection, and so on. With the suffix-reading rules of D3.6, the
  outermost indirection of `node**` is the last `*` (level 1 holds a `node*`, level 2 a `node`),
  of `node*[]` it is the `[]` (level 1 holds `node*` elements, level 2 the nodes), of
  `i32[][]` it is the first `[]`, and of `u8[]*` it is the trailing `*` (level 1 holds the slice
  header, level 2 the bytes). Fixed arrays and structs do not add a level: their elements
  and fields share the storage of the value that contains them. `string` has a single level (its
  characters are never mutable). `void*` has a single level (D3.11).
- **D5.3** Placement rule. A `mut` **before the base type** marks every level mutable, including
  the binding. A `mut` **immediately after a `*` or `[]` suffix** marks mutable exactly the
  storage that holds the pointer or slice header introduced by that suffix. Read the front `mut`
  as "fully mutable" and any other `mut` as "this level only". A `mut` that marks a level twice
  (`mut i32* mut p`) is an error ("redundant mut"), so every type has one spelling. Inside a
  fixed-array type, a `mut` after `*` marks the array's slots, which are level 0 of the array
  value (`node* mut[4] t` has assignable slots and immutable nodes; `t[..]` is `node* mut[]`).

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

  Rationale: C-style `const` placement, with the default inverted, made the most common local
  (`mut node* cur = head; cur = cur->next;`) need `mut` twice. What is lost is "mutable target,
  non-rebindable variable", which is a lint-level property.
- **D5.4** Dropping mutability is the one implicit conversion. Level 0 (the receiving binding) is
  unconstrained. For a level `k >= 1`, mutability may be dropped only if every level between 1
  and `k - 1` is immutable in the target type. So `mut node*[]` converts to `node*[]` and to
  `node* mut[] ` is refused (a mutable slot could then hold a pointer to what the source still
  sees as a mutable node). This closes the C `T** -> const T**` hole with a short recursive check.
  Adding mutability requires `cast` (D3.14). Dropping `own` (D17.4) is the other implicit
  conversion and follows the same monotone shape.
- **D5.5** Struct fields. A field's own storage is as mutable as the struct value that contains it
  (level 0 of the field is inherited from the access path). A leading `mut` in a field type
  therefore describes the levels behind the field's indirections; a `mut` that would apply only
  to the field's own slot is an error. A leading `mut` on a return type that has no indirection
  is an error for the same reason.
- **D5.6** Parameters. `mut` on a parameter follows D5.3; at level 0 it makes the callee's local
  copy assignable. Function-type identity ignores level 0 of parameters (D3.10).
- **D5.7** Mutability of an lvalue (D6.7): a variable has its level-0 bit; `*p` and `p->f` have
  level 1 of `p`'s type; `e.f` and `e[i]` on a fixed array have the mutability of `e`; `s[i]` on
  a slice has level 1 of `s`'s type; `str[i]` is immutable. Assignment, compound assignment,
  `++`, `--` and `&` producing a `mut T*` all require a mutable lvalue.
- **D5.8** `&e` has type `T*` where the level-1 bit is the mutability of `e` and deeper levels come
  from `e`'s type. `new(T)` returns `own mut T*`; `new(T[n])` returns `own mut T[]` (D17.3).
- **D5.9** Shallow model. Immutability of a variable never propagates through a pointer or slice
  it contains; the levels behind an indirection are fixed by the type. `node n` with a field
  `mut node* next`: `n.value = 1` is an error, `n.next->value = 1` is allowed.

## D6 Expressions and evaluation

Owner: `core-language.md` (Expressions).

- **D6.1** Precedence, highest first: primary (`()` `[]` `.` `->` calls, slicing, `cast`,
  `sizeof`, `new`, struct and array literals); unary (`! ~ - * &`); `* / % *%`; `+ - +% -%`;
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
  another literal; `= {}` zero-initializes any aggregate, slice, string or enum; `i32 x = {};`
  is an error. Trailing commas are allowed in brace lists and enum bodies, not in parameter or
  argument lists. `IDENT {` is never a block because every control-flow condition is
  parenthesized and every body is braced.
- **D6.6** `?:` requires a `bool` condition and two operands of one type; untyped constants adopt
  the other operand's type.
- **D6.7** Lvalues: variables and parameters, module-level constants and globals, `*p`, `p->f`,
  `e.f` where `e` is an lvalue, `e[i]` where `e` is an lvalue fixed array or any slice or string
  expression, and parenthesized lvalues (a constant is an immutable lvalue: addressable and
  sliceable, never assignable). `.len` and `.ptr` are never lvalues. Field access and indexing
  on an rvalue struct or array are allowed and yield rvalues (copied through a temporary). `&e`
  requires an lvalue.
  Returning the address of a local or a slice of a local array is not diagnosed (documented
  undefined behavior, as in C).
- **D6.8** Indexing `e[i]`: `e` is a fixed array, slice or string; `i` is any integer type or an
  untyped constant. Signed indices are sign-extended, unsigned zero-extended, and one unsigned
  comparison against the length catches negatives. Out of range is a runtime error (D10.6); a
  constant index out of range for a fixed array is a compile error. Pointers cannot be indexed.
- **D6.9** Slicing `e[lo..hi]`, `e[lo..]`, `e[..hi]`, `e[..]`: `e` is a fixed array (lvalue
  only), a slice, or a string; bounds are any integer type or untyped constants; the result is a
  slice (or string) whose mutability is that of `e`'s elements. The runtime check is
  `0 <= lo <= hi <= len`, relative to the operand, not the original allocation. `p[lo..hi]` on a
  `T*` or `mut T*` produces a `T[]` or `mut T[]` with no check; this is the explicit unsafe
  escape for foreign memory. `void*` cannot be sliced.
- **D6.10** `->` is `(*p).f` and is required for pointers; `.` on a pointer is an error with a
  hint. Through a pointer to a slice or string, `->` also reaches the `.len` and `.ptr`
  pseudo-fields (`out->len`). Indexing through a pointer to an array or slice is written
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
  slice or string expression evaluated once before the loop (a fixed array is copied as a
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
- **D8.2** Parameters are passed by value: primitives, pointers, slices and strings by copying the
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
  `fn i32 main(string[] args)`. `args[0]` is the program name; each element is NUL-terminated
  because it comes from `argv`. The return value is the exit status. A `main` returning `void`
  is an error. A `main` in any other module is an ordinary function.

## D9 Modules, namespaces and FFI

Owner: `module-system.md`.

- **D9.1** One module per file; the module path is the file path relative to a search root, with
  `::` separating segments and `.ft` dropped: `std::io` is `<std>/io.ft`, `util::strings` is
  `<root>/util/strings.ft`. Every segment must be an identifier that is not a keyword, so the
  earlier `std::string` is `std::str`.
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
- **D9.7** Symbol names in the generated assembly are the module path joined with dots plus the
  declaration name: `std.io.read_file`, `main.main`. Dots are legal in ELF symbols and cannot
  appear in identifiers, so the scheme is injective. Runtime symbols are prefixed `fort_rt_`;
  the compiler emits `fort_entry` in the entry module (D11.6). `extern` names are unmangled.
- **D9.8** `extern fn i64 write(i32 fd, void* buf, u64 n);` declares a C function with the
  System V x86-64 ABI. Extern signatures may use only integers, floats, `bool`, `char`, enums
  (passed as `i32`), pointers and function pointers: no slices, strings, structs or arrays, and
  no variadics. Before every extern call the compiler sets `al` to the number of vector
  registers the call uses (zero when no float is passed), as the ABI requires of callers of
  variadic functions, so a fixed-prototype declaration of a variadic C function is safe.
  Narrow integers and `bool` are normalized with zero- or sign-extension on both sides
  of the boundary. C `char*` maps to `char*` (or `u8*`); `size_t` to `u64`; `ssize_t` and
  `off_t` to `i64`; `mode_t` to `u32`; `int` to `i32`; `long` to `i64`; `double` to `f64`. The
  same C symbol may be declared `extern` in several modules provided the signatures are
  identical, `own` qualifiers included (D17.13).
- **D9.9** Internal calling convention (v1 simplification): integers, pointers, `bool`, `char`,
  enums, function pointers and floats are passed and returned in registers per System V; every
  aggregate (struct, fixed array, slice, `string`) is passed by a hidden pointer to a caller-made
  copy and returned through a hidden result pointer. Struct layout stays C-compatible, so
  pointer-based interop works. A fort function is usable as a C callback exactly when its
  signature is extern-legal.
- **D9.10** Whole-program compilation: the compiler walks the import closure from the entry file,
  type-checks every module, emits one assembly file, and runs the system C compiler to assemble
  and link it with the runtime. Interface files, separate compilation, a module cache and
  incremental rebuilds are deferred.

## D10 Memory and runtime checks

Owner: `memory-model.md`.

- **D10.1** Stack: locals, parameters, fixed arrays and struct values; freed at scope exit. Heap:
  only through `new`; freed only through `del`.
- **D10.2** `new(T)` returns `own mut T*` to zero-initialized storage; `new(T[n])` returns
  `own mut T[]` of `n` zero-initialized elements (D17.3), where `n` is any integer type; a
  negative `n`, a size that overflows, or allocation failure is a runtime error; `n == 0` is
  allowed and yields a
  non-null pointer (the runtime allocates at least one byte). `new(T{...})`,
  `new(T[])` and `new(void)` are errors. Rationale for zero-initialization: keeps "no undefined
  values" true at the cost of one `calloc`; the earlier "uninitialized" text is withdrawn.
- **D10.3** `del(x)` frees the allocation designated by its operand, which must have an `own`
  type; the full rules, including that `del` empties an lvalue operand and that `del` of a view,
  sub-slice, `.ptr`, stack address or literal is a compile error, are D17.9. Allocation has no
  header, so `new`/`del` and C `malloc`/`free` are interchangeable; memory from C is adopted with
  `cast` (D17.3).
- **D10.4** No pointer arithmetic: `p + 1`, `p++` and `p[i]` are errors. The only ways to get a
  pointer are `null`, `&`, `new`, `.ptr`, `cast`, a function name, and calls; the only way to
  get a slice from a raw pointer is the two-bound form `p[lo..hi]` (D6.9); `p[lo..]`, `p[..hi]`
  and `p[..]` are errors because a pointer has no length.
- **D10.5** `null` is the zero pointer and function-pointer value. `== null` and `!= null` are
  allowed only on pointers, `void*` and function pointers; slices and strings compare `.len`
  or `.ptr`. `null` has no type of its own: it is usable only where a pointer, `void*` or
  function-pointer type is expected, so `print(null)` and `null == null` are errors.
  Dereferencing `null` is undefined behavior (a segfault in practice).
- **D10.6** Bounds checks are performed on every index and slice operation, in every build mode;
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
  faults instead of skipping the guard page.

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
  | slice bounds                       | `slice bounds 2..7 out of range for length 3`      |
  | overflow of `+ - *`, `++`, `--`, unary `-` | `integer overflow`                         |
  | shift count                        | `shift count 64 out of range for i64`              |
  | division or remainder by zero      | `division by zero`                                 |
  | `MIN / -1`, `MIN % -1`             | `division overflow`                                |
  | negative `new` count               | `negative allocation count -1`                     |
  | `new` size overflow                | `allocation size overflow`                         |
  | allocation failure                 | `out of memory`                                    |
  | overwriting a live `own` value (D17.11) | `overwriting owned value`                     |

  The end of a `noreturn` function is guarded by a bare trap instruction (SIGILL, no message),
  since a conforming body never reaches it. `<file>` is the path the compiler opened (search
  root as given plus the relative module path); the column of a check is that of its operator
  token, or of the builtin's name for `new`, `assert` and `panic`; the `assert` text is the
  source text of the expression, verbatim.
- **D11.5** Output buffering: `print`/`println` write to a runtime buffer for stdout;
  `eprint`/`eprintln` are unbuffered; `fprint`/`fprintln` use one runtime buffer per descriptor,
  and `fprint(1, ...)` shares the stdout buffer with `print`. An `extern` write to a descriptor
  bypasses the buffers. Buffers flush when full, at exit, and before any runtime error. The
  runtime exports
  `fort_rt_flush(i32 fd)` and `fort_rt_flush_all()`; `io.close` and `io.flush` call the former,
  which is how a library call flushes a buffer the runtime owns.
- **D11.6** Process start: the C runtime owns `main(argc, argv)`, builds `string[] args`, calls
  the compiler-emitted `fort_entry(args)`, flushes, and exits with `status & 0xFF`.
  `fort_entry` takes the argument slice by pointer and is the one compiler-emitted exception to
  D9.8's ban on aggregates at the C boundary; `toolchain.md` fixes its C prototype and the names
  of every other runtime entry point.
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

- **D12.1** Keywords with type operands: `new(T)`, `new(T[n])`, `sizeof(T)`, `cast(e, T)`.
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
- **D13.4** The stdlib never passes slices, strings or structs across an `extern` boundary; it
  unpacks `.ptr` and `.len`. Functions that hand a path to C copy it into a NUL-terminated buffer.
- **D13.5** Ownership in the library (D17): every function that allocates returns an `own`
  value (`str.dup`, `str.concat` and `strbuf.take` return `own string`, exact length, no NUL;
  `str.to_cstr` returns `own mut char[]` with a trailing NUL) or delivers it through an `own`
  slot (`io.read_file_bytes`); containers hold their
  storage as `own` fields (`str_buf { own mut u8[] data; u64 len; }`, `ptr_vec`, `int_vec`,
  `str_map`) and expose a `free` function that `del`s them; out-parameters that receive
  ownership are pointers to `own` slots (`mut u8[] own* out`), which the caller initializes to
  `{}` or `null`; `sys.args()` and every `view`-style accessor return borrowed values.

## D14 Toolchain and test conventions

Owner: `toolchain.md`.

- **D14.1** `fort [options] entry.ft`. Options: `-o <file>` (default `a.out`), `-S` (stop after
  emitting `<entry>.s`), `-c` (stop after the object file), `-I <dir>` (repeatable),
  `--std-dir <dir>` (default `$FORT_STD_DIR`, else `std` beside the binary),
  `--release` (D11.1), `--no-bounds-check` (D10.6), `-l<lib>` (passed to the linker),
  `--cc <path>` (default `cc`), `--help`, `--version`. Exit status: 0 success, 1 compile error,
  2 usage, toolchain (`cc` failed) or internal error; usage and toolchain errors are printed as
  `fort: error: <message>`.
- **D14.2** Diagnostics: `<file>:<line>:<col>: error: <message>` on stderr, one per line,
  optionally followed by `note:` lines. Errors without a position in the file (a missing
  `main`, an invalid module name) use `1:1`. A syntax error stops the compilation of that file
  after one diagnostic (no recovery in v1). Modules are checked in dependency order; all semantic
  errors of the first module that has any are reported, then compilation stops. Positions of
  errors that concern a whole construct: "missing return" and a non-exhaustive enum `switch`
  are reported at the closing brace of the body or `switch`; an infinite-size struct at its
  `struct` keyword; a shadowing error at the inner declaration ("'n' shadows a parameter",
  "'n' shadows an enclosing local"). The compiler never emits warnings in v1.
- **D14.3** Generated assembly is GNU syntax, position-independent (RIP-relative data, `@PLT`
  calls for externs), and is assembled and linked by the system C compiler together with the
  runtime object.
- **D14.4** Language tests live under `test/lang/`: `run/<area>/NNN_name.ft` (compile, run,
  compare), `fail/<area>/NNN_name.ft` (must not compile), where `<area>` is one of `lexical
  constants operators casts mutability ownership declarations control switch defer functions
  structs enums arrays slices strings pointers globals builtins errors modes modules ffi stdlib`,
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
slice equality; definite-assignment analysis (idiom: initialize with `{}` or a sentinel);
alignment and packed attributes (idiom: an opaque `u8[N]` field and a C shim); separate
compilation and interface files; conditional compilation; labeled `break` (idiom: a flag or a
helper function); raw strings; a blank identifier; compile-time function evaluation; `alignof`;
`sizeof(expr)`; array suffixes after a trailing pointer suffix (`i32[4]*[2]`, idiom: a struct);
string `switch`; linear ownership, that is compile-time detection of leaks and of use after
`move` (idiom: `defer del`, and the zeroing that `move` and `del` leave behind, D17); `goto`
(never).

## D16 Hazards not to re-litigate

Findings from the design reviews that look like bugs but are deliberate.

- Level-0 mutability is excluded from the monotone drop rule (D5.4) on purpose: the receiving
  variable's own mutability cannot alias anything.
- `.len` of a slice is `u64` and nothing converts implicitly, so `for (mut u64 i = 0; ...)` is the
  idiom; `.len` of a fixed array is an untyped constant precisely to soften this.
- `print('a')` prints `a` and `print(cast('a', u8))` prints `97`, because `char` is distinct.
- `new(T)` returns `own mut T*` (D17.3); the earlier example `Point* p = new(Point);
  p->x = 10;` is now an error and must read `own mut point* p`.
- `for (i32 i = 0; ...)` is an error; the induction variable needs `mut`.
- Unsigned subtraction traps in checked mode (`len - 1` on an empty slice); test before
  subtracting or use `-%` when wrapping is intended.
- `defer` captures nothing; after `defer del(p);`, a later `del(p); p = move(q);` frees `q`.
- `break` inside a `switch` inside a loop exits the switch, not the loop.
- Symbol mangling uses dots, not double underscores, because `a__b` is not injective.
- `extern` signatures exclude aggregates so the compiler does not need System V aggregate
  classification in v1.
- Generated code must be position-independent; do not rely on `-no-pie`.
- Prefix `own` marks one level while prefix `mut` marks every level (D17.2); the asymmetry is
  chosen for the failure mode, not by oversight.
- `move` and `del` empty an immutable binding; that is ownership ending, not an assignment
  (D17.6).
- `mut node* n = new(node);` is an error: an owning temporary must land in an `own` place
  (D17.8).


## D17 Ownership

Owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
`del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
statements). Added after the v1 design review at the user's request; wherever an earlier
decision or document says ownership is "by convention", this section supersedes it.

- **D17.1** `own` is a qualifier on reference types: `own T*`, `own void*`, `own T[]`,
  `own string`. It states that the reference designates the start of a live allocation obtained
  from `new` (or adopted with `cast`, D17.3) and that `del` on it is meaningful. It is erased at
  run time (same bits, layout and ABI) and is part of type identity: `own node*` and `node*` are
  different types, as are `fn void(own node*)` and `fn void(node*)`. `own` on a non-reference
  type (`own i32`, `own point`, `own i32[4]`) or on a function-pointer type is an error; an
  array or struct that *contains* an `own` reference is an owning aggregate (D17.7).
- **D17.2** Placement. A `own` before the base type (and before any `mut`) marks the outermost
  reference of the type, the one the binding holds. A `own` immediately after a `*` or `[]`
  suffix (before any `mut` in that position) marks the reference that suffix introduces. Marking
  one level twice is a "redundant own" error, and a postfix `own` on the outermost suffix
  (`node* own p`) is an error too, so every type has one spelling. Unlike prefix `mut`, prefix
  `own` marks one level only: the safe failure mode for `own mut node*[] kids` is that
  `del(kids[i])` does not compile when the nodes belong to someone else (an arena, say).

  | Declaration                 | Meaning                                                     |
  |-----------------------------|-------------------------------------------------------------|
  | `own mut u8[] buf`          | owned slice of writable bytes                               |
  | `own u8[] data`             | owned slice, read-only through this binding                 |
  | `own mut node* n`           | owned node                                                  |
  | `own mut node*[] items`     | owned slice of borrowed pointers (a `ptr_vec`, arena nodes) |
  | `own mut node* own[] kids`  | owned slice of owned nodes                                  |
  | `node* own[] view`          | borrowed slice of owned nodes                               |
  | `mut u8[] own* out`         | borrowed pointer to an owned slot (an out-parameter)        |
  | `own string name`           | owned immutable characters (`str.dup`, `strbuf.take`)       |

- **D17.3** Producers. `new(T)` yields `own mut T*`; `new(T[n])` yields `own mut T[]` and
  `new(T[n][K])` yields `own mut T[][K]`; standard-library functions that allocate return `own`
  (D13.5). Inside `new(...)`, `own` may appear only in postfix positions of the element type:
  `new(node* own[n])` yields `own mut node* own[]` whose slots are null; a prefix `own` there
  does not parse. `cast` may add `own` to a pointer or slice, adopting memory that came from C
  (`cast(p, own mut u8*)` for a `void*` from an extern that does not say `own`, the same unsafe
  escape as adding `mut`), and may drop it; the target type of a cast decides (D3.14). Slicing
  (`buf[..]`, `buf[lo..hi]`) and `.ptr` always yield views, as do `&`, literals and the runtime's
  `args`.
- **D17.4** Lending. `own X` converts implicitly to `X` wherever a value meets an expected type,
  like dropping `mut` (D5.4); the two drops combine (`own mut u8[]` to `u8[]`). Dropping `own`
  at a level `k` is allowed only if every level between 1 and `k - 1` is immutable in the target
  (the D5.4 shape: otherwise `mut node*[] w = kids; w[0] = &local;` would let `del(kids[0])`
  free a stack address) and only if no outer level keeps `own` (`own mut node* own[]` to
  `own mut node*[]` is an error, since the inner objects would then be owned by nobody); lend the
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
  because the move changes storage that others can see: `move(v[0])` on a `node* own[] v` is an
  error, since nothing may be taken out of what was only lent. Fields and elements of a local
  value count as the local. Moving a zero value yields a zero value. `move` and `del` of a
  module-level constant (D7.10) are errors: it lives in read-only memory.
- **D17.7** Owning aggregates. A struct or fixed array that contains an `own` reference by value
  (directly or through nested aggregates) is owning. Copying an owning lvalue into an owning
  place (initialization, assignment, a by-value parameter, a literal element) requires `move`;
  returning a local owning value is an implicit move. Functions therefore take `vec*` or
  `mut vec*`. `del` of an aggregate is an error: `del` is shallow, and a struct frees its own
  fields.
- **D17.8** Temporaries must land. An `own` rvalue may only be bound to an `own` place, passed
  to an `own` parameter, or `del`ed. Anything else is a compile error ("owning temporary would
  leak"), because nothing could ever `del` it: converting or casting it to a non-`own` type
  (`mut node* n = new(node);`, `use(str.dup(x))`, `cast(new(node), node*)`), slicing, indexing or
  taking `.ptr` of it (`new(u8[8])[..4]`, `new(i32[2])[0]`), accessing a field of an owning
  aggregate rvalue, and discarding it as an expression statement (`move(x);`, `str.dup(s);`).
- **D17.9** `del(x)` requires an `own` operand of any mutability: an `own` pointer, `own void*`,
  `own` slice or `own string`, as an lvalue or an rvalue. On an lvalue, `del` empties the operand
  under the rules of D17.6, with the same mutability requirement through indirections; on an
  rvalue it only frees. `del(null)` (the literal adopts `own void*`) and `del` of a zero slice or
  string are no-ops, so
  `del(buf); del(buf);` frees once, and a use after `del` or `move` dereferences `null`. `del`
  of a view, a sub-slice, a `.ptr`, a stack address or a literal is a compile error, because
  none of them has an `own` type. This supersedes the earlier "del does not null its argument".
- **D17.10** Loops. The collection expression of a range `for` lends: an owning collection
  (an `own` slice, or an owning fixed array) is iterated in place, never moved or copied. The
  loop variable's type is the element type with its outermost `own` removed (`for (mut Node* c
  : kids)` over `own mut Node* own[] kids`); declaring it `own` is an error, and elements that
  are owning aggregates cannot be copied into a loop variable at all, so such a collection is
  iterated by index. Moving an element out is written explicitly, `move(kids[i])`.
- **D17.11** Overwrite check. In checked builds (D11.1), storing into an `own` reference-typed
  lvalue whose current value is not the zero value is a runtime error, `overwriting owned
  value` (D11.4), because the previous allocation would leak. `del` and `move` leave zero
  behind, so `del(v.data); v.data = new(...)`, `a = move(b)` after `move(a)`, and initialization
  from `{}` or `null` all pass. The check runs after the right-hand side is evaluated,
  immediately before the store, and is reported at the `=` token. Release builds store without
  checking. Assignments of owning aggregates are not checked field by field.
- **D17.12** Strings. `own string` is an owned, immutable character sequence: `str.dup`,
  `str.concat` and `strbuf.take` return it; literals, sub-strings and `sys.args()` are `string`.
  `del(own string)` is legal and `del(string)` is not. A string built in an `own mut u8[]`
  becomes an `own string` with `cast(move(buf), own string)` (the target says `own`, so the
  source must be moved, D3.14); `cast(buf, string)` lends a view instead.
- **D17.13** `own` may appear in `extern` signatures. It is erased, and it documents the C
  side's convention: `extern fn own void* malloc(u64 n);` (`void*` has no target level, so no
  `mut`, D5.5), `extern fn void free(own void* p);`. Signature identity includes `own` (D9.8).
- **D17.14** Not tracked, exactly as in C: a view, or a copy made before a `move` or `del`,
  used after the allocation was freed; an `own` value that is never freed; two `own` copies made
  through `cast`. The linear check that would make leaks and use after `move` compile errors is
  deferred (D15); this design is its intended base and adds no syntax it would not need.

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
