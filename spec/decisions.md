# fort design decisions

This is the decision log for the fort language, v1. It is the source of truth: when another
document in `spec/` or `notes/` disagrees with this file, this file wins and the other document
has a bug.
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
- D20 Editor support
- D21 Compile-time selection
- Ready-to-implement checklist

## D1 Naming and files

### D1.1 The name of the language and its files
- owner: this file; section D1 names no specification document.
- rule: The language is `fort`. Source files use the extension `.ft`. The compiler binary is `fort`.
  The documents' earlier `.lang` and `langc` are gone.

### D1.2 Where the specification lives
- owner: this file; section D1 names no specification document.
- rule: The design lives in `spec/`. `decisions.md` (this file) and `grammar.md` are normative;
  `core-language.md`, `type-system.md`, `memory-model.md`, `module-system.md`, `stdlib.md` and
  `toolchain.md` are the specification proper, each owning one topic. `project-overview.md` is the
  entry point. `notes/` holds the engineering knowledge, in `environment.md` (the VM, the build),
  `testing.md` (the tests), `compiler.md` (the passes) and `style.md` (the conventions).
  `notes/README.md` indexes both directories. Those five files describe the implementation, not the
  language. Every specification file above wins over them, and the routing rule of `AGENTS.md` says
  which one takes a new fact.
- history: Amended 2026-09-12 (T-098): the engineering knowledge joined this rule. Until then the
  rule named the specification files alone. Amended 2026-09-12 (T-100): T-099 moved the nine
  specification files from `notes/` to `spec/`, and this rule still said "The design lives in
  `notes/`". The rule now names `spec/` for the specification and `notes/` for the engineering
  knowledge. The T-098 text called `notes/` the home of both.

### D1.3 Markdown line width
- owner: this file; section D1 names no specification document.
- rule: Markdown wraps at 100 columns (`notes/style.md` 4).
- history: Amended 2026-09-12 (T-105): the rule cited `AGENTS.md`. T-099 moved the markdown width
  out of that file, and the rule now names `notes/style.md` 4, which holds it.

### D1.4 Identifier conventions
- owner: this file; section D1 names no specification document.
- rule: Identifier conventions (not enforced by the compiler): modules, functions, variables,
  fields, struct and enum type names, and enum members are lower_case with underscores (`struct
  str_buf`, `enum color { red, green }`, `color.red`, `fn parse_i64(...) i32`); module-level
  constants are UPPER_CASE (`i32 MAX = 64;`); a variable never takes its type's name (`point p`,
  never `point point`), because a local may shadow a module-level name (D7.9); a struct field may
  (`node* node;`), since fields live in no namespace a type could occupy.
- rationale: user decision, matching C's `struct point` and the function and variable style.

## D2 Lexical structure

### D2.1 Source encoding and whitespace
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Source is UTF-8. Whitespace is space, tab, `\n`, `\r`. An optional leading BOM is skipped.
  Non-ASCII bytes are allowed only inside string literals and comments.

### D2.2 Comments
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Comments: `//` to the end of the line, and nothing else. There are no block comments, so
  there is no nesting question, no unterminated-comment failure mode, and no `a /*p` ambiguity;
  commenting out a region is a line-oriented operation every editor does. The adjacent two-byte
  sequence `/*` is a lexical error ("block comments are not supported, use `//`") rather than a
  division followed by a dereference, so the C habit fails loudly instead of parsing as something
  else; `a / *p`, with the operators separated, is that division and is legal.
- rationale: user decision, and the same rule holds for the project's C sources (`notes/style.md`
  2).
- history: Amended 2026-09-12 (T-105): the rationale cited `AGENTS.md` for the C sources. T-099
  moved that rule out of that file, and the rationale now names `notes/style.md` 2, which holds it.

### D2.3 Identifiers
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Identifiers: `[A-Za-z_][A-Za-z0-9_]*`, case-sensitive, no length limit. `_` is an ordinary
  identifier. Keywords cannot be identifiers.

### D2.4 Keywords and reserved words
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Keywords: `as bool break case cast char continue default defer do else enum extern f32 f64
  false fn for i8 i16 i32 i64 if import mut new noreturn null own return sizeof string struct switch
  true u8 u16 u32 u64 void while`. Reserved for future use, not usable as identifiers: `async await
  const match pub priv trait type union yield`.

### D2.5 Integer literals
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Integer literals: decimal `123`, hex `0x7F`, octal `0o17`, binary `0b1010`. `_` may appear
  between two digits (`1_000_000`, `0xFF_FF`), nowhere else. A decimal literal other than `0` may
  not start with `0` (no C-style octal). No suffixes.

### D2.6 Float literals
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Float literals: digits `.` digits with optional exponent (`1.0`, `2.5e-3`), or digits with
  an exponent (`1e10`). `1.` and `.5` are not literals, and the integer part follows D2.5's
  leading-zero rule (`0.5` and `0e1` are fine, `09.5` is an error). No suffixes; `f32` values come
  from context (D4).

### D2.7 Char literals
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Char literals: `'x'` where `x` is one printable ASCII character other than `'` or `\`, or
  one escape. A non-ASCII byte in a char literal is an error ("use a string").

### D2.8 Escapes
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Escapes, in char and string literals: `\n \t \r \0 \\ \' \" \xHH` (exactly two hex digits).
  Anything else after `\` is an error.

### D2.9 String literals
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: String literals: `"..."` with escapes; a raw newline inside is an error; no adjacent-literal
  concatenation; no raw strings.

### D2.10 Operators and punctuation
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Operators and punctuation: `+ - * / % +% -% *% = += -= *= /= %= +%= -%= *%= &= |= ^= <<= >>=
  == != < <= > >= && || ! & | ^ ~ << >> ++ -- ? : . -> .. ... ( ) [ ] { } , ; @`.
  Longest match wins (`...` before `..` before `.`, and `+%=` before `+%` before `+`).
  `%` is never a prefix operator, so `+%` is unambiguous. `>>` and `<<` are single tokens.
- history: Amended 2026-09-11: the list held `::`, the module path separator of D9.1; that separator
  became `.`, so `::` is no longer a token and a source holding one lexes two colons.
  Amended 2026-09-15 (T-140): `...` marks the variable tail of a C extern (D9.8).

### D2.11 The nesting limit
- owner: `core-language.md` (Lexical structure), `grammar.md` (Lexical grammar).
- rule: Nesting of blocks, parentheses, brackets, braces and type suffixes deeper than 256 is a
  compile error, so a recursive-descent compiler written in fort never needs an unbounded stack.

## D3 Types

### D3.1 Primitive types, sizes and alignment
- owner: `type-system.md`.
- rule: Primitive types: `i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool char void`. Sizes: 1, 2, 4, 8
  bytes for the integers, 4 and 8 for the floats, 1 for `bool` and `char`; alignment equals size.
  Pointers, function pointers, spans and strings align to 8; arrays to their element; structs to
  their most-aligned field. `void` is only a return type or the base of `void*`. There is no `byte`
  type.

### D3.2 The char type
- owner: `type-system.md`.
- rule: `char` is a distinct one-byte character type. It supports `== != < <= > >=` (ordered by
  unsigned byte value), `switch`, `cast` to and from integer types, and nothing else (no arithmetic,
  no bitwise operators). Char literals default to `char` (D4.3).
- rationale: the user chose a distinct text type over `u8`.

### D3.3 The bool type
- owner: `type-system.md`.
- rule: `bool` has exactly the values `true` and `false`. Conditions must be `bool`; there is no
  truthiness for integers or pointers (`p != null`, `x != 0`). `bool` supports `== != ! && ||` and
  `cast` to integer types (0 or 1). `cast` from an integer to `bool` is an error.

### D3.4 Fixed arrays
- owner: `type-system.md`.
- rule: Fixed arrays `T[N]`: `N` is a constant expression (D4.6) greater than 0. Value type:
  assignment, parameter passing and return copy all elements. Different `N` are different types.
  `a.len` is an untyped integer constant. Fixed arrays have no `.ptr`. Sizes are computed exactly: a
  type whose size would exceed `2^63 - 1` bytes (the range of an `i64` index) is a compile error,
  "type is too large", at the declaration that introduces it, reported like the infinite-size error
  of D3.8 rather than as an out-of-memory failure of the compiler.
- history: Amended 2026-09-10 (T-011 review): the limit was unstated.

### D3.5 Spans
- owner: `type-system.md`.
- rule: Spans `T@` (the reference suffix `@`, D3.6; until 2026-09-10 spelled `T[]`) replace the
  earlier "dynamic array". A span is a fat pointer `{T* ptr; u64 len}`; whether it owns its elements
  is part of its type (`own`, D17); all spans of the same element type, element mutability (D5) and
  ownership are one type; the zero value is `{null, 0}`. `.len` (type `u64`) and `.ptr` (a pointer
  to the element type, carrying the element level's mutability: `node* mut@` gives `node* mut*`) are
  read-only pseudo-fields. Spans are produced by `new(T, n)` (D10.2, as `T mut@ own`), by taking a
  span (D6.9, always a view) and by the zero initializer `{}`. A span literal `{1, 2, 3}` does not
  exist.
- history: Amended 2026-09-10: the type was called a slice until then, and the operation slicing.

### D3.6 How type suffixes read
- owner: `type-system.md`.
- rule: Type suffixes read as follows. A reference suffix, `*` (pointer) or `@` (span, D3.5),
  applies to everything to its left, so a sequence of them reads inside-out: `node**` is a pointer
  to a pointer, `node*@` a span of pointers, `u8@*` a pointer to a span, `u8@@` a span of spans.
  Fixed-array suffixes form one group that reads outside-in like C declarators: `i32[3][4]` is three
  arrays of four, indexed `a[i][j]` with `i < 3`, `j < 4`. Reference suffixes may precede the array
  group, making arrays of references (`node*[16]` is sixteen pointers, `node@[4]` four spans), or
  follow it, making references to the whole array (`i32[4]*` points to an `i32[4]`, `i32[4]@` is a
  span of `i32[4]`, and `new(i32[4], n)` returns `i32[4] mut@ own`); no array suffix may follow a
  trailing reference suffix (`i32[4]*[2]` does not parse; wrap it in a struct). `u8@*` is the usual
  shape of an out-parameter (`fn read_file(string path, u8 mut@ own mut* out) bool`, D17.2).
  A suffix after a function type belongs to that function type's return type, which is the last
  element of it: `fn (i32) i32[4]` returns an `i32[4]` (D3.10).
- history: Amended 2026-09-10: spans were called slices and spelled `T[]`, read with the array
  group, so `i32[][4]` was the span of `i32[4]`. Amended 2026-09-14 (T-136): a suffix after a
  function type applied to the function type, so `fn i32(i32)[4]` was an array of four function
  pointers and `fn i32[4](i32)` a function returning an `i32[4]`; with the result last the two
  spellings are one and only the second reading is available.

### D3.7 The string type
- owner: `type-system.md`.
- rule: `string` is a distinct type: an immutable span of `char` (`{char* ptr; u64 len}`). Literals
  have type `string` and are stored in read-only memory with a trailing NUL that is not counted in
  `len`. Sub-strings are not NUL-terminated. Indexing yields `char`; a span of a `string` is a
  `string`; `.len` and `.ptr` (`char*`) exist; `==`/`!=` compare `len` then bytes, so the zero
  string equals `""`. Bytes are UTF-8 by convention and never validated. There is no `+`; the
  standard library concatenates and returns `string own`, the owned form (D17.12).
- history: Amended 2026-09-10: spans were called slices (D3.5).

### D3.8 Structs and value containment
- owner: `type-system.md`.
- rule: Structs: `struct name { T1 f1; T2 f2; }` with no trailing semicolon, nominal typing,
  selected-target C layout (fields in order, natural alignment, size rounded to alignment).
  No methods, no inheritance, no per-field `mut` at the field's own level (D5.5).
  An empty struct is an error. A
  struct may contain itself only through a pointer or span; value-containment cycles are "infinite
  size" errors. A struct B is contained by value in a struct A when a field of A is written `B`, or
  a fixed array of any rank over it (D3.4), and in no other case: a `*` or `@` anywhere in the
  written type ends the containment (D3.11, D5.8, the reference is a word or two whatever it refers
  to), and so does a `fn` signature, since a function pointer is an ordinary pointer whose identity
  is structural over its signature (D3.10), nothing stores the signature, and an aggregate crosses a
  call through a hidden pointer (D9.9), so even a call through such a pointer needs the layout only
  at the call site. An infinite size is therefore a cycle of value-containment edges alone, and a
  compiler that makes a struct's layout wait for one it does not contain by value reports an
  infinite size for a finite struct.
- history: Amended 2026-09-11 (T-082): "value containment" is stated positively, because it decides
  which declarations a struct's layout may wait for and two declaration orders of one pair must be
  one program (D7.10).
  Amended 2026-09-15 (T-140): layout now follows the selected C target.

### D3.9 Enums
- owner: `type-system.md`.
- rule: Enums: `enum color { red, green = 5, blue }`. Underlying type `i32`, size 4, and `i32` is
  the signed type of D3.1: a member may be negative, a widening `cast` sign-extends, and the enum
  table of `toolchain.md` 6 prints the member signed. Members are scoped: `color.red` everywhere,
  including `case` labels; `m.color.red` across modules. Values start at 0 and increment; an
  explicit value is a constant expression that may not refer to the enum itself; duplicate values
  are errors. Enums support `== !=`, `switch` (D7.7) and `cast` to and from any integer type
  (int-to-enum is unchecked). No ordering operators. A zeroed enum holds 0 even if 0 is not a
  member.

### D3.10 Function types and function pointers
- owner: `type-system.md`.
- rule: Function types are written `fn (P1, P2) R` with parameter types only, the result last as in
  a declaration (D8.1). A function type ends at its return type, so every marker and every suffix
  written after it belongs to that return type and the function type carries none of its own:
  `fn (i32) i32[4]` is a function returning `i32[4]` and `fn (i32) i32*` one returning `i32*`. To
  mark or to suffix a function type -- an array of function pointers, a function-pointer binding
  that is assigned to -- wrap it in a struct, the escape `grammar.md` 4 prescribes for
  `i32[4]*[2]`. Identity is structural
  over parameter types (including pointee mutability), return type and `noreturn`; binding-level
  `mut` on parameters is ignored. A function name used as a value, including a qualified `m.f`, has
  its function type; `&f` and `*f` are errors. The name must be a fort function.
  An `extern fn` in value position is an error: the direct call takes its target ABI form from
  the extern declaration (D9.8), and a function-pointer type has no C variable tail (D8.3).
  Wrap a C call in a fort function when C needs a function pointer.
  `null` is a valid function-pointer value; calling it is undefined
  behavior. `==`/`!=` compare identity. Function pointers are in the C bootstrap's subset
  (`toolchain.md` 7.3): a function pointer is an ordinary `ptr` value and a call through one an
  ordinary `call` in LLVM IR (D19.2), so the bootstrap implements them.
- history: Amended 2026-09-10: an `extern` name was a value like any other, which emitted a
  non-variadic indirect call site against a symbol declared variadic -- no `al` set, no diagnostic,
  and no test that could see it (T-017's review). Amended 2026-09-14 (T-136): a function type was
  written `fn R(P1, P2)` and took markers and suffixes of its own, so `fn i32(i32)[4]` was an array
  of four function pointers and `fn i32(i32) mut f` a function-pointer binding that could be
  assigned to. With the result last there is no token between the return type and the suffix, so
  the suffix can only belong to the return type and both forms lose their spelling; the struct
  wrapper replaces them.
  Amended 2026-09-15 (T-140): direct C extern calls now use the selected target ABI form.
  Function-pointer types stay fixed, and C extern names stay outside value position.

### D3.11 The void pointer
- owner: `type-system.md`.
- rule: `void*` is an opaque pointer with no pointee type: no `*`, `->`, indexing or span
  expression. It reaches one storage level all the same, which no expression names and which
  `void mut*` marks writable; `void* mut p` marks the binding instead, so the two are distinct
  and both legal (D5.2, D5.3). Conversion to and from any pointer, function pointer or `u64`
  requires `cast`.
- rationale: mutability is a property of storage, not of the type of that storage. A `void*`
  reaches storage whose type is unknown, and whether a callee may write that storage is what a C
  signature states with `const`: `fread(void*)` against `fwrite(const void*)`. Without
  `void mut*` a program marks the pointer's own storage and not the storage behind it, and
  `std.libc` cannot declare `malloc` as an allocation the caller may write.
- history: Amended 2026-09-14 (T-086): until then `void*` had no pointee level at all and
  `void mut*` was the error "'void' is only a return type or the base of 'void*'". The `mut`
  costs no run-time representation, since every pointer is one machine word.

### D3.12 Type identity
- owner: `type-system.md`.
- rule: Type identity: primitives by name; structs and enums nominally; arrays by element type and
  length; spans, pointers and function types structurally, including mutability levels behind
  indirections (D5.2).

### D3.13 Where equality is defined
- owner: `type-system.md`.
- rule: Equality `==`/`!=` is defined on integers, floats, `bool`, `char`, enums, pointers
  (identity), function pointers (identity) and `string` (contents). It is a compile error on
  structs, fixed arrays and spans. This narrows type-system.md's earlier "all types support ==".

### D3.14 Conversions and casts
- owner: `type-system.md`.
- rule: Conversions. The only implicit conversion is dropping mutability (D5.4). Everything else is
  `cast(expr, Type)` (D6.4). Allowed casts: integer to integer (widening sign- or zero-extends by
  the source's signedness, narrowing truncates, same-width sign change reinterprets); integer to
  float (round to nearest); float to integer (truncate toward zero, saturate at the target's range,
  NaN becomes 0); float to float; `bool` to integer; `char` to and from integer; enum to and from
  integer; any pointer to any pointer or `void*`; pointer to and from `u64`; function pointer to
  and from `void*`; among `string`, `char@`, `u8@`, `char mut@` and `u8 mut@` (a `mut` in the
  outermost position of a cast target is an error: a cast result has no binding); a span to a span
  of the same element type whose marks differ only in mutability, dropped at any level (the drop
  reaches every level, which the implicit conversion does not; adding is refused by the rule
  below); any cast that only drops mutability or ownership, at any level (**not** a no-op: D5.4
  and D17.4 cover the drop at a level
  `k` only when every level between 1 and `k - 1` is immutable in the target, and the cast alone
  reaches the rest. `void mut* mut@` converts to `void*@` and to `void mut*@`, and a cast alone
  takes it to `void* mut@`); identity. The result of a cast is `own` exactly when its target type
  says `own`: an `own` source cast to a non-`own` target lends (the result is a view), and an
  `own` lvalue cast to an `own` target is a copy that must be written `cast(move(x), ...)`
  (D17.5). The rule below refuses a non-`own` source cast to an `own` target. Forbidden:
  integer to `bool`, any other span-to-span cast (the element type of a span never changes, because
  `len` counts elements), pointer to span, struct or array casts. Casts never trap: float to integer
  is emitted as `llvm.fptosi.sat` or `llvm.fptoui.sat`, whose saturating result is this rule (plain
  `fptosi` would be poison out of range, D19.2).
  **A cast never adds `mut`, from any source.** The target marks a level `mut` only where the
  source marks the level at the same depth `mut` too. Where the source has no such level -- an
  integer, the `void` behind a `void*`, a `string`, or a pointee type the cast reinterprets -- the
  target marks no level below that point. So `cast(cast(p, void*), u8 mut*)` is an error at the
  second cast, and so are `cast(bits, u8 mut*)` from a `u64` and `cast(s, u8 mut@)` from a
  `string`. There is no cast-away-const escape. With `void mut*` in an `extern` (D17.13) this
  leaves `mut` on a reference with no cast escape at all: the one way to a writable reference over
  foreign storage is a declaration that says `void mut*`, which the fort programmer writes, and
  `std.libc`'s `malloc` does. An `extern` that lies about a C function stays unsafe, and no rule
  inside fort reaches it. The library follows the rule and does not bend it: `std.sort_ptr` takes
  `void* mut@`, which a caller holding `void mut* mut@` reaches with a dropping cast, and
  `std.ptr_vec` keeps `void mut*` slots, because `ptr_push` takes a pointer and `ptr_pop` gives one
  back and a round trip through `void*` slots would lose the `mut` for good (`stdlib.md` 2.7,
  2.10).
  **A cast never adds `own`, from any source.** The target marks a level `own` only where the
  source marks the level at the same depth `own` too. Where the source has no reference at that
  depth -- an integer, the `void` behind a `void*`, or a pointee type the cast reinterprets -- the
  target marks no `own` at that depth or below it. So `cast(p, i32* own)` from an `i32*` is an
  error, and so are `cast(bits, void* own)` from a `u64`, `cast(s, string own)` from a `string`
  and `cast(q, node* own*)` from a `node**`. A fixed-array suffix on one side only ends the
  correspondence of levels, as it does for `mut`: `cast(&arr, node* own*)` from a
  `node* own[2] mut arr` is an error, although a fixed array adds no level (D5.2). This is the
  conservative reading, and a later amendment may relax the `mut` and the `own` walk together. A
  cast carries `own` or drops it, and it never adds it. Ownership enters a program only through
  `new` and through an `extern` signature that says `own` (D17.3, D17.13).
- history: Amended 2026-09-14 (T-085): a cast could add `mut` -- to a pointer at any level, to a
  span at any level, from a `void*`, from a `u64` and from a `string` -- which left `mut` on a
  reference a suggestion. The user withdrew the escape on 2026-09-11, `void*` included. The cost,
  measured on the branch: `test/lang/run/casts/005_span_mutability.ft` was rewritten, because its
  subject was the escape; `run/casts/003_pointer_forms.ft` and
  `run/pointers/012_void_mut_pointer.ft` route through `void mut*` instead; and 10 sites of
  `src/fort/check.ft` cast a `const` away as the C bootstrap does, which five field declarations
  now carry instead (`ast.node.sym`, `ast.sym.node`, `ast.sym.owner`, `scope.binding.node` and
  `types.node.decl`).
  Amended 2026-09-14 (T-135): the dropping-cast clause called such a cast "a no-op, since
  the implicit conversions of D5.4 and D17.4 cover it". They do not cover a drop at a level behind a
  mutable level, which is the drop D5.4's own example refuses; `test/fort/types_convert_test.ft:173`
  asserts that `void mut* mut@` does not convert to `void* mut@`, and until T-135 three sites of
  `src/fort` (`gen.ft` twice and `types.ft` once) wrote that cast to perform exactly that drop on
  the result of a `new`. Amended 2026-09-10: spans were called slices (D3.5).
  Amended 2026-09-29 (T-257): a cast could add `own` to a pointer or span. The allowed list held
  "adding `own` to a pointer or span (adoption, D17.3)", and "a non-`own` source cast to an `own`
  target adopts". Adoption existed for memory from C. An `extern` that says `own` (D17.13) already
  covers that memory, and every allocating `extern` in `std` says `own`. The cast was then the one
  way a view of fort memory got a second owner, which D17.14 listed as untracked and no analysis
  can close. The user withdrew adoption on 2026-09-29, as T-085 withdrew the added `mut`.

### D3.15 sizeof
- owner: `type-system.md`.
- rule: `sizeof(Type)` takes a type only, yields an untyped integer constant (D4). `sizeof` of
  `void` is an error. `sizeof(T@)` and `sizeof(string)` are 16; function pointers are 8; `bool` and
  `char` are 1; enums are 4. There is no `alignof` in v1.

### D3.16 No type inference and no type aliases
- owner: `type-system.md`.
- rule: No type inference (`var`/`auto`) and no type aliases in v1.

## D4 Untyped constants and constant expressions

### D4.1 Untyped constants take their type from context
- owner: `type-system.md` (Constants), `core-language.md` (Literals).
- rule: Integer, float and char literals are untyped constants. An untyped constant takes its type
  from context: the declared type of the variable being initialized or assigned, the other operand
  of a binary operator, the parameter type, the return type, the `case` operand type, or an index,
  span-bound or `new` count position (any integer type is fine there; a negative constant in such a
  position is a compile error). This is the Go model. A `cast` is not a context: in `cast(c, T)` an
  untyped `c` first takes its default type (D4.5) and is then converted to `T` with runtime
  semantics (D4.4). The count operand of a shift is not a context either: in `u64 m = 1 << n;` the
  untyped `1` takes `u64` from the declaration, whatever the type of `n`; with no enclosing context
  it takes its default type.

### D4.2 Contextual conversion
- owner: `type-system.md` (Constants), `core-language.md` (Literals).
- rule: Contextual conversion is checked at compile time. An untyped integer may become any integer
  type it fits in, or any float type. An untyped float may become only a float type, never an
  integer type even when integral (`i32 x = 2.0;` is an error). `u32 x = -1;` and `u8 b = 256;` are
  errors.

### D4.3 Char literals in an integer context
- owner: `type-system.md` (Constants), `core-language.md` (Literals).
- rule: A char literal is an untyped constant whose default type is `char`; in an integer context it
  takes that integer type (`cast(c, i32) - '0'` and `u8 b = 'a';` are fine). An integer literal
  never becomes `char` implicitly (`char c = 65;` is an error; use `cast`).

### D4.4 Constant folding
- owner: `type-system.md` (Constants), `core-language.md` (Literals).
- rule: Arithmetic among untyped constants folds at compile time: integer with integer stays an
  untyped integer (`1 / 2` is `0`, so `f64 d = 1 / 2;` is `0.0`); integer with float becomes an
  untyped float; `~c` on an untyped integer is `-c - 1`, so `u32 m = ~0;` is an error (write
  `0xFFFFFFFF`); constant `/` and `%` truncate toward zero exactly as at runtime (D6.13); `& | ^` on
  untyped integers operate on the infinite two's-complement extension of the values (Go's rule; `-1
  & 0xFFFFFFFFFFFFFFFF` is `18446744073709551615`), and only `^` can leave the range; shifts among
  untyped constants fold exactly too, with a count in `0..63`, `<<` an exact multiplication and `>>`
  a floor division (`-3 >> 1` is `-2`), so `i32 x = 1 << 31;` is an error (2147483648 does not fit
  `i32`) while `cast(1 << 31, i32)` is `-2147483648` and `one << 31` on an `i32` variable `one` is
  `-2147483648` (D6.2). Untyped integers are evaluated exactly in the range `[-2^63, 2^64 - 1]`; any
  intermediate outside it, a shift count outside `0..63`, and constant division by zero, are compile
  errors. Untyped floats are evaluated as `f64`. `cast` on a constant has runtime semantics
  (`cast(0x80000000, i32)` is `-2147483648`, `cast(-1, u32)` is `4294967295`). A float constant that
  is not finite in the target type is an error.
- history: Note 2026-09-12 (T-041, the first implementation of floats): "evaluated as `f64`" is two
  roundings for an `f32` context, and the text is read as written. A literal is rounded to binary64,
  and the constant then takes `f32` from its context (D4.2), which rounds again. The two differ from
  one rounding exactly when the literal lands on a binary64 value midway between two binary32
  values, where the second rounding is a tie and goes to the even mantissa: `f32 x =
  1.000000059604644776257986737988403547205962240695953369140625;` is `1.0` in fort and `1.0000001`
  in C, whose `strtof` rounds the decimal straight to binary32
  (`run/constants/011_float_two_roundings.ft`). No other class of literal can tell the two apart.
  The rule stands as written: one evaluation type for every untyped float keeps the folding of this
  decision in one format, and a second reading would need a rule for every mixed expression.

### D4.5 Default types with no context
- owner: `type-system.md` (Constants), `core-language.md` (Literals).
- rule: With no context at all (for example an argument to `print`), an untyped integer becomes
  `i32` if it fits, otherwise `i64`, otherwise it is an error; an untyped float becomes `f64`; a
  char literal becomes `char`.
- history: Note 2026-09-13 (T-117): "if it fits" reads on an untyped expression that folded to no
  value as well as on a folded constant. The authority is D4.1's last sentence, quoted here as it
  stood on 2026-09-13: "The count operand of a shift is not a context either: in `u64 m = 1 << n;`
  the untyped `1` takes `u64` from the declaration, whatever the type of `n`; with no enclosing
  context it takes its default type." That sentence routes the shifted **constant** of
  `4294967296 << n` to this rule, and the count of the shift changes nothing. So this rule answers
  for a constant that stands inside an expression with no folded value.
  The expression takes `i32` when every constant that meets the type fits `i32`, and `i64` when
  one of them does not. A constant that fits neither is the error. The constants that meet the
  type are the ones D4.1 sends the context to. One type covers the whole expression, which D6.2
  settles and this rule does not: with no promotion between two integer types, two types inside
  one untyped expression would make `1 + (4294967296 << n)` an error.
  A node that folded stands for its whole subtree, because D4.4 folds exactly and `2147483648 - 1`
  is the constant 2147483647: `(2147483648 - 1) << n` takes `i32` and `(2147483647 + 1) << n`
  takes `i64`.
  The other reading gives "otherwise `i64`" to a folded constant alone. D4.1's last sentence
  refuses it: it sends the shifted constant of `3000000000 << n` to this rule exactly as
  `print(3000000000)` reaches it, and no decision gives the two a different answer. Both compilers
  refused the wide case until this note.
  `run/constants/012_untyped_constant_wider_than_i32.ft` and
  `run/errors/033_shift_of_a_wide_default_typed_constant.ft` hold the rule.
  Note 2026-09-14 (T-124): the note above answers for one clause of the three, and the same walk
  answers for all three. One type covers an expression that folded to no value, which D6.2
  forces, so its constants settle on one clause of this rule. **The clause that wins is the
  strongest any constant of the expression asks for, in this order: the float clause, then the
  char clause, then the integer clause.** A fold comes first and is not this rule: `print(1 +
  2.5)` is the untyped float 3.5 by D4.4, so no default type is chosen for the `1` at all.
  The integer clause wins over the char clause because a char literal takes an integer type in an
  integer context while an integer constant never becomes `char` (D4.3), so `c ? 'a' : 98` is an
  `i32` in which the char literal is its code point. The char clause and the integer clause win
  over the float clause because no decision requires an untyped integer or a char literal to take
  a float type. D4.2 permits an untyped integer to take one in a float **context**; with no
  context this rule answers, and the answer it keeps for one untyped expression is the one the
  integer clause gave.
  Unary `-` and `~` are not one untyped expression with what stands beside them, and they answer
  differently. Each gives an operand that folded to no value its default type at once and alone,
  so `-(c ? 1.5 : 2.5)` is a **typed** `f64` by the float clause. A constant beside it then takes
  `f64` from a typed operand, as it takes a type from any typed operand (D4.1), so
  `-(c ? 1.5 : 2.5) + 1` is `-0.5` while `(c ? 1.5 : 2.5) + 1` is the error above. That is the
  float clause working, followed by an ordinary context; it is not two clauses meeting inside one
  expression. Unary `!` takes a `bool` operand and gives the constant that context instead, so
  `!(c ? 1.5 : 2.5)` reports "a float constant does not become bool".
  The type this rule chooses is not always a type every constant of the expression can take. D4.2
  and D4.3 decide that, one constant at a time, and a constant that cannot take it is the error.
  So `c ? 1 : 2.5` is an `i32` and reports "a float constant does not become i32", `c ? 'a' : 1.5`
  is a `char` and reports "a float constant does not become char", and
  `c ? 9223372036854775808 : 1.5` reports the integer against `i64`, which is this rule's own
  tail. `c ? 1.5 : 2.5` is an `f64`, because every constant in it is a float.
  The type this rule chooses then meets the operand rules of D6.2, at the point a context fixes
  it. The operands of an expression that folded to no value carry no value, so the operand rules
  cannot read their kind where the operator stands and are applied again where the type is known:
  `'a' << n` and `(c ? 1.5 : 2.5) & (c ? 2.5 : 3.5)` are errors. A constant that does not fit the
  type is reported first, so `char c = 1 << n;` names the integer constant and not the operand
  rule: the left operand there is the `1`.
  Two expressions fold to no value: a shift whose count is a variable, and a `?:` whose condition
  is a run-time value. A shift takes an integer left operand, so a `?:` is the only expression
  that shows the char clause or the float clause of this rule at run time. `'a' << n` printed 97
  and exited 0 in both compilers until this note.
  `run/constants/013_default_type_of_a_char_and_a_float.ft`,
  `fail/operators/018_char_left_operand_of_a_shift.ft` and
  `fail/constants/010_float_in_integer_context.ft` hold the three.

### D4.6 Constant expressions
- owner: `type-system.md` (Constants), `core-language.md` (Literals).
- rule: Constant expressions (required for array lengths, `case` labels, enum values and
  module-level initializers): literals, `true`, `false`, `null`, module-level immutable declarations
  with constant initializers (from any module), enum members, `sizeof`, `.len` of any expression of
  fixed-array type (the operand is not evaluated, so `m[i].len` is constant for `i32[3][4] m`),
  `$cfg` values (D21.1), unary `- ! ~`, the binary arithmetic, wrapping, bitwise, shift,
  comparison and logical operators,
  `?:`, `cast` among numeric types, `char` and enums (so `cast(color.blue, i32) + 1` may size an
  array), parentheses, and struct or array literals whose leaves are constant expressions. Not
  constant: calls, `&` (except `&global` in module-level initializers, D7.10), field access,
  indexing, span expressions, `.len` of spans or strings, reads of `mut` globals, `null` in a
  `cast`. Typed constant folding respects the declared type and applies every checked-mode rule at
  compile time (overflow, shift count, division by zero, `MIN / -1` and `MIN % -1` are all compile
  errors in a typed constant): `i32 A = 2147483647;` then `A + 1` is a compile error, not a runtime
  trap. Constant references are evaluated lazily with cycle detection; `i32 A = B; i32 B = A;` is an
  error.
- history: Amended 2026-09-16 (T-156): `$cfg` values were not constant expressions.

## D5 Mutability

### D5.1 Immutable by default
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Everything is immutable unless marked `mut`. This applies to variables, parameters, the
  targets of pointers and the elements of spans.

### D5.2 Storage levels
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Storage levels. A declared type is a chain of storage levels numbered from the binding
  inward: level 0 is the binding's own storage; level 1 is the storage reached through the outermost
  indirection (the `*` or `@` whose value the binding holds); level 2 the storage reached through
  the next indirection, and so on. With the suffix-reading rules of D3.6, the outermost indirection
  is always the last reference suffix: of `node**` the last `*` (level 1 holds a `node*`, level 2 a
  `node`), of `node*@` the `@` (level 1 holds `node*` elements, level 2 the nodes), of `i32@@` the
  last `@`, and of `u8@*` the trailing `*` (level 1 holds the span header, level 2 the bytes). Fixed
  arrays and structs do not add a level: their elements and fields share the storage of the value
  that contains them. `string` has a single level (its characters are never mutable). `void*` has
  two: the binding and the storage it reaches, whose type is unknown and whose mutability
  `void mut*` marks (D3.11).
- history: Amended 2026-09-14 (T-086): a `void*` had a single level, like a `string`. Amended
  2026-09-10: spans were called slices (D3.5).

### D5.3 Where a mut marker goes
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Placement rule. A `mut` marks the storage of the type element it follows: after the base
  type, values of that type (`node mut*` points to writable nodes, `u8 mut@` is a span of writable
  bytes); after a `*`, the pointer that suffix introduces, that is, the storage holding it (`node*
  mut p` is rebindable); after an `@`, the span header (`u8@ mut s`); after a fixed-array suffix,
  the array, whose elements share its storage (D5.2), so `i32[4] mut a` marks both and `i32 mut[4]`
  is an error ("mark the array after its length"). Nothing precedes the base type: `mut node* p` is
  an error ("write `node mut* p` or `node* mut p`"). Each storage level has exactly one position, so
  every type has one spelling, and a doubled marker does not parse. The outermost position is the
  binding's own storage, so the `mut` immediately before the name says the binding is assignable,
  for `i32 mut x`, `node* mut p` and `u8@ mut s` alike; `string mut s` is rebindable and `string`
  has no element position (D5.2); `void* mut p` marks the binding and `void mut* p` the storage
  the pointer reaches, and both are legal (D3.11).

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
  | `void* mut vp`       | yes               | no expression reaches it           |
  | `void mut* vp`       | no                | no expression reaches it           |
  | `string mut s`       | yes               | never                              |

- rationale: one rule with no exceptions, C's east-const (`int const x`, `node const* p`, `node*
  const p`) with the default inverted, and the same rule places `own` (D17.2); because reference
  suffixes read inside-out (D3.6), the binding's marker sits next to the name in every declaration,
  and no combination is unspellable.
- history: Amended 2026-09-14 (T-086): the rule said "`void* mut p` is legal and `void mut*` is
  not", and the table held neither row. Amended 2026-09-10: until then a `mut` before the base type
  marked every level including
  the binding and a postfix `mut` marked the storage holding that pointer or header, which made the
  front `mut` mean the variable for scalars and the data for pointers, swapped C's positions, and
  left "writable target, fixed binding" unspellable. Amended 2026-09-10: spans were called slices
  (D3.5).

### D5.4 Dropping mutability
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Dropping mutability is the one implicit conversion. Level 0 (the receiving binding) is
  unconstrained. For a level `k >= 1`, mutability may be dropped only if every level between 1 and
  `k - 1` is immutable in the target type. So `node mut* mut@` converts to `node*@` and to `node
  mut*@`, and converting it to `node* mut@` is refused (a mutable slot could then hold a pointer to
  what the source still sees as a mutable node). This closes the C `T** -> const T**` hole with a
  short recursive check. Nothing adds mutability: no implicit conversion and no cast (D3.14).
  Dropping `own` (D17.4) is the
  other implicit conversion and follows the same monotone shape. The drop applies to initialization,
  assignment, argument passing and `return` only: comparison and `?:` require identical types,
  mutability levels included (D6.2).
- history: Amended 2026-09-14 (T-085): "adding mutability requires `cast`" named an escape the
  cast no longer has. Amended 2026-09-11: the last sentence is a signpost added after T-027 read
  this decision to ask whether `char*` compares with `char mut*` and found no answer here.

### D5.5 Struct fields and return types
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Struct fields. A field's own storage is as mutable as the struct value that contains it
  (level 0 of the field is inherited from the access path), so the outermost position of a field
  type never carries `mut`: `i32 mut count` and `node* mut next` are errors ("a field's own storage
  follows its struct"), while `node mut* next` marks the node behind the field. A return type has no
  binding, so its outermost position never carries `mut` either: `fn find() node mut*` is fine,
  `fn find() node* mut` and `fn f() i32 mut` are errors.

### D5.6 Parameters
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Parameters. `mut` on a parameter follows D5.3; in the outermost position (`i32 mut n`,
  `node* mut p`) it makes the callee's local copy assignable. Function-type identity ignores that
  position (D3.10).

### D5.7 The mutability of an lvalue
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Mutability of an lvalue (D6.7): a variable has its level-0 bit; `*p` and `p->f` have level 1
  of `p`'s type; `e.f` and `e[i]` on a fixed array have the mutability of `e`; `s[i]` on a span has
  level 1 of `s`'s type; `str[i]` is immutable. Assignment, compound assignment, `++`, `--` and `&`
  producing a `T mut*` all require a mutable lvalue.

### D5.8 The types of & and new
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: `&e` has type `T*` where the level-1 bit is the mutability of `e` and deeper levels come
  from `e`'s type. `new` returns the storage it allocates writable and the reference it creates
  `own`, with no outermost `mut` (an rvalue has no binding): `new(T)` returns `T mut* own`,
  `new(T, n)` returns `T mut@ own` (D17.3). The `mut` that `new` supplies is the outermost position
  of `T`, which is the storage it allocates; every position below it is the one the program wrote,
  so the element type of the result is the element type written (D10.2).
- history: Amended 2026-09-14 (T-135): `new` returned the storage writable "at every level", which
  marked positions it does not allocate, and no `mut` parsed inside `new(...)` to say otherwise.

### D5.9 The shallow model
- owner: `core-language.md` (Declarations and mutability), `type-system.md` (Mutability levels).
- rule: Shallow model. Immutability of a variable never propagates through a pointer or span it
  contains; the levels behind an indirection are fixed by the type. `node n` with a field `node mut*
  next`: `n.value = 1` is an error, `n.next->value = 1` is allowed.

## D6 Expressions and evaluation

### D6.1 Operator precedence
- owner: `core-language.md` (Expressions).
- rule: Precedence, highest first: primary (`()` `[]` `.` `->` calls, span expressions, `cast`,
  `sizeof`, `new`, struct and array literals); unary (`! ~ - * &`); `* / % *%`; `+ - +% -%`; `<<
  >>`; `< <= > >=`; `== !=`; `&`; `^`; `|`; `&&`; `||`; `?:` (right-associative). Assignment is a
  statement, not an expression (D7.2). There is no comma operator and no unary `+`.

### D6.2 Operand rules
- owner: `core-language.md` (Expressions).
- rule: Operand rules. Arithmetic `+ - * / %` and wrapping `+% -% *%`: both operands the same
  integer type, or the same float type (`%` and the wrapping forms are integer-only). Unary `-`:
  signed integers and floats only. Bitwise `& | ^ ~`: integers only, same type. Shifts `<< >>`: left
  operand any integer type, right operand any integer type or untyped constant; the result has the
  left operand's type; a constant count that is negative or at least the width of the left operand's
  type is a compile error; `>>` is arithmetic for signed and logical for unsigned types; `<<`
  discards the bits shifted out and never checks for overflow (`1 << 31` on `i32` is `-2147483648`
  in both build modes). Comparisons: same type, ordering only on integers, floats and `char`. `! &&
  ||`: `bool` only. Every mixed-type operation is an error; there is no promotion, not even for
  `u8`/`i8`. "Same type" for comparison and `?:` operands means identical, mutability levels
  included; the implicit drop of D5.4 applies only to initialization, assignment, argument passing
  and `return`. Ownership is the exception: `==`, `!=` and `?:` operands lend, so an `own` and a
  non-`own` operand of the same underlying type compare, and `?:` yields an `own` value only when
  both operands are `own` rvalues or `null` (D17.4).

### D6.3 Evaluation order
- owner: `core-language.md` (Expressions).
- rule: Evaluation order is left to right for operands, arguments, and struct and array literal
  fields. `&&`, `||` and `?:` evaluate only what they need. For an assignment the target's address,
  including any index and its bounds check, is computed before the right-hand side; compound
  assignment computes the target once. Temporaries live until the end of the enclosing statement.
  An aggregate literal reads every member before the destination changes, so `s = pair{s.b, s.a}`
  swaps the two fields. An index evaluates its base to determine its value or storage, then its
  index.
  A span or `string` index reads a stored base header after the index. A span expression evaluates
  its base, then evaluates its explicit lower and upper bounds in that order.
  It reads a stored span or `string` base header after those bounds. An absent upper bound uses
  the length from that read. A base rvalue keeps the header produced during base evaluation.
  The span or `string` operation checks bounds after it reads the header.
- history: Amended 2026-09-28 (T-209): the rule said nothing about a literal that reads its own
  destination, and the emitter wrote such a literal member by member, so `s = pair{s.b, s.a}`
  gave `s.b, s.b`. The user ruled for the swap. The emitter keeps the old behavior until the FIR
  migration replaces it (`fir.md` 12.5). Amended 2026-10-02: the user selected the
  existing order for a stored span or `string` header after the index or explicit bounds.

### D6.4 The cast form
- owner: `core-language.md` (Expressions).
- rule: Casts are `cast(expr, Type)`, a keyword form so the parser never has to guess whether a
  parenthesized name is a type. Semantics in D3.14.
- rationale: user decision.

### D6.5 Struct and array literals
- owner: `core-language.md` (Expressions).
- rule: Struct literals `point{1, 2}` (positional, every field, in order) and `point{.x = 1, .y =
  2}` (designated, any order, omitted fields zeroed, no mixing with positional, no duplicates;
  designated form for structs only) are expressions. `point{}` is all-zero. Typed array literals
  `i32[3]{1, 2, 3}` must have exactly `N` elements or be `{}`. A bare `{...}` is allowed only as the
  initializer of a declaration (local, global, `for` init) whose type is a struct or array, and
  nested inside another literal; `= {}` zero-initializes any aggregate, span, string or enum; `i32 x
  = {};` is an error. Trailing commas are allowed in brace lists and enum bodies, not in parameter
  or argument lists. `IDENT {` is never a block because every control-flow condition is
  parenthesized and every body is braced.

### D6.6 The conditional operator
- owner: `core-language.md` (Expressions).
- rule: `?:` requires a `bool` condition and two operands of one type; untyped constants adopt the
  other operand's type.

### D6.7 Lvalues
- owner: `core-language.md` (Expressions).
- rule: Lvalues: variables and parameters, module-level constants and globals, `*p`, `p->f`, `e.f`
  where `e` is an lvalue, `e[i]` where `e` is an lvalue fixed array or any span or string
  expression, and parenthesized lvalues (a constant is an immutable lvalue: addressable and the
  operand of a span expression, never assignable). `.len` and `.ptr` are never lvalues. Field access
  and indexing on an rvalue struct or array are allowed and yield rvalues (copied through a
  temporary). `&e` requires an lvalue. The ownership proof rejects returned or retained borrows
  that outlive local, temporary, or by-value parameter storage (D17.14).
  This includes addresses into inline aggregate fields and spans of local arrays.
  Borrowed parameter slots and the caller storage they reference have separate storage boundaries.
- history: Amended 2026-10-01 (T-272): The previous rule did not diagnose returned local addresses
  or local-array spans.

### D6.8 Indexing
- owner: `core-language.md` (Expressions).
- rule: Indexing `e[i]`: `e` is a fixed array, span or string; `i` is any integer type or an untyped
  constant. Signed indices are sign-extended, unsigned zero-extended, and one unsigned comparison
  against the length catches negatives. Out of range is a runtime error (D10.6); a constant index
  out of range for a fixed array is a compile error. Pointers cannot be indexed.

### D6.9 Span expressions
- owner: `core-language.md` (Expressions).
- rule: Span expressions `e[lo..hi]`, `e[lo..]`, `e[..hi]`, `e[..]`: `e` is a fixed array (lvalue
  only), a span, or a string; bounds are any integer type or untyped constants; the result is a span
  (or string) whose mutability is that of `e`'s elements. The runtime check is `0 <= lo <= hi <=
  len`, relative to the operand, not the original allocation. `p[lo..hi]` on a `T*` or `T mut*`
  produces a `T@` or `T mut@` without a runtime range check.
  For fort storage, the ownership proof requires a live source and sufficient extent for the range.
  Foreign storage uses D17.13 trust. The operation supplies no proof exemption.
  No span can be taken of a `void*`.
  Ordinary array, span, and string bounds keep their existing runtime-check contract (D10.6).
- history: Amended 2026-09-10: the operation was called slicing and its result a slice (D3.5).
  Amended 2026-10-01 (T-272): Raw-pointer spans previously supplied an unchecked foreign-memory
  escape.

### D6.10 The arrow operator
- owner: `core-language.md` (Expressions).
- rule: `->` is `(*p).f` and is required for pointers; `.` on a pointer is an error with a hint.
  Through a pointer to a span or string, `->` also reaches the `.len` and `.ptr` pseudo-fields
  (`out->len`). Indexing through a pointer to an array or span is written `(*p)[i]`, never `p[i]`
  (D10.4).
- rationale: C familiarity and an explicit dereference.

### D6.11 Calls
- owner: `core-language.md` (Expressions).
- rule: Calls: arguments are matched by position; no defaults, no named arguments, no overloading,
  and no variable tails in fort function definitions or function-pointer types.
  A C extern with `...` accepts a variable tail under D9.8.
  An `own` parameter takes ownership of its argument (D17.5): an `own` lvalue argument
  must be written `move(x)`, an `own` rvalue passes as it is. A function name, a
  function-pointer-typed expression and a qualified name `mod.f` are callable. A `noreturn` call is
  a terminating statement (D8.4).
- history: Amended 2026-09-15 (T-140): C extern calls may supply a declared variable tail.

### D6.12 Float semantics
- owner: `core-language.md` (Expressions).
- rule: Floats follow IEEE 754: `NaN != NaN`, `-0.0 == 0.0`, division by zero yields infinities or
  NaN and never traps. There are no infinity or NaN literals; the standard library provides bit
  casts. `% ++ -- ~ & | ^ << >>` are errors on floats.

### D6.13 Integer division and remainder
- owner: `core-language.md` (Expressions).
- rule: Integer division truncates toward zero and `%` takes the sign of the dividend, as on x86-64
  and in C. Division by zero, and `MIN / -1` and `MIN % -1`, are runtime errors in every build mode,
  checked explicitly at every width.

## D7 Statements

### D7.1 Local declarations
- owner: `core-language.md` (Statements).
- rule: Declarations: `Type name = init;` and `Type mut name = init;`, the marker before the name
  being the binding's own storage (D5.3), one declarator per declaration, initializer mandatory (no
  definite-assignment analysis). A declaration is recognized as a type-looking prefix followed by an
  identifier (`grammar.md`, Disambiguation).

### D7.2 Assignment statements
- owner: `core-language.md` (Statements).
- rule: Assignment `lv = e;`, compound assignment `lv op= e;` for `+ - * / % +% -% *% & | ^ << >>`,
  and postfix `lv++;` `lv--;` (integer types only) are statements. They are not expressions: `a = b
  = c;` and `if (x = 5)` do not parse.

### D7.3 Expression statements and blocks
- owner: `core-language.md` (Statements).
- rule: Expression statements are calls only. `a * b;`, `x;` and `point{1, 2};` are errors. A call's
  result may be discarded. The empty statement `;` is an error. A bare block `{ ... }` is a
  statement and a scope.

### D7.4 The if statement
- owner: `core-language.md` (Statements).
- rule: `if (cond) { } else if (cond) { } else { }`: parenthesized `bool` condition, mandatory
  braces on every branch.
- rationale: mandatory braces remove the dangling-else and "goto fail" bug classes.

### D7.5 Loops
- owner: `core-language.md` (Statements).
- rule: Loops. `while (cond) { }`; `do { } while (cond);` (variables declared in the body are not
  visible in the condition); `for (init; cond; step) { }` where `init` is one declaration, an
  assignment statement, a call, or empty, `cond` is `bool` or empty (true), `step` is an assignment,
  `++`, `--`, a call, or empty; `for (;;)` is legal. The induction variable must be declared `mut`
  like any other (`for (i32 mut i = 0; i < n; i++)`); there is no exception. Range loop `for (T x :
  coll) { }` and `for (T mut x : coll) { }` where `coll` is a fixed array, span or string expression
  evaluated once before the loop. A span or string header is read once at entry, including its
  pointer and length. A fixed array that owns nothing is copied as a value. An owning fixed array
  lends its storage (D17.10). `x` is a fresh copy of each element at the start of its iteration.
  `break` and `continue` target the innermost enclosing loop (`continue` in a `for` runs `step`).
  There is no labeled `break`.
- history: Amended 2026-10-01 (T-263): A span or string header is read once before the loop.
  The previous lowering read an owning header again at each iteration.

### D7.6 The switch statement
- owner: `core-language.md` (Statements).
- rule: `switch (e) { case a, b: ... default: ... }`: `e` is an integer, `char` or enum type (not
  `bool`, not `string`, not a pointer); an untyped constant operand takes its default type (D4.5).
  Labels are constant expressions convertible to `e`'s type, no duplicates after evaluation; at most
  one `default`, in any position. Each case body is an implicit block scope with an implicit `break`
  at its end; there is no fallthrough; an empty case body does nothing (use `case a, b:` to share a
  body). `break` inside a case exits the switch (C semantics; the "break inside switch inside loop"
  trap is documented); `continue` targets the enclosing loop.

### D7.7 An exhaustive enum switch
- owner: `core-language.md` (Statements).
- rule: A `switch` over an enum with no `default` must list every member; otherwise it is a compile
  error. Listing every member is not covering every value: D3.9 zeroes an enum to 0 whether or not 0
  is a member and leaves int-to-enum unchecked, so a value outside the member set is reachable in a
  program with no error in it. The compiler therefore gives such a switch a `default` of its own
  that reports a runtime error naming the enum and the value, and does not return. D10.7 leaves no
  third option: the case is not in its list of undefined behaviour, so it is defined or diagnosed,
  and it cannot be diagnosed at compile time. It is not a bounds check, so `--no-bounds-check` does
  not remove it (D10.6).
- rationale: adding a member then finds every switch that needs updating.
- history: Amended 2026-09-11: the rule said which switches compile and not what the ones it admits
  do with a value no clause names; T-020's review found the emitter naming the continuation as the
  LLVM default, where the epilogue had written `unreachable`, so a legal program became UB that
  `-O1` then folded on.

### D7.8 The defer statement
- owner: `core-language.md` (Statements).
- rule: `defer` followed by an assignment, a `++`/`--` statement, a call statement, or a block
  (`grammar.md`, `defer_stmt`). The deferred code runs when the enclosing block is exited by any
  path: falling off the end, `return`, `break`, `continue`. The set of deferred statements that run
  at an exit is static: those textually before the exit in each exited block, innermost block first,
  in reverse order within a block. Nothing is captured at `defer` time; the statement is ordinary
  code executed at exit: after `defer del(p);`, a later `del(p); p = move(q);` makes the deferred
  statement free `q`. `return e` evaluates `e` before deferred code runs, so deferred code cannot
  change the returned value. `return` inside deferred code, and `defer` at module level, are
  errors. `break` and `continue` inside deferred code bind to the innermost loop or `switch`
  written inside that deferred code (D7.5, D7.6); the constructs around the `defer` are out of
  reach. With no such construct inside it, a `break` or a `continue` there is an error, the same
  error it is anywhere else. Runtime errors (D11.4) do not run deferred code. Because `return x` of
  an `own` local empties `x` before the deferred code runs (D17.5, D17.6), `defer del(buf);`
  followed later by `return buf;` frees `buf` on every path except the one that hands it to the
  caller.
- rationale: fixes the multi-return cleanup pitfall from memory-model.md with a purely static
  expansion, no runtime list.
- history: Amended 2026-09-14 by T-075: the rule banned `break` and `continue` inside deferred
  code outright, which is broader than its own reason. The reason is that deferred code is the
  expansion of an exit, so an exit that leaves the block the expansion unwinds has nowhere to go.
  A `break` that leaves a loop written wholly inside the deferred code leaves nothing the unwind
  expands, and `defer { while (true) { break; } }` was refused for no reason the decision
  gave. The ban now covers `return` alone, which would leave the function in the middle of an
  unwind. The user chose the narrow rule on uniformity: every other construct binds `break` to the
  innermost enclosing loop, and a reader had to learn that deferred code alone did not.

### D7.9 Scoping and shadowing
- owner: `core-language.md` (Statements).
- rule: Scoping and shadowing. One namespace per module holds functions, structs, enums, constants,
  globals, externs and import bindings; any collision is an error. Lookup goes from the innermost
  block outward, then the module namespace, then the universe (D12). A local or parameter may not
  reuse the name of any enclosing local or parameter. It may shadow a module-level name (including
  an import binding) or a universe name, which is then inaccessible within its scope; a module-level
  declaration may likewise shadow a universe name. Enum members are not in the module namespace
  (D3.9). Sibling scopes may reuse names. A local's scope starts after its own declaration (`i32 x =
  x;` is an error).
- rationale: otherwise `import std.io;` would forbid a parameter named `io` anywhere in the module.

### D7.10 Module-level declarations
- owner: `core-language.md` (Statements).
- rule: Module-level declarations. `Type NAME = init;` is a compile-time constant: it lives in
  read-only memory, is addressable, and is usable in array lengths and `case` labels. `Type mut g =
  init;` is a global in writable memory. Initializers must be constant expressions (D4.6) extended
  with `null`, function names, `&` of a module-level declaration from any module, and struct or
  array literals of those. No calls and no reads of `mut` globals, so there is no initialization
  order. Top-level declarations are order-independent within a module (no forward declarations);
  struct sizes and constant values are resolved lazily with cycle detection.

### D7.11 The return statement
- owner: `core-language.md` (Statements).
- rule: `return e;` in a `void` function and `return;` in a non-`void` function are errors. A
  non-`void` function must end in a terminating statement (D8.4).

## D8 Functions

### D8.1 Function declaration syntax
- owner: `core-language.md` (Functions).
- rule: Declaration syntax: `fn name(Type p1, Type p2) ReturnType { ... }`, with `void` for no
  result: `fn main() void { }`. The return type is never omitted. Function-pointer types read the
  same way, with the result last: `fn (i32, i32) i32`. A parameter and a local keep `Type name`.
- rationale: the name is the second token of every declaration, at a fixed column, so
  `grep 'fn take('` finds the definition and nothing else; the keyword makes top-level and
  statement-level parsing unambiguous.
- history: Amended 2026-09-14 (T-136): the return type stood between `fn` and the name
  (`fn ReturnType name(params)`, and `fn R(P)` for a function type), so the name began at a column
  the return type's width decided and `fn strbuf.str_buf mut* own take(` buried it. The user
  ratified the reversal on 2026-09-14. One commit moved both parsers, the corpus and this log, so
  the old form parses nowhere after it.

### D8.2 Parameters and results pass by value
- owner: `core-language.md` (Functions).
- rule: Parameters are passed by value: primitives, pointers, spans and strings by copying the
  scalar or the fat pointer; structs and fixed arrays by copying the whole value. Results are
  returned by value likewise. There are no reference parameters; use `T mut*`.

### D8.3 What functions do not have
- owner: `core-language.md` (Functions).
- rule: Fort function definitions and function-pointer types have no variable tails.
  Functions have no nested definitions, closures, overloading, default arguments or methods.
  Recursion is allowed; depth is bounded only by the OS stack.
  A C extern declaration may mark a variable tail under D9.8.
- history: Amended 2026-09-15 (T-140): C extern declarations may mark C variadics.

### D8.4 Terminating statements
- owner: `core-language.md` (Functions).
- rule: Terminating statements: `return`; a call to a `noreturn` function or to `panic`; an `if`
  with an `else` whose branches both terminate; `while (true)`, `for (;;)` or a `for` with an empty
  condition, with no `break` targeting it; a `switch` all of whose cases terminate, that no `break`
  targets, and that either has a `default` or is an exhaustive enum switch (D7.7), which terminates
  through the default the compiler gives it; a block whose last statement terminates. A `break`
  inside a loop or a `switch` that stands in a case targets that inner construct (D7.5, D7.6), and
  a `continue` targets no `switch`. A non-`void` function body must end in a terminating statement
  or it is a compile error ("missing return", reported at the body's closing brace).
- rationale: catching this at compile time is a core "better than C" promise, and the structural
  rule is a few dozen lines to implement.
- history: Amended 2026-09-30 (T-210): the rule for a `switch` did not name a `break`. A `break` in
  a case leaves the `switch`, so control fell off the end of a function that the checker accepted,
  and the emitted code reached `unreachable`. The rule for a `switch` now has the words that the
  rule for a loop already had.

### D8.5 The noreturn result type
- owner: `core-language.md` (Functions).
- rule: `noreturn` is a return type: `fn fatal(string msg) noreturn { ... }`. Such a function may
  not contain `return` and must end in a terminating statement; the compiler emits a trap after its
  body and after every call to it. `panic` and `sys.exit` are `noreturn`.
- rationale: without it every error-reporting helper forces a dead `return` after each call.

### D8.6 The entry point
- owner: `core-language.md` (Functions).
- rule: Entry point: the module given to `fort` must define `fn main() i32` or `fn main(string@
  args) i32`. `args[0]` is the program name; each element is NUL-terminated because it comes from
  `argv`. The return value is the exit status. A `main` returning `void` is an error. A `main` in
  any other module is an ordinary function.
- history: Amended 2026-09-10 with D20: `fort --check` inspects a module rather than building a
  program, so it does not apply this rule (D20.1).

## D9 Modules, namespaces and FFI

### D9.1 Module paths
- owner: `module-system.md`.
- rule: One module per file; the module path is the file path relative to a search root, with `.`
  separating segments and `.ft` dropped: `std.io` is `<std>/io.ft`, `util.strings` is
  `<root>/util/strings.ft`. Every segment must be an identifier that is not a keyword, so the
  earlier `std.string` is `std.str`. The entry file is the exception: it is named on the command
  line rather than reached by an import path, so its base name need not be an identifier, and a name
  that is not one simply cannot be imported by anything (`007_case.ft` is the module `007_case`,
  whose name reaches the generated module only inside a quoted symbol, D9.7). It may not contain a
  `.`, the one character a module path is spelled with and so the one that breaks the injectivity
  D9.7 rests on: an entry `my.app.ft` is the module `my.app` and emits `my.app.main`, already the
  symbol of a `main` in the module `my.app` that is `my/app.ft`. Nothing else is barred: the mangler
  is the identity on a module path (D9.7), so every other character reaches the symbol verbatim, and
  a name holding one cannot spell a path, whose every segment is an identifier.
- history: Amended 2026-09-10: module-system.md required the entry base name to be a segment, which
  would have rejected every test file D14.4 names `NNN_name.ft`. Amended 2026-09-10, separately:
  that exception admitted a dotted base name, which the compiler accepted and then emitted two
  definitions of one symbol for -- invalid IR, exit 0, caught only by `opt`. Amended 2026-09-10, a
  third time: barring `.` alone was incomplete, since the mangler also transforms `:`; T-070's
  review found `my:app.ft` still colliding with a module `myapp`, by the same silent route. Amended
  2026-09-11 by T-080, which made the separator `.` where it had been `::`: a `:` was barred because
  the mangler wrote a `::` as `.` and dropped a `:` it could not pair, so `my:app.ft` emitted
  `myapp.main` and collided with a module `myapp`. The mangler now translates nothing, `my:app.ft`
  emits `my:app.main`, and no module path can spell that, since every segment of one is an
  identifier and an identifier holds no `:`. The bar on `:` is therefore dropped and only the bar on
  `.` is re-derived. A `:` reaches the ELF symbol through a quoted LLVM name (D9.7) and through the
  assembler, which quotes it in turn; `bootstrap0/test/gen_symbols_test.c` holds the IR name (the
  hand-written end-to-end witness `test/ir/colons.ll` was retired 2026-09-25).

### D9.2 Search roots
- owner: `module-system.md`.
- rule: Search roots, in order: the directory containing the entry file; each `-I` directory; the
  standard library directory. The first segment `std` is reserved for the standard library
  directory. The current working directory is never searched. Import paths are root-relative (a file
  in `util/` imports its sibling as `util.other`). A module's identity is the real path of its file;
  reaching one file through two different paths is an error.

### D9.3 Import forms
- owner: `module-system.md`.
- rule: Import forms, only at the top of a file before any declaration: `import a.b;` binds the
  short name `b` to the module; `import a.b as c;` renames it; `import a.b.sym;` binds the symbol
  `sym` from module `a.b`; `import a.b.sym as alias;`; `import a.b.{s1, s2 as t};` is sugar for
  independent symbol imports of `s1` and `s2` from module `a.b` (the prefix must be a module; the
  braces never name modules). Resolution of a path `p.last`: the module reading (`p/last.ft` exists)
  and the symbol reading (`p.ft` exists and declares `last`) are both tried; exactly one must
  succeed, otherwise the import is an error: "not found" when neither reading succeeds, "has no
  declaration named" when only `p.ft` exists but lacks `last`, "ambiguous" when both succeed. No
  wildcard imports. A duplicate binding is an error; importing the same module under two names is
  allowed; import bindings are not re-exported.
- history: Amended 2026-09-11: every form was spelled with `::`, `import a::b::{s1, s2 as t};`
  included; T-080 made the separator `.` throughout, so the grouped form is `import a.b.{s1, s2 as
  t};` and `::` ceases to be a token with no exception (D2.10).

### D9.4 Qualified access
- owner: `module-system.md`.
- rule: Qualified access uses a dot in expressions and in type positions: `io.read_file(p)`,
  `math.vector v = ...;`, `m.color.red`.
- rationale: consistent with field access, and the resolver knows which identifiers are modules.

### D9.5 Circular imports
- owner: `module-system.md`.
- rule: Circular imports are a compile error even though whole-program compilation would permit
  them. Consequence, documented: mutually referential types must live in one module.

### D9.6 Everything is exported
- owner: `module-system.md`.
- rule: Everything at module level is exported in v1. Visibility modifiers are deferred.

### D9.7 Symbol names
- owner: `module-system.md`.
- rule: Symbol names in the generated code are the module path, a dot and the declaration name:
  `std.io.read_file`, `main.main`. The path is dot-separated already (D9.1), so the mangler is the
  identity on it and copies it across. Dots are legal in ELF and Mach-O symbols and cannot appear in
  identifiers, so splitting a symbol on its dots recovers the segments it was built from, the last
  being the declaration name and the rest the module path, and the scheme is injective. That
  argument needs every segment of a module path to hold no dot, which is why the entry file's base
  name, the one segment that need not be an identifier, may hold none either (D9.1). The runtime is
  ordinary fort and its symbols are mangled like every other module's (`std.rt.print_i64`, D13.1).
  The compiler emits one unmangled definition, in the entry module: `main` (D11.6). `extern` names
  are unmangled. A name is quoted in LLVM IR when LLVM's unquoted identifier
  syntax does not admit it (`@"std.io.read_file"`), and a `"`, a `\` or any byte outside the
  printable range within it is written `\XX`; that is spelling only, since LLVM reads `\XX` back to
  the byte, so the IR name keeps its identity unchanged.
  Mach-O adds one leading `_` to each external object symbol; the IR name does not contain it.
  `main` is reserved: the compiler emits its definition (D11.6), so an `extern` declaring the name
  is an error and not a second declaration of it -- nothing can check a declared signature against a
  definition the compiler writes itself, and a mismatch is otherwise a silent call through the wrong
  type. The reserved `main` is the C entry point and not the entry module's `fn i32 main`, whose
  symbol is `<entry>.main` (D8.6) and which collides with nothing. Fort functions, constants and
  globals are `dso_local` with the default external linkage (D9.6), so fort-to-fort calls are direct
  and fort data is addressed PC-relative; `extern` symbols are not `dso_local`.
  Linux reaches them through procedure linkage and global offset tables.
  Mac reaches them through Mach-O stubs and global offset tables.
  The runtime is fort, so a call into it is a direct fort-to-fort call like any other.
- history: Amended 2026-09-10 with D19: the assembler directives that spelled this became IR linkage
  words. Amended 2026-09-11: quoting was said to cover dotted names and to keep the emitter free of
  per-name analysis, which left a legal entry base name holding a `"` emitting invalid IR that clang
  rejected, and made `extern fn fort_entry` a `declare` beside a `define` -- caught by `opt` until
  T-018 dropped the declaration, and a silent SIGSEGV after (T-018's review). Amended 2026-09-11: a
  module path was spelled with `::` (D9.1) and the mangler wrote each `::` as a `.` and dropped a
  `:` it could not pair; T-080 made the separator `.`, so there is nothing left to translate and the
  injectivity argument is the splitting one above. Amended 2026-09-11 (T-088): runtime symbols were
  C names in a reserved `fort_rt_` space, that reservation is withdrawn with the C runtime (D13.1 as
  amended), and `main` joined `fort_entry` as a name the compiler emits and an `extern` may not
  declare (D11.6).
  Amended 2026-09-15 (T-140): Mac uses Mach-O stubs and the same fort symbol names.
  Amended 2026-09-28 (T-212): the compiler also emitted `fort_entry` in the entry module, and that
  name was reserved as well. The emitted `main` now calls the entry module's `main` itself
  (D11.6), so the compiler emits one unmangled definition and `fort_entry` is an ordinary C name.

### D9.8 Extern declarations
- owner: `module-system.md`.
- rule: `extern fn write(i32 fd, u8* buf, u64 n) i64;` declares a C function.
  Linux x86-64 uses the System V C ABI. Mac arm64 uses the Apple arm64 C ABI.
  The signature may use integers, floats, `bool`, `char`, enums, pointers and function pointers.
  The signature excludes spans, strings, structs and arrays by value.
  `extern fn printf(char* fmt, ...) i32;` declares a C variable tail.
  It follows one or more fixed parameters.
  Only an extern declaration may write `...`. A fort definition or function-pointer type cannot.
  A call supplies at least the fixed parameter count and may supply more arguments only for `...`.
  A variable tail accepts `i32`, `u32`, `i64`, `u64`, `f64`, pointers and function pointers.
  It rejects narrow integers, `bool`, `char`, enums, `f32` and aggregates as direct tail arguments.
  The caller casts narrow integers, `bool`, `char` and enums to `i32` for C default promotions.
  It casts `f32` to `f64`. A variable tail does not transfer ownership.
  An owning pointer lvalue lends through the tail; an owning rvalue is an error (D17.8).
  The compiler checks tail types, not C format strings or a C callee's expected tail types.
  A fixed extern uses a fixed LLVM declaration and call form on both targets.
  An extern with `...` uses a variadic LLVM form with its fixed prefix.
  On Linux the variadic call sets the System V vector-register count in `al`.
  Apple arm64 puts fixed C arguments in their normal registers and the variable tail on the stack.
  A fixed declaration of a variadic C function is an incorrect C prototype on both targets.
  The variable tail must appear in that function's extern declaration.
  An extern name is not a value; its ABI form comes from its declaration (D3.10).
  Extern call sites and extern declarations are `nobuiltin`, so rewriting cannot replace a
  declared C symbol.
  Narrow fixed parameters and results use D9.9's `zeroext` and `signext` attributes.
  C `char*` maps to `char*` or `u8*`; `size_t` to `u64`; `ssize_t` and `off_t` to `i64`.
  C `mode_t` maps to `u32` on Linux and `u16` on Mac; C `int` maps to `i32`.
  C `long` maps to `i64`; C `double` maps to `f64`.
  A variable mode argument to C `open` uses promoted `i32` or `u32`, not Mac's fixed `u16`.
  The same C symbol may be declared in several modules when the signatures are identical.
  Identity includes the variable-tail mark, fixed parameter types, result type and `own` (D17.13).
  Parameter names do not affect identity. Fort `char` is C's `unsigned char` (`i8 zeroext`, D3.2).
  The compiler
  never emits a call to a C symbol of its own accord: anything it needs at run time is a call to a
  function of `std.rt` (D13.1), whose symbols are mangled fort names (D9.7) and so cannot be the
  name of any C library function. A compiler-emitted `@memcmp` would be a second declaration of an
  target C symbol a program may also declare `extern`.
  This breaks D9.7's one entity per symbol, and it is
  the library-call rewriting `nobuiltin` exists two sentences above to prevent. `std.rt` reaches the
  C library through `std.libc`, with `extern` declarations that answer to this decision like every
  other module's. The runtime occupies no C name of its own, so nothing a program declares `extern`
  can collide with `std.rt` itself; but `std.rt` is in every closure (D9.10) and imports are
  transitive, so `std.libc`'s declarations are in every closure too, and a program that declares one
  of those C symbols itself must now agree with `std.libc`'s signature, `own` included (D17.13).
  That is a real tightening and it is this decision working: two disagreeing declarations of one C
  symbol are one target C symbol reached through two types.
  This is the bug the identity rule exists to
  catch. A program that wants a different spelling of `write` or `free` imports `std.libc` and calls
  it rather than redeclaring it.
- history: Amended 2026-09-14 (T-136): an extern was written `extern fn R name(P);`, with the
  result before the name, and it now matches a definition's shape (D8.1).
  Amended 2026-09-14 (T-086): the example declared `write` with a `void* buf`, which
  `std.libc` no longer spells that way, so a program that copied the line conflicted with the
  library's declaration by this decision's own identity rule. Amended 2026-09-10 with D19: the
  compiler itself set `al` to the vector-register count
  before every extern call, and extended narrow values by hand. Amended 2026-09-11: T-015 found that
  `string ==` needed `memcmp` and stopped rather than invent a fourth declaration group for
  `toolchain.md` 6 item 8; there is no fourth group. Amended 2026-09-11: "identical" did not say
  whether it meant the types or what the two modules wrote, and the bootstrap read it as the written
  declarations, since the check ran in the loader, before types exist. That refused a binding-level
  `mut`, which is not part of a function type (D3.10) and changes no emitted byte, and it refused
  two spellings of one imported enum -- for which no shared spelling exists, since a module can
  neither qualify a name with its own module name nor import itself, so such a program could not be
  written at all -- while accepting two genuinely different local types of one spelling. Amended
  2026-09-11 (T-074): "as written" is withdrawn and the word is again "identical", meaning the
  types; the check runs in the checker, where they exist. `char` and `u8` are one C type there,
  since fort `char` is C's `unsigned char` (D3.2) and this decision maps a C `char*` to either, and
  a struct or an enum is identified by the declaration it comes from (D3.8, D3.9), so two modules
  that each declare one do conflict. The diagnostic names the differing parameter and, where either
  declaration names a struct or an enum, carries a note saying that such a type is its declaration
  and not its spelling, with the two ways out: give both declarations that one type, importing it
  where it is missing, or declare the symbol in one module and export a fort function the others
  import. Either, not each: one module writing the `i32` an enum crosses as is the same class of
  fix, the enum being importable. Amended 2026-09-11 (T-088): an `extern fn` naming a runtime entry
  point was a case of its own -- the checker held it against the compiler's own prototype and the
  emitter replaced it with a canonical declaration -- because the runtime was C and shared the C
  name space with every program. The runtime is fort (D13.1 as amended), so there is no such name to
  declare and the case is withdrawn: an `extern` naming `fort_rt_del` is an ordinary declaration of
  an ordinary C symbol, and `toolchain.md` 6 item 8 has two declaration groups, externs and
  intrinsics. The same amendment tightens this rule where it used not to bite: `std.libc` rides into
  every closure behind `std.rt` (D9.10), so a program's own declaration of a libc symbol is now held
  against `std.libc`'s, where before it was held against nothing unless the program imported the
  library.
  Amended 2026-09-15 (T-140): C externs gained an explicit variable tail for both targets.
  Mac fixed externs gained fixed LLVM calls. Linux fixed extern IR keeps its old form.
  Amended 2026-09-15 (T-149): Mac extern declarations gained `nobuiltin`.
  Apple clang removed a failed `calloc` and matching `free` when the caller read no storage.
  The existing call-site `nobuiltin` did not keep that allocation failure observable.
  Amended 2026-09-17: before this date a Linux fixed extern kept a variadic LLVM declaration and
  call form, and only Mac extern declarations carried `nobuiltin`. From that date both targets
  used the fixed form for a fixed extern and `nobuiltin` on every extern declaration. A Linux
  fixed declaration of a variadic C function became an incorrect C prototype, so `std.libc`
  declared `open` and `snprintf` with `...` on Linux as it did on Mac.

### D9.9 The internal calling convention
- owner: `module-system.md`.
- rule: Internal calling convention (v1 simplification): integers, pointers, `bool`, `char`, enums,
  function pointers and floats use the selected target's scalar C registers; every aggregate
  (struct, fixed array, span, `string`) is passed by a hidden pointer to a caller-made copy and
  returned through a hidden result pointer. Struct layout stays C-compatible, so pointer-based
  interop works. A fort function is usable as a C callback exactly when its signature is fixed
  and extern-legal, function-pointer parameters included (D3.10).
  In LLVM IR an aggregate argument is a
  plain `ptr` parameter, never `byval`, and an aggregate result is a leading `ptr sret(%T)`
  parameter on a function returning `void`; a span or `string` stays one hidden pointer and is never
  split into two scalars, so the entry module's `main` takes the argument span as one `ptr` (D11.6)
  by this rule and by no exception to it. `bool`, `char`, `u8` and `u16` parameters and results
  carry `zeroext`, `i8` and `i16` carry `signext`, and nothing wider carries an extension
  attribute, in fort and extern signatures alike (D9.8).
  The definition and every call mark the result pointer `sret(%T)` on both targets.
  System V passes it in the first integer register; Apple arm64 puts it in `x8`.
  A plain Mac call pointer would take `x0` and would break a return with scalar parameters.
  Extern signatures exclude aggregate results, so these result rules govern fort-to-fort calls.
- history: Amended 2026-09-10 with D19: the register-level spelling of the same convention is now
  LLVM's job, and the prototype read `const struct fort_slice*` while spans were called slices
  (D3.5). Amended 2026-09-11 (T-088): `fort_entry` was called from the C runtime, so this decision
  stated its C prototype; with `main` emitted by the compiler (D11.6 as amended) its span parameter
  is an ordinary aggregate parameter of this convention.
  Amended 2026-09-15 (T-140): Apple arm64 calls need call-site `sret` to use `x8`.
  Amended 2026-09-17: before this date a Linux call passed the result pointer as a plain `ptr`.
  From that date the call marked it `sret(%T)` on both targets, as the definition did.
  Amended 2026-09-28 (T-212): the rule named the compiler-emitted `fort_entry` as the function
  that took the argument span as one `ptr`. The compiler no longer emits it (D11.6 as amended),
  and the entry module's `main` takes the span by the same rule.

### D9.10 Whole-program compilation
- owner: `module-system.md`.
- rule: Whole-program compilation: the compiler walks the import closure from the entry file,
  type-checks every module, emits one LLVM IR module (D19.1), and runs `--cc` over it once to
  compile and link it (D14.3). Interface files, separate compilation, a module cache and incremental
  rebuilds are deferred. Every closure holds `std.rt` (D13.1), which the compiler loads as a root of
  its own beside the entry file, in a build and under `--check` alike, so the runtime is parsed,
  checked and emitted like any other module and its records appear in the index (D20.3). What
  `std.rt` imports is in the closure with it, `std.libc` above all, so every program is now compiled
  against those `extern` declarations and D9.8's identity rule binds its own declarations of those C
  symbols to them. Membership does not bind the name: a module that wants to name the runtime writes
  `import std.rt;` like any other importer (D9.3), and one that does not never sees it. A flag that
  leaves it out is deferred (D15).
- history: Amended 2026-09-10 with D19: the compiler emitted one assembly file that the system C
  compiler assembled and linked. Amended 2026-09-11 (T-088): the closure was the entry file's alone
  and `--cc` linked the C runtime object into it (D13.1 as amended). Note 2026-09-14 (T-132):
  `std.rt` imported `std.libc` alone, so every closure held two library modules; the fold of the
  float printers gave it `std.strbuf` as well, and `std.strbuf` imports `std.mem`, so every
  closure holds four. The sentence above does not change and the count is not in it; the cost of
  the three extra modules is measured in `notes/compiler.md` 7.

## D10 Memory and runtime checks

### D10.1 Stack and heap
- owner: `memory-model.md`.
- rule: Stack: locals, parameters, fixed arrays and struct values; freed at scope exit. Heap: only
  through `new`; freed only through `del`.

### D10.2 The new builtin
- owner: `memory-model.md`.
- rule: `new(T)` returns `T mut* own` to one zero-initialized `T`, for every `T` including an array
  (`new(u8[4])` is a `u8[4] mut* own`); `new(T, n)` returns `T mut@ own` of `n` zero-initialized
  elements (D17.3), where `n` is any integer type; a negative `n`, a size that overflows, or
  allocation failure is a runtime error; `n == 0` is allowed and yields a non-null pointer (the
  runtime allocates at least one byte). `new(T{...})`, `new(T@)` and `new(void)` are errors. Inside
  `new(...)` an `own` parses only after a `*` (D17.3), so `new(string own)` does not parse, and a
  `mut` parses below the outermost position of `T` but never in it, so `new(T mut)` and `new(T*
  mut)` do not parse while `new(T mut*, n)` does. **The element type of the result is the element
  type written**: `new(void mut*, 16)` is a `void mut* mut@ own` and `new(void*, 16)` is a `void*
  mut@ own`. A fixed array shares its storage with its elements (D5.2), so the position a `[N]`
  follows takes no `mut` here either and D5.3 is the rule that refuses `new(i32 mut[4], n)`.
  `new(T*)` allocates one pointer slot and is legal.
- rationale: Rationale for zero-initialization: keeps "no undefined values" true at the cost of one
  `calloc`; the earlier "uninitialized" text is withdrawn. Rationale for the outermost position:
  `new` marks the storage it allocates (D5.8), and that storage is the outermost position of `T`
  and nothing below it. A position below it describes storage `new` did not allocate -- what a
  pointer in the fresh memory would reach, and that pointer is null -- so the program says what it
  means there and `new` supplies the one mark that is its own.
- history: Amended 2026-09-14 (T-135): a `mut` never parsed inside `new(...)` and the result was
  writable at every level, so `new(node*, n)` answered `node mut* mut@ own` and the element type of
  the result was not the element type written. Three sites of the self-hosted compiler
  (`src/fort/gen.ft`, twice, and `src/fort/types.ft`) cast the result back down to the element type
  they had asked for, and one of them carried a comment naming this rule as the cause. Amended
  2026-09-10: the count was written inside the type (`new(T[n])`), which left no spelling for one
  array object, and a span was called a slice (D3.5).

### D10.3 The del builtin
- owner: `memory-model.md`.
- rule: `del(x)` frees the allocation designated by its operand, which must have an `own` type; the
  full rules, including that `del` empties an lvalue operand and that `del` of a view, sub-span,
  `.ptr`, stack address or literal is a compile error, are D17.9. Allocation has no header, so
  `new`/`del` and C `malloc`/`free` are interchangeable; memory from C is owned when its `extern`
  declaration says `own` (D17.13).
- history: Amended 2026-09-29 (T-257): the rule read "memory from C is adopted with `cast`
  (D17.3)". A cast no longer adds `own` (D3.14).

### D10.4 No pointer arithmetic
- owner: `memory-model.md`.
- rule: No pointer arithmetic: `p + 1`, `p++` and `p[i]` are errors. The only ways to get a pointer
  are `null`, `&`, `new`, `.ptr`, `cast`, a function name, and calls; the only way to get a span
  from a raw pointer is the two-bound form `p[lo..hi]` (D6.9); `p[lo..]`, `p[..hi]` and `p[..]` are
  errors because a pointer has no length.

### D10.5 The null value
- owner: `memory-model.md`.
- rule: `null` is the zero pointer and function-pointer value. `== null` and `!= null` are allowed
  only on pointers, `void*` and function pointers; spans and strings compare `.len` or `.ptr`.
  `null` has no type of its own: it is usable only where a pointer, `void*` or function-pointer type
  is expected, so `print(null)` and `null == null` are errors. Dereferencing `null` is undefined
  behavior (a segfault in practice). Between two operands neither of which is `null`, the same-type
  rule is D6.2's.
- history: Amended 2026-09-11: the last sentence is a signpost added after T-027 consulted this
  decision for a pointer comparison that did not involve `null`.

### D10.6 Bounds checks
- owner: `memory-model.md`.
- rule: Bounds checks are performed on every index and span operation, in every build mode;
  `--no-bounds-check` disables them for benchmarking and is documented as unsafe.

### D10.7 Undefined behavior in v1
- owner: `memory-model.md`.
- rule: The ownership proof rejects invalid or unproved fort temporal storage operations (D17.14).
  Foreign trust can hide invalidation, dangling results, ownership duplication, and invalid release
  (D17.13). These foreign violations remain undefined behavior.
  Dereferencing `null`, calling a null function pointer, and data races remain undefined behavior.
  Foreign raw-pointer ranges beyond the object remain undefined behavior.
  The proof rejects a proved empty-owner dereference. It does not establish total memory safety.
  Existing bounds, nullability, arithmetic, and data-race rules still apply.
  Everything else is defined or a diagnosed error. There is no strict-aliasing rule.
  Reading an object through another same-size type yields its bit pattern:
  `*cast(&x, u64*)` for an `f64 x` is defined.
  Same-size reinterpretation retains source and extent obligations (D6.9, D17.14).
- history: Amended 2026-09-29 (T-257): the list held "`del` of adopted memory (D17.3) that is not
  the start of an allocation" and "writing through a cast that added mutability into read-only
  memory". A cast adds neither `own` nor `mut` (D3.14); the second item was stale since T-085.
  Amended 2026-10-01 (T-272): The previous list left fort dangling views and local storage escapes
  undiagnosed.

### D10.8 Stack probes
- owner: `memory-model.md`.
- rule: Frames larger than one page are probed so that a large local array plus recursion faults
  instead of skipping the guard page. Linux and Mac request `"probe-stack"="inline-asm"` on every
  fort definition.
  A module produced by `-S` carries the guarantee whatever the driver line is (D19.1).
- history: Amended 2026-09-10 with D19: the compiler emitted the page touches itself.
  Amended 2026-09-15 (T-140): Apple clang's C IR uses `__chkstk_darwin`.
  Amended 2026-09-17: Mac requested `"probe-stack"="__chkstk_darwin"` before this date. Only
  Apple's LLVM implements that probe on arm64: Homebrew clang 19.1.7 rejected it with
  `Unsupported stack probing method`. Apple clang 21 and clang 19.1.7 both accept `inline-asm` and
  emit the probes, so the default `--cc` of D14.3 can be any clang.
  Amended 2026-09-30 (T-268): every host now uses LLVM 18. Homebrew clang 18.1.8 on arm64 Mac
  accepts `inline-asm` and emits the same probe loop as clang 19.1.7 for a 100000-byte frame: a
  4096-byte `sub sp` and a `str xzr, [sp]` per page before the remainder.

## D11 Build modes and the runtime contract

### D11.1 The two build modes
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: Two build modes. **Checked**, the default: `+ - *`, unary `-`, `++ --` and the compound
  assignments trap on overflow for signed and unsigned integers; a shift count that is negative or
  at least the operand width traps; storing over a live `own` value traps (D17.11). **Release**
  (`fort --release`): overflow wraps in two's complement, the shift count is taken modulo the width,
  and owned values are overwritten without a check. Programs must not rely on either behavior for
  correctness.
- rationale: user decision ("trap in debug, wrap in release").

### D11.2 The wrapping operators
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: `+% -% *%` (and `+%= -%= *%=`) wrap in both modes. They exist so hashes, checksums and
  counters can be written once and behave identically in both modes. Recommended addition, flagged
  for the user.

### D11.3 Division errors in both modes
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: Division by zero and `MIN / -1` (D6.13) are runtime errors in both modes.

### D11.4 The runtime error contract
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: Runtime error contract: the runtime flushes buffered output, writes one line to stderr, and
  calls `abort()`, so the process dies with SIGABRT (status 134 under a shell). Formats:
  `<file>:<line>:<col>: runtime error: <message>` for checks (bounds, overflow, shift, division,
  allocation, ownership, enum `switch`), `<file>:<line>:<col>: panic: <message>` for `panic`, and
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
  | an enum value no clause names (D7.7) | `enum value 0 is not a member of level`          |

  The end of a `noreturn` function has a trap (D19.7). Linux x86-64 raises SIGILL.
  Mac arm64 raises SIGTRAP. Neither target writes a message for this trap.
  A conforming body never reaches it. `<file>` is the path the compiler opened (search root as given
  plus the relative module path); the column of a check is that of its operator token, or of the
  builtin's name for `new`, `assert` and `panic`, or of the `switch` keyword for the default of
  D7.7; the `assert` text is the source text of the expression, verbatim.
- history: Amended 2026-09-10: the bounds message read `slice bounds ...` while spans were called
  slices (D3.5). Amended 2026-09-11 (T-020): the enum `switch` default of D7.7 was added.
  Amended 2026-09-15 (T-140): the `noreturn` trap now names both target signals.

### D11.5 Output buffering
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: Output buffering: `print`/`println` write to a runtime buffer for stdout;
  `eprint`/`eprintln` are unbuffered; `fprint`/`fprintln` use one runtime buffer per descriptor, and
  `fprint(1, ...)` shares the stdout buffer with `print`. An `extern` write to a descriptor bypasses
  the buffers. Buffers flush when full, at exit, and before any runtime error. A buffered descriptor
  that is **interactive** is line-buffered as well: a newline written through it flushes the whole
  buffer, not only the bytes up to that newline, which is C's rule (C11 7.21.3p7) and makes a
  terminal show each `println` as the program runs. Interactive means `isatty` of the descriptor,
  and the runtime asks once, when it creates that descriptor's buffer, so that no `print` carries a
  system call of its own; every buffered descriptor is decided that way and not stdout alone, since
  `fprint(fd, ...)` on any descriptor has the same policy. The runtime exports `std.rt.flush(i32
  fd)` and `std.rt.flush_all()`; `io.close` and `io.flush` call the former, which is how a library
  call flushes a buffer the runtime owns.
- history: Amended 2026-09-11 (T-083): buffering was unconditional, so a program on a terminal
  showed nothing until it exited and its `print` output arrived after its `eprint` output whatever
  the order in the source. Amended 2026-09-11 (T-088): the two names were the C runtime's
  `fort_rt_flush` and `fort_rt_flush_all`; the behaviour is unchanged and only the runtime moved
  (D13.1 as amended).

### D11.6 Process start
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: Process start: the compiler emits `main(argc, argv)` in the entry module (D9.7). That
  `main` calls `std.rt.args_init(argc, argv)`, which builds the `string@` of arguments from `argv`,
  then `std.rt.args()`, which writes that span into the frame of the emitted `main`. It then calls
  the entry module's `main` (D8.6): with the span when that `main` declares the parameter, and with
  no argument when it does not. On return, it flushes runtime buffers.
  It releases the runtime-owned argument headers before the final normal-exit boundary (D17.19).
  Argument bytes remain borrowed from C startup storage. The entry returns `status & 0xFF`.
  The compiler accounts for this generated sequence even when it lies outside source-function FIR.
  `std.rt.exit` performs the same runtime cleanup before foreign process termination.
  It does not execute caller defers or automatically delete user globals.
  The span goes by pointer, which is D9.9's internal convention for an aggregate parameter. The
  span in the frame of the emitted `main` is the caller-made copy of that convention, so no second
  copy exists. `toolchain.md` 5 fixes the runtime entry points and `toolchain.md` 6 the definition
  the compiler writes.
- history: Amended 2026-09-10: the prototype took a `const struct fort_slice*` and the failure was
  `fort_rt_fail_slice`, while spans were called slices (D3.5). Amended 2026-09-11 (T-088): the C
  runtime owned `main(argc, argv)` and called `fort_entry` from C, which made `fort_entry` the one
  compiler-emitted exception to D9.8's ban on aggregates at the C boundary. With the runtime in fort
  (D13.1 as amended) the compiler emits `main` itself, so that exception is gone and the only C ABI
  surface left in a program the compiler builds is `main`, which the C start-up code calls with C
  types.
  Amended 2026-09-28 (T-212): the compiler also emitted `fort_entry` beside `main`. The emitted
  `main` called `fort_entry(args)`, and `fort_entry` copied the span into its own frame when the
  entry module's `main` declared the parameter, then called that `main`. The T-088 amendment left
  `fort_entry` unchanged and gave no reason for it. The emitted `main` now calls the entry
  module's `main` itself and passes its own span, which D9.9 already makes the caller-made copy.
  Amended 2026-10-02 (T-277): Normal exit previously flushed without an argument-storage cleanup
  requirement. Final runtime cleanup now precedes the D17.19 obligation boundary.

### D11.7 How the print family writes a value
- owner: `memory-model.md` (Runtime errors), `toolchain.md` (Build modes, runtime).
- rule: Value formatting by the print family: integers in decimal; `bool` as `true`/`false`; `char`
  as its byte; `u8` as a number; enums as the member name, or the number if no member matches;
  pointers, `void*` and function pointers as `0x` plus lowercase hex (`0x0` for `null`); `string` as
  its bytes; floats as the shortest decimal that round-trips in the argument's own type (`f32` or
  `f64`), `%g`-style (exponent form below 1e-4 or at 1e17 and above, the exponent written as `e`, a
  sign, and at least two digits: `1e+21`, `1.5e-07`), with `.0` appended when the text has neither
  `.` nor `e`; `inf`, `-inf`, `nan`. No separators are inserted between arguments; each argument is
  evaluated and written in turn, left to right.

## D12 Builtins

### D12.1 Builtins that take a type
- owner: `core-language.md` (Builtins).
- rule: Keywords with type operands: `new(T)`, `new(T, n)`, `sizeof(T)`, `cast(e, T)`.

### D12.2 Universe-scope functions
- owner: `core-language.md` (Builtins).
- rule: Universe-scope functions with ordinary call syntax and special typing. They may be shadowed
  by a module-level or local declaration (D7.9) and cannot be used as values:
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
  default type (D4.5). Universe functions other than `move` yield no value: they are usable only as
  call statements (including as `defer` operands and in `for` init and step positions). `assert` is
  active in both build modes. Every runtime call a builtin makes is a call to a named function of
  `std.rt` (D13.1), an ordinary fort function reached by its mangled name (D9.7): the compiler holds
  the list of names, since fort has no attribute with which a module could mark a declaration as the
  target of a builtin, and `toolchain.md` 5.1 is that list. Not every builtin makes one. `move` is
  compiled where it stands, a read of the operand and a zeroing of it (D17.6), and `assert` calls
  only on the failing branch; `del` calls, and so do `panic` and every member of the print family.
- history: Amended 2026-09-11 (T-088): the calls named the C runtime's `fort_rt_*` entry points, the
  runtime being C (D13.1 as amended).

## D13 Standard library scope

### D13.1 The standard library and the runtime are fort
- owner: `stdlib.md`.
- rule: The standard library is written in fort on top of `extern` declarations, and the runtime is
  part of it: `std.rt` is an ordinary fort module, in every import closure (D9.10) and compiled into
  the program like any other (D19.1). It owns process start and exit (D11.6), allocation (D10.2,
  D10.3), the runtime-error and panic paths (D11.4, the ownership overwrite check of D17.11
  included), and the formatting and buffering of the print family (D11.5, D11.7, D12.2); it reaches
  the operating system through `std.libc`, with ordinary `extern` declarations (D9.8).
  `toolchain.md` 5 names its entry points and fixes what each one does.
- history: Amended 2026-09-11 (T-088): the runtime was `runtime/fort_rt.c`, compiled to
  `<std-dir>/fort_rt.o` and linked into every program, and this decision then called it permanent
  and not a self-hosting goal. The argument that reverses it: a builtin needs the compiler for one
  thing, the type-directed fan-out of one call into N typed calls, since fort has neither variadics
  nor generics; the *target* of each typed call can be an ordinary fort function, reached by its
  mangled name (D9.7) and compiled with the program by whole-program compilation (D9.10), exactly as
  `std.str.dup` already is. Go lowers `println` to `runtime.printint`, a Go package, and Rust lowers
  `v[i]` to `core::panicking::panic_bounds_check`, Rust code in `core`. Neither of the two things
  that kept the runtime in C applies to that target: nothing about it needs a C name and nothing
  needs a `.o`.

### D13.2 The v1 library modules
- owner: `stdlib.md`.
- rule: v1 modules: `std.sys` (exit, args, errno), `std.libc` (thin libc externs, named so that its
  short name does not collide with the common parameter name `c`), `std.mem` (copy, fill, equal),
  `std.io` (descriptors, read/write whole files and streams, close), `std.str` (compare, search,
  classify, parse integers, duplicate, NUL-terminated copies for C), `std.strbuf` (growable byte
  buffer), `std.vec` (`ptr_vec`, `int_vec`, the non-generic pattern), `std.strmap` (string-keyed
  open-addressing table), `std.math` (float bit casts, abs/min/max per type), `std.sort` (an
  in-place sort of an array over libc `qsort`), `std.net` (a TCP listener and a TCP connection over
  `std.libc`, IPv4 only and with no name resolution), `std.os` (the target triple and the path of
  the running binary) and `std.rt` (the runtime itself, D13.1:
  process start and exit, allocation, the failure paths, the print buffers and the float text of
  D18.1 to a descriptor or to a `str_buf`, over `std.libc` and `std.strbuf`). A C library
  function is declared in `std.libc` and nowhere else: the compiler, the runtime and the tests
  import it for `strerror`, `mkdtemp`, `fork`, `mkdir` and the like.
- history: Amended 2026-09-11 (T-087): the five `fort_rt_*` declarations stood in `std.libc`, whose
  header had to describe itself as libc "plus" the runtime; they moved to `std.rt`, so "thin libc
  externs" is true of `std.libc` without qualification. Amended 2026-09-11 (T-088): `std.rt` held
  `extern` declarations of the C runtime's entry points and nothing else; it is the runtime (D13.1
  as amended), so it declares no `fort_rt_` symbol and every module of this list is fort;
  `std.rt_float` joins the list with the float entry points D18.1 moves out of C. Note 2026-09-11
  (T-091): the migration window the note here described is closed. T-090 wrote the runtime in fort
  while the compiler still lowered every builtin to the C one, so `std.rt` held five `extern fn
  fort_rt_*` declarations for that window; T-091 retargeted the builtins, deleted the C runtime and
  those declarations, and "declares no `fort_rt_` symbol" is true of the module from that commit on
  (`bootstrap0/test/runtime_sig_test.c`, `std_rt_declares_no_runtime_c_symbol`). Amended 2026-09-13
  (T-045): `std.sort` joins the list. It is the first module this list gained after v1 was written,
  and `stdlib.md` 2.10 specifies it. Its element type must own nothing, because the sort permutes
  the elements inside C, where the ownership rules of D17 see nothing. Amended 2026-09-13 (T-107):
  `std.rt_float` held the two printers alone; it also holds the two buffer formatters of D18.1 as
  amended, and it imports `std.strbuf` for them. Amended 2026-09-14 (T-097): `std.net` joins the
  list and `stdlib.md` 2.13 specifies it. It resolves no name: `connect` reads a dotted quad,
  because `getaddrinfo` answers with a list the caller must free and no `extern` signature can
  carry that ownership across the boundary (D17, D9.8). Its seven `extern` declarations stand in
  `std.libc` with every other one, so a program that declares `socket` or `bind` itself must now
  match those signatures (D9.8).
  Amended 2026-09-14 (T-096): the entry for `std.rt_float` gave the reason for the split and no end
  for it; T-096 added the pointer to D18.1, which now carries the one condition that ends the split
  and the measurement of what the split costs today.
  Amended 2026-09-14 (T-131): the condition named in D18.1 arrived on that date, so the clause "for
  as long as the C bootstrap builds the compiler" in the `std.rt_float` entry is spent. The entry
  stays as it reads until T-132 folds the module into `std.rt`; D18.1's history note of the same
  date records that window.
  Amended 2026-09-14 (T-132): the window closed on the same date. `std.rt_float` leaves this list
  and its four functions are declarations of `std.rt`, which now imports `std.strbuf` for the two
  `append` functions. The list is one module shorter and `stdlib.md` 2.12 is a section of 2.11.
  The file `std/rt_float.ft` is gone, which took a pin of its own; D18.1's note of the same date
  says why. What the fold costs every program is measured in `notes/compiler.md` 7.
  Amended 2026-09-17: `std.os` joined the list and `stdlib.md` 2.14 specified it. Before this
  date the Linux sources of `std.libc` and `std.net` stood in `std/` and the Mac sources in
  `std/darwin/`. From that date `std.libc`, `std.net` and `std.os` had one source for each target,
  in `std/linux/` and `std/darwin/`.
  Amended 2026-09-25 (the user): every C library declaration moved into `std.libc`; until then
  the compiler, the runtime and the `test/fort` suites declared the functions they alone called.

### D13.3 The error-handling idiom
- owner: `stdlib.md`.
- rule: Error handling idiom (the earlier TBD): functions return `bool` or an error enum, with
  results delivered through `T mut*` out-parameters; `-1`/`null` sentinels where conventional;
  `panic` for programming errors; `defer` for cleanup. No `Result` type in v1.

### D13.4 The library at the C boundary
- owner: `stdlib.md`.
- rule: The stdlib never passes spans, strings or structs across an `extern` boundary; it unpacks
  `.ptr` and `.len`. Functions that hand a path to C copy it into a NUL-terminated buffer.
- history: Amended 2026-09-10: spans were called slices (D3.5).

### D13.5 Ownership in the library
- owner: `stdlib.md`.
- rule: Ownership in the library (D17): every function that allocates returns an `own` value
  (`str.dup`, `str.concat` and `strbuf.take` return `string own`, exact length, no NUL;
  `str.to_cstr` returns `char mut@ own` with a trailing NUL) or delivers it through an `own` slot
  (`io.read_file_bytes`); containers hold their storage as `own` fields (`str_buf { u8 mut@ own
  data; u64 len; }`, `ptr_vec`, `int_vec`, `str_map`) and expose a `free` function that `del`s them;
  out-parameters that receive ownership are pointers to `own` slots (`u8 mut@ own mut* out`), which
  the caller initializes to `{}` or `null`; `sys.args()` and every `view`-style accessor return
  borrowed values.

## D14 Toolchain and test conventions

### D14.1 The command line
- owner: `toolchain.md`.
- rule: `fort [options] entry.ft`. Options: `-o <file>` (default `a.out`), `-S` (stop after emitting
  `<entry>.ll`, D19.1), `-c` (stop after the object file), `-I <dir>` (repeatable), `--std-dir
  <dir>` (default `$FORT_STD_DIR`, else `std` beside the binary), `--release` (D11.1),
  `--no-bounds-check` (D10.6), `-l<lib>` (passed to the linker), `--cc <path>` (default `clang`
  on both targets; it must be a clang that compiles LLVM IR),
  `--target <triple>` (default built target,
  passed to `--cc` as `--target=<triple>`), `-Xcc <arg>` (repeatable, passed to `--cc` verbatim
  after the compiler's own arguments), `--cfg <list>` (D21.1, repeatable), `--check` (D20.1),
  `--json` (D20.2, only with `--check`),
  `--index` (D20.3, which implies `--check --json`), `--tokens` (lex the entry file alone and write
  its tokens to stdout; it resolves no import and parses nothing, and combining it with `--check`,
  `--json` or `--index` is a usage error), `--ast` (lex and parse the entry file alone and write its
  syntax tree to stdout as one S-expression; it resolves no import and checks nothing, and combining
  it with `--tokens`, `--check`, `--json` or `--index` is a usage error), `--fir` (check the
  closure of a program and write its FIR to stdout in the textual form of `spec/fir.md` 13, then
  stop before the emitter; it writes no file and takes the target rule of `--check`, and combining
  it with `--tokens`, `--ast`, `--check`, `--json` or `--index` is a usage error),
  `--fir-after=<pass>` (implies `--fir` and writes the FIR at the named point of `spec/fir.md`
  11; the names are `lower`, the output of the lowering, and `build-mode`, the output of the
  build-mode pass for the mode that `--release` and `--no-bounds-check` select, and any other
  name is a usage error), `--fir-test` (read the entry file as a FIR test of `spec/fir.md`
  16.3: check its prelude as the module `main`, parse its FIR text against that module, run the
  passes that its `//! pass:` directives name, in their order, and write the module to stdout in
  the textual form of `spec/fir.md` 13; a pass that the compiler does not run is an error with
  status 2, and combining it with `--tokens`, `--ast`, `--check`, `--json`, `--index`, `--fir` or
  `--fir-after` is a usage error), `--fir-verify-report` (implies `--fir`, and writes in place of
  the FIR one line to stdout for each function that breaks a rule of `spec/fir.md` 10, its first
  violation after the lowering or after the build-mode pass of the selected mode, then exits 0;
  `--fir-after` does not change the report), `--fir-stats` (after a build writes its module,
  write the line `lowered N of M` to stdout: N functions that the translator wrote of M
  definitions of fort functions, and N equals M, because a function that the translator does
  not write is a compile error, `spec/fir.md` 9.8; combining it with `--tokens`, `--ast`,
  `--check`, `--index`, `--fir` or `--fir-test` is a usage error), `--help`, `--version`.
  Exit status: 0 success, 1 compile error, 2 usage, toolchain (`--cc` failed) or internal error;
  usage and toolchain errors are printed as `fort: error: <message>`.
  A Linux x86-64 compiler stores `x86_64-linux-gnu` as its built target.
  A Mac arm64 compiler stores `arm64-apple-macosx11.0.0` as its built target. macOS 11.0 is the
  first macOS for Apple Silicon, and the build does not read the host version.
  The built target is `std.os.TARGET` of the standard root that the compiler is built with.
  The driver uses the stored built target when `--target` is absent.
  The compiler accepts `x86_64-linux-gnu` and `arm64-apple-macosxM.m.p` target forms.
  In IR modes, it rejects any other target form as a usage error with status 2.
  `-S --target` may select the other target and must emit IR for that selected target.
  A non-built target with `-S` requires an explicit `--std-dir` option before IR emission.
  When `-S` and `-c` occur together, `-S` wins and permits that cross-target IR output.
  The caller must supply standard sources that match the selected target's C ABI and constants.
  The compiler checks that the option is present; it does not verify the sources' C ABI.
  Without `-S`, `-c` and linking require the selected target to equal the built target.
  They reject a different target with usage status 2 before IR or object output.
  The Mac compiler gets its running binary path from `_NSGetExecutablePath`.
  It uses `realpath` when that call succeeds. It uses the returned path if `realpath` fails.
  It then looks for `std` beside the binary. Linux uses `/proc/self/exe` for that lookup.
  `FORT_STD_DIR` and an explicit `--std-dir` keep their existing precedence.
- history: Amended 2026-09-10 with D19: `-S` emitted `<entry>.s`, `--cc` defaulted to `cc`, and
  `--target` and `-Xcc` did not exist. Amended 2026-09-10 with D20: `--check`, `--json` and
  `--index` did not exist. Amended 2026-09-11: `--tokens` did not exist. It is the observation point
  the self-hosted lexer is verified at, the two compilers' dumps being compared file by file over
  the whole repository, and the only thing stage2 can do before it has a parser. Amended 2026-09-11:
  `--ast` did not exist. It is the same observation point one pass later, the two compilers' syntax
  trees being compared file by file over the whole repository, and it is what a compiler with a
  parser and no checker can do.
  Amended 2026-09-15 (T-140): defaults now follow the binary's built target.
  Cross-target `-S` now uses a caller-selected standard root. Mac finds its binary through
  `_NSGetExecutablePath`.
  Amended 2026-09-15 (T-140): a two-component host version now gets a zero patch part.
  Amended 2026-09-16 (T-156): `--cfg` did not exist.
  Amended 2026-09-17: the Mac built target became the fixed `arm64-apple-macosx11.0.0`. Before
  this date the build read the host version with `sw_vers -productVersion`. On the same date the
  built target moved from the compiler's `platform` module to `std.os.TARGET` (D13.2), and the
  default `--cc` became `clang` on both targets (D14.3).
  Amended 2026-09-29 (T-221): `--fir` and `--fir-after=<pass>` did not exist. They are the
  observation point of FIR (`spec/fir.md` 17.1, open question 1), as `--ast` is of the parser.
  `--fir-after` names only the two points where FIR changes. `verify` is not a name, because
  the verifier changes nothing and runs twice (T-221 review, finding 3).
  Amended 2026-09-29 (T-224): `--fir-test` did not exist. It is the driver of the harness of
  `test/fir/` (`spec/fir.md` 16.3), as `--check` is the driver of the check mode.
  Amended 2026-09-30 (T-247): `--fir-after=build-mode` wrote the module that `--fir` writes, and
  `--fir-test` ran the one pass of its directive and refused `build-mode` with status 2. The
  build-mode pass exists now, and a test of it names the pass and then `verify`.
  Amended 2026-09-30 (T-253): `--fir-verify-report` and `--fir-stats` did not exist. They are the
  measurements of the migration (`spec/fir.md` 16.1, 16.5): the report runs rule V9 over a
  corpus without a panic, and the count gives the share of functions that the FIR path writes.
  Amended 2026-10-01 (T-255): `--fir-stats` counted "N functions that the FIR path wrote", and
  N was less than M when the direct path wrote a function. T-255 deleted the direct path, so a
  module that a build writes has N equal to M; the flag stays for `tools/ir_snapshot.sh`.

### D14.2 Diagnostics and recovery
- owner: `toolchain.md`.
- rule: Diagnostics go to stderr. Each diagnostic starts with a header line,
  `<file>:<line>:<col>: error: <message>`, and `note:` header lines can follow it. Errors without
  a position in the file (a missing `main`) use `1:1`. Under each header line the compiler writes
  as many rendered lines as the rendering needs, and each rendered line starts with a space. When
  the file path does not start with a space, the first byte of a line tells a header line from a
  rendered line. The rendered lines show the source line where the range of the diagnostic starts
  (D20.4), then an underline: `^` at the start column and `~` up to the end column, exclusive. A
  range that ends on a later line is underlined at least to the end of its first line, and its
  later lines need not be shown. The empty range at 1:1 and a file the compiler cannot read show
  no rendered line. A
  lexical error is reported and lexing resumes at the start of the next line, dropping the line it
  stands on with the tokens already lexed on it, so a file reports at most one lexical diagnostic
  per line and its token stream still covers the rest of the file and ends at the end of it; the
  file is parsed from those tokens, so an editor keeps the declarations after a half-typed literal,
  and the parser recovers from whatever construct the missing line broke. A file with a lexical
  error is not checked. After a syntax error the parser reports it, skips to the next statement,
  clause, field or declaration boundary and parses on, so a file reports one diagnostic for each
  construct that failed and not only for the first: at most 20 per file counted across its lexical
  and its syntax errors together, never two in a row at one position, and none for a construct whose
  tokens an earlier skip had already dropped. The skipped tokens are held in the tree as an error
  node that every later pass skips. A file with a syntax error is parsed whole and not checked.
  Every module of the closure is checked in dependency order; a declaration whose check failed has
  the error type, which silences every later diagnostic involving it, so an importer sees only its
  own errors. Positions of errors that concern a whole construct: "missing return" and a
  non-exhaustive enum `switch` are reported at the closing brace of the body or `switch`; an
  infinite-size struct at its `struct` keyword; a shadowing error at the inner declaration ("'n'
  shadows a parameter", "'n' shadows an enclosing local"). Those two are one case of a wider rule.
  A diagnostic about two declarations stands at the declaration in the module the compiler is
  checking; within one module it stands at the later of the two by position. Most such diagnostics
  report that one place and say nothing about the other: the two shadowing messages above do, and
  so do "'n' is already declared in this block", "duplicate parameter 'n'", "duplicate field 'n'"
  and "duplicate enum value N for 'n'". A diagnostic that reports the other declaration as well
  makes that second report its note. Every module of the closure is checked in dependency order
  (D9.10), which puts an imported module before its importer and leaves two modules that neither
  imports unordered. The error stands in the module being checked and not in the later of the two,
  so an unordered pair is settled as well: the module checked second carries the error. The note
  can therefore name a module the reader did not write, the standard library above all. The
  reason is the note's place. A nested note carries no severity of its own and goes with the
  error it follows (D20.2), and a client that cannot open a file may drop that file's diagnostics
  (`toolchain.md` 9.2). An error in a file the reader cannot open takes the note in the reader's
  own file with it and leaves the reader nothing. An error in the file the reader wrote survives
  the loss of its note, because the error says what is wrong. A diagnostic about anything but two
  declarations stands where the construct it names stands, in whatever file that is.
  The compiler never emits warnings in v1.
- history: Amended 2026-09-14 (T-112): which of two declarations carried the error and which
  carried the note was implemented and never decided, so no rule told a new diagnostic of that
  shape where to stand. Amended 2026-09-10 with D20: a syntax error stopped the file after one
  diagnostic with no recovery, and only the semantic errors of the first module that had any were
  reported. Amended 2026-09-10 with T-062: a lexical error stopped the compilation of the file
  after one diagnostic, the file was never parsed, and the cap of 20 counted syntax errors alone.
  Amended 2026-09-27 (T-191): a diagnostic was its header line alone, "one per line", and no line
  showed the source text or the range. The user accepted rendered lines under each header line,
  with no limit on their count, on 2026-09-25.

### D14.3 The --cc command line
- owner: `toolchain.md`.
- rule: Generated code is LLVM IR (D19.1), compiled and linked by `--cc` in one invocation, `<cc>
  --target=<triple> -O1 -fPIE -Wno-override-module -o <out> <entry>.ll [-l<lib>...] [<-Xcc
  args>...]`, `-O2` in place of `-O1` under `--release` and `-c` before `-o` when the compiler stops
  at the object; the executable is position-independent. The module holds the whole program, the
  runtime included (D9.10, D13.1), so the only inputs the line names are that module and the
  libraries the program asked for.
  Linux x86-64 and Mac arm64 use the same line, each with its own triple.
  `-c` removes the link libraries. The line passes no `-pie` and no `-Wl,-pie`: clang links a
  position-independent executable by default on both targets.
  The default `--cc` is `clang` on both targets.
  Neither target links an object from a fort C runtime (D13.1).
- history: Amended 2026-09-10 with D19: generated code was GNU assembly, assembled and linked by the
  system C compiler. Amended 2026-09-11 (T-088): the line also named `<std-dir>/fort_rt.o`, the C
  runtime object (D13.1 as amended).
  Amended 2026-09-15 (T-140): Mac uses Apple clang and the Mach-O PIE linker flag.
  Amended 2026-09-17: the line dropped `-pie` and `-Wl,-pie`, because clang 18.1.3 for
  `x86_64-linux-gnu` and Apple clang 21 for arm64 both link a position-independent executable
  without them. The default `--cc` became `clang` on both targets. Before this date the Mac
  default was `/usr/bin/clang`. From that date the driver had no branch on the target.

### D14.4 Where the language tests live
- owner: `toolchain.md`.
- rule: Language tests live under `test/lang/`: `run/<area>/NNN_name.ft` (compile, run, compare),
  `fail/<area>/NNN_name.ft` (must not compile), where `<area>` is one of `lexical constants
  operators casts mutability ownership declarations control switch defer functions structs enums
  arrays spans strings pointers globals builtins errors modes modules ffi stdlib`, plus
  `run/modules/<name>/main.ft` and `fail/modules/<name>/main.ft` for multi-file tests (the harness
  compiles `main.ft` with the directory as root and collects `error` annotations from every `.ft`
  file in it), `ffi/*.c` helpers, `programs/*.ft` for larger programs. The harness invokes `fort` on
  the test file itself (`main.ft` for multi-file tests) with `test/lang` as the working directory,
  so `<file>` in diagnostics and runtime errors is the path relative to `test/lang`. Directory tests
  exist only under `modules`. Verdicts: PASS, FAIL (the program or compiler misbehaved), ERROR (the
  compiler exited 2, crashed, timed out, or the harness could not link or start the program), XFAIL
  and XPASS for tests listed in `xfail.txt` (an ERROR on a listed test is XFAIL too). Compiler unit
  tests in C live in `bootstrap0/test/` using `test.h`.

### D14.5 Test file directives
- owner: `toolchain.md`.
- rule: Test file directives, all at the top of the file (`//!`) except `error`; in a multi-file
  test only `main.ft` carries directives and sibling modules carry none:
  - `//! run` or `//! fail` (required, first line);
  - `//! flags: --release` (extra compiler flags);
  - `//! args: a b c`; `//! link: ffi/helpers.c` (repeatable, relative to `test/lang`);
  - `//! stdin:` followed by `//< ` lines;
  - `//! stdout:` followed by `//| ` lines: the expected output is each line's text after
    `//| ` followed by a newline (a bare `//|` is an empty line); compared exactly, trailing
    spaces included; output without a final newline cannot be expressed, use `println`;
  - `//! exit: N` (default 0), `//! abort` (expect SIGABRT) or `//! signal: NAME` (expect that
    signal; the names are in `toolchain.md` 7.3);
  - `//! stderr: <substring>` (repeatable; each must appear in stderr); `flags`, `args`,
    `exit`, `abort`, `signal`, `stdin` and `stdout` appear at most once, `link`, `stderr` and
    `error-any` any number of times; a `fail` test carries at least one `error` or `error-any`;
  - `stdout`, `exit`, `abort`, `signal` and `stderr` each have a target form with the suffix
    `-<os>`, where `<os>` is a `$cfg(target_os)` value (`linux` or `macos`): `//! stdout-macos:`,
    `//! signal-macos: TRAP`, `//! exit-linux: 3`. The harness derives the OS from its `--target`
    triple. When the OS matches, the target form replaces the plain one: `stdout-<os>` replaces
    `stdout`, `stderr-<os>` replaces every `stderr`, and `exit-<os>`, `abort-<os>` or
    `signal-<os>` replaces the outcome. The plain form is the expectation of every other OS. A
    target form without its plain form (for an outcome, any of `exit`, `abort` and `signal`) is
    a lint error, and the rules of a plain directive hold for its target form: the three
    outcome forms of one OS are mutually exclusive, and only `stderr-<os>` repeats;
  - in `fail` tests, `//! error: <substring>` at the end of the offending line; every such line
    must produce a diagnostic on that line containing the substring, and no unannotated
    diagnostic may occur; `//! error-any: <substring>` at the top for errors without a useful
    line (for example circular imports).
  - every `fail` test has a golden, a file that holds the compiler's whole stderr for that test:
    `<stem>.stderr` beside `<stem>.ft`, and `expected.stderr` in the directory of a directory
    test. The stderr must equal the golden byte for byte after two normalizations: the
    `--std-dir` prefix of a path becomes `<std>`, and the absolute path of the corpus root
    becomes `<root>`. A missing golden fails the test. A golden without a `fail` test beside it
    is a lint error.
- history: Amended 2026-09-25: the `-<os>` target forms did not exist. Until then a run test
  whose outcome differs between the two targets ran on Linux alone, by an exclusion list in the
  darwin gate (4 fixtures: the trap signal, the errno numbers, the `sockaddr_in` family field
  and the import `$cfg(target_os)` selects). The same amendment lists `signal:` here; before it
  `toolchain.md` 7.2 alone named it.
  Amended 2026-09-27 (T-191): a `fail` test had no golden, and nothing held the compiler's
  stderr beyond the `error`, `error-any` and `stderr` substrings.

### D14.6 The test-to-source ratio
- owner: `toolchain.md`.
- rule: Coverage target: **more than two lines of test for each line of source** the
  project writes and ships -- the compiler (`bootstrap0/src/*.c`, `*.h`, `src/fort/*.ft`) and the
  standard library (`std/*.ft`), the runtime included, since it is `std.rt` -- measured against
  `bootstrap0/test/*.c`, `bootstrap0/test/common/*.h`, `test/**/*.ft` and `test/lang/ffi/*.c` by
  `agents/lines.py`. The implementation plan will size the corpus; the seed tests written in this
  phase establish the format and one example per feature area.
- history: Amended 2026-09-11 (T-076): until then the target was "three lines of test for each line
  of compiler", with the runtime and the standard library excluded from both sides, which credited
  library and runtime code with tests it did not have -- `test/runtime_test.c` counted 850 lines
  into the numerator while the 851 lines it tests counted nowhere, and a ticket writing `std/*.ft`
  met the ratio without the ratio seeing its code. The editor extension (`editors/`) still counts on
  neither side, that being a separate question about non-compiler code. Amended 2026-09-11 (T-088):
  the runtime is `std.rt` now (D13.1 as amended) and counts with `std/*.ft`; `runtime/*.c` and `*.h`
  stayed on the source side while those files existed. Amended 2026-09-12 (T-091): they are deleted
  and `agents/lines.py` globs them no longer, so the source side is the compiler and the standard
  library and nothing else. Amended 2026-09-14 (T-123, the user): until then the target was
  "about three lines of test for each line of source", and the corpus had never met it. Measured
  on 865a15c: 2.19, from 52963 source lines against 115824 test lines, so 3.0 needed about 43000
  further test lines. The user refused that backfill and set the target above 2.0, which the
  corpus meets today. The number changed and the rule that a ticket's own diff carries its
  tests did not: `agents/lines.py --since main --min 3.0` stays an acceptance criterion, because a
  per-branch minimum above the corpus target is what holds the corpus above its floor. Amended
  2026-09-25 (the user): the tool is `agents/lines.py`, in the gitignored directory of the process
  tooling, and no ctest runs it; until then it was `tools/lines.py` with the ctest `lines_selftest`.

### D14.7 The C-started target bootstrap chain
- owner: `toolchain.md` (8).
- rule: The production bootstrap detects and accepts exactly two host operating systems. Linux
  selects `linux` and `x86_64-linux-gnu`. Darwin selects `darwin` and
  `arm64-apple-macosx11.0.0`. The build rejects cross compilation and all other hosts.
  CMake reads `tools/bootstrap.ref` and owns the complete build graph. The file contains an ordered,
  gap-free list from `bootstrap-1`. Each row names one lowercase, full commit SHA. Each commit is in
  HEAD's history and contains the compiler and standard sources for both supported hosts. A shallow
  clone that lacks one listed commit cannot bootstrap. The C compiler builds only `bootstrap-1` for
  the detected host. Each listed source compiler builds the next listed revision for that host. The
  last listed compiler builds working-tree HEAD. The graph does not call a shell script to read the
  list or select a predecessor. CMake applies the configured checked or release mode to each source
  stage.
  `FORT_STAGE1_COMPILER` can name an external compiler. This mode skips the list and builds HEAD
  directly. `FORT_ENABLE_BOOTSTRAP0=OFF` requires that external compiler. Bootstrap mode rejects
  an external compiler. External-stage1 mode builds no C compiler target and registers no C
  compiler test. A successful native build proves the bootstrap edge.
  Add a pin only when the current last pin cannot build a required later revision. A new pin must
  build with its predecessor and build its successor. Both builds must pass on both supported host
  systems before the pin enters the list.
  The project does not maintain C-to-fort parity.
- rationale: One C compiler can start the same source chain on both supported systems. CMake records
  all dependencies and rebuilds only the affected stages.
- history: Amended 2026-09-24: the C compiler became bootstrap-0, and the list starts at
  `bootstrap-1`. Stage1 became the name of the compiler that builds HEAD. Before this date the C
  compiler was stage1 and the first listed revision was bootstrap-0.
  Amended 2026-09-17 (T-160). The two exclusive modes replace the C parity role. The
  native build proves the C-to-bootstrap-0 edge. External-stage1 mode contains no C compiler test.
  Amended 2026-09-17 (T-159). Removed the supplied-seed workflow after CMake took control
  of the complete graph. Amended 2026-09-17 (T-155). The C compiler replaces the user-supplied
  executable seed.
  Decided 2026-09-16 (T-150). The earlier chain used a supplied target seed from one source
  baseline.

## D15 Not in v1

Deferred deliberately. The specification lists each with the idiom to use instead.

Generics; untagged unions (idiom: a fat struct with a kind field); tagged unions and `Result`
(idiom: D13.3); methods; closures and nested functions; variadic fort function definitions;
variadic function-pointer types; overloading; default
and named arguments; visibility modifiers; type aliases; integer-range `for`; struct, array and
span equality; definite-assignment analysis (idiom: initialize with `{}` or a sentinel);
alignment and packed attributes (idiom: an opaque `u8[N]` field and a C shim); separate
compilation and interface files; conditional compilation; labeled `break` (idiom: a flag or a
helper function); raw strings; a blank identifier; compile-time function evaluation; `alignof`;
`sizeof(expr)`; array suffixes after a trailing pointer suffix (`i32[4]*[2]`, idiom: a struct);
string `switch`; `goto` (never). Amended 2026-09-10: spans were called slices (D3.5).
Amended 2026-10-01 (T-272): removed compile-time ownership proof from the deferred features.
D17.14 and D19.8 define that proof and its staged delivery.
A stricter rule that forbids current zero-value inspections after `move` or `del` remains deferred.
Note 2026-10-03: The deferred stricter rule is the recorded branch ruling of 2026-09-18:
"every read of an emptied local is an error".
Branch `feat/local-linear-check` records that ruling at `77bb6a41`.
Main never adopted it. D17.9 keeps zero-value inspections legal.
The proof adds no general resource type system for file descriptors, sockets, or scalar handles.

Deferred at the C boundary (2026-09-11, T-025): an extern link name or alias.
An extern still has no link name of its own; its declared name is its C symbol (D9.8).
Two declarations of one symbol must agree on their fixed prefix and variable-tail mark.
Amended 2026-09-15 (T-140): a C extern may now declare `...` after its fixed prefix.
Calls to that declaration may use different tail counts and types.
The compiler rejects tail types that require C default promotions.
The caller writes the explicit promotion cast. The compiler does not check C format strings.

Deferred in the runtime (2026-09-11, T-088): a flag that leaves `std.rt` out of the import
closure, for a program that carries a runtime of its own or targets an environment with none.
Every closure holds it (D9.10, D13.1) and there is no way to ask for a program without it; a
program that must not link one is outside v1.

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
- The module path separator and the member accessor are the same character on purpose (D9.1,
  D9.4), so `import std.io;` reads like a field access and nothing tells a module path from a
  qualified name by shape alone -- the dot is also the field accessor, the enum-member accessor
  (D3.9) and the qualified-name separator. Nothing has to tell them apart: an import path is its
  own grammar production and appears in no other construct, so there is nothing to parse either
  way, and the gain is one separator instead of two and a mangler that translates nothing (D9.7).
  The cost is paid in the diagnostics instead: a path whose separator is spelled `::` or `..` is
  named where it stands rather than reported as the `;` that ending the path early leaves missing
  (module-system.md 13). Weighed and rejected with it on 2026-09-11: dropping the separator before
  the brace of the grouped form, and leaving that one form on `::`.
- `extern` signatures exclude aggregates so the compiler does not need target C aggregate
  classification in v1.
- Generated code must be position-independent: `--cc` uses target PIE flags (D14.3) and
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

Added after the v1 design review at the user's request; wherever an earlier decision or document
says ownership is "by convention", this section supersedes it.

### D17.1 The own qualifier
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: `own` is a qualifier on reference types: `T* own`, `void* own`, `T@ own`, `string own`. It
  states that the reference designates the start of a live allocation obtained from `new` (or
  answered by an `extern` whose signature says `own`, D17.13) and that `del` on it is meaningful.
  It is erased at run time (same bits, layout and ABI) and is part of type identity: `node* own`
  and `node*` are different types, as are `fn (node* own) void` and `fn (node*) void`. `own` on a
  non-reference type (`i32 own`, `point own`, `i32[4] own`) or on a function-pointer type is an
  error; an array or struct that *contains* an `own` reference is an owning aggregate (D17.7).
- history: Amended 2026-09-29 (T-257): the allocation was "obtained from `new` (or adopted with
  `cast`, D17.3)". A cast no longer adds `own` (D3.14).

### D17.2 Where an own marker goes
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Placement. An `own` follows a `*` or an `@` and marks the reference that suffix introduces
  as owning its target; `string`, a reference without a suffix (D3.7), takes it directly (`string
  own name`). It precedes `mut` in the position (`node* own mut p`, D5.3), never follows a
  non-reference base type or a fixed-array suffix (`node own*` and `node*[4] own` are errors; `node*
  own[4] t` is four owning pointers), and nothing precedes the base type, so every type has one
  spelling. The outermost reference is the one the binding holds, so the `own` before the name says
  the binding owns what it refers to: `node* own p` and `u8@ own buf` are what `del(p)` and
  `del(buf)` require (D17.9). Each `own` marks one reference only: the safe failure mode for `node*
  mut@ own kids` is that `del(kids[i])` does not compile when the nodes belong to someone else (an
  arena, say).

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

- history: Amended 2026-09-10 with D5.3: until then an `own` before the base type marked the
  outermost reference and `node* own p` was an error. Amended 2026-09-10, separately: spans were
  called slices (D3.5).

### D17.3 What produces an owning value
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Producers. `new(T)` yields `T mut* own` and `new(T, n)` yields `T mut@ own`, for every `T`
  that `new` accepts, which is D10.2's rule and not a second one: no `mut` in the outermost position
  of `T`, an `own` only after a `*`, and `T` itself neither `void` nor a span. `new(u8[4], n)` is
  `u8[4] mut@ own` and `new(node mut* own, n)` is `node mut* own mut@ own` whose slots are null. The
  result is always `own`, and `new` marks the storage it allocates writable (D5.8). That storage is
  the outermost position of `T`, which is why `T` may not spell a `mut` there: `new` supplies it, so
  there is one spelling for each type. Below it `T` says what it means, and the element type of the
  result is the element type written -- `new(node* own, n)` is a `node* own mut@ own`, `n` owned
  slots whose nodes this span cannot write. `new(void*)` is legal
  and yields `void* mut* own`, one pointer slot, since a pointer to `void` has a size (D3.11); it is
  `new(void)` that is not
  legal, and D10.2 rejects that one. Standard-library functions that allocate
  return `own` (D13.5). An `extern` whose result says `own` produces an owning value (D17.13). A
  `cast` carries `own` or drops it, and the target type of a cast decides (D3.14). A cast never
  adds `own`. Its result owns only what its operand owned. Span
  expressions (`buf[..]`, `buf[lo..hi]`) and `.ptr` always yield views, as do `&`, literals and the
  runtime's `args`.
- history: Amended 2026-09-14 (T-085): the adoption clause called itself "the same unsafe escape
  as adding `mut`" and gave a `void*` as the source of its example. D3.14 no longer lets a cast add
  `mut`, so adoption is the only unsafe mark a cast adds and the example says `void mut*`.
  Amended 2026-09-14 (T-135): `new` marked every position of `T` writable, so `new(node*,
  n)` was a `node mut* mut@ own` and `T` could spell no `mut` at all. `new(void*)` reads `void*
  mut* own` again, as it did before T-086, and the type T-086 gave it is now spelled `new(void
  mut*)`. Amended 2026-09-14 (T-086): `new(void*)` yielded `void* mut* own`, because the `void`
  base took no mark while `void mut*` was not a type. Amended 2026-09-10: the count moved out of
  the type, `new(T[n])` to `new(T, n)`. Amended
  2026-09-10, separately: the restriction read "not itself `own`, `mut` or a reference to `void`",
  which contradicted the `new(node* own, n)` in its own next clause, D10.2's parse rule, and `void*
  own` (D17.1); it was a second statement of D10.2's rule that had drifted from it, so it now cites
  it instead of restating it.
  Amended 2026-09-29 (T-257): the rule let `cast` add `own` to a pointer or span, "adopting memory
  that came from C (`cast(p, u8 mut* own)` for a `void mut*` from an extern that does not say
  `own`)", and called adoption "the one unsafe mark a cast adds". D3.14 now refuses that cast.

### D17.4 Lending
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Lending. `own X` converts implicitly to `X` wherever a value meets an expected type, like
  dropping `mut` (D5.4); the two drops combine (`u8 mut@ own` to `u8@`). Dropping `own` at a level
  `k` is allowed only if every level between 1 and `k - 1` is immutable in the target (the D5.4
  shape: otherwise `node* mut@ w = kids; w[0] = &local;` would let `del(kids[0])` free a stack
  address) and only if no outer level keeps `own` (`node mut* own mut@ own` to `node mut* mut@ own`
  is an error, since the inner objects would then be owned by nobody); lend the whole thing instead.
  Operands of `==`, `!=` and `?:` lend, so an `own` **lvalue** never blocks a comparison; an `own`
  **rvalue** operand is the error of D17.8 instead, `make() == null` above all, since lending it
  would leave nothing able to free it. `?:` is the exception: it yields `own` when both operands are
  `own` rvalues or `null` (D6.2), so the temporary lands wherever the conditional's value lands.
  Bootstrap-0 does not implement `?:` at all (`toolchain.md` 7.3), so that clause is carried by the
  self-hosted compiler alone: T-044 added it, with the `fail` test for
  `use(flag ? new(node) : new(node))` (`test/lang/fail/ownership/037_ternary_own_arms.ft`).
  Lending copies the source relation without transferring allocation ownership.
  Mutable aliases remain legal under the existing mutability rules.
  The proof checks their ordered ownership effects; it does not require exclusive borrowing.
- history: Amended 2026-09-11: the comparison clause read "so `own` never blocks a comparison"
  without distinguishing an lvalue from an rvalue, which D17.8's "anything else is a compile error"
  contradicts for the rvalue (T-022). Amended 2026-09-12 (T-105): the stage1 clause cited `(D3.10)`
  for what stage1 does not implement. D3.10 decides function types and function pointers, and it
  states that function pointers are inside the bootstrap's subset; it lists nothing stage1 lacks.
  The list of the four families the C bootstrap lacks, `?:` among them, is `toolchain.md` 7.3, and
  the clause names that section now. Note 2026-09-13 (T-122): the sentence above about D3.10
  describes the log as it stood on 2026-09-13. On that date D3.10 still carried the title
  "Function types and function pointers". It still put function pointers inside the C bootstrap's
  subset, and it still listed nothing stage1 lacks. So the note above needed a date and no
  correction. Amended 2026-09-24: "Stage1" in the rule became "Bootstrap-0", the new name of the
  C compiler. The notes above use the old name.
  Amended 2026-10-01 (T-272): Lending gained source relations and temporal proof obligations without
  exclusive borrowing.

### D17.5 Transfer
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Transfer. Copying an `own` **lvalue** into an `own` place (a declaration's initializer, an
  assignment, an `own` parameter, an `own` element or field of a literal, a `return` operand that is
  not a local) requires `move(lv)`. An `own` **rvalue** (`new(...)`, a call result, `move(...)`,
  `cast(...)` to an `own` type) flows into an `own` place without it. `return x` where `x` is a
  local variable or parameter of `own` type is an implicit `move`.
  Transfer preserves allocation identity independently of the owning place.
  Existing views into that live heap allocation remain valid after the transfer.
  A view into owner-slot storage instead observes the slot's changed contents.
- history: Amended 2026-10-01 (T-272): Transfer gained allocation-source preservation obligations.

### D17.6 The move builtin
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: `move(lv)` is a universe function (D12.2). Its operand is an lvalue of owning type (an `own`
  reference or an owning aggregate, D17.7); it yields the operand's value and sets the operand to
  its zero value (`null`, `{null, 0}`). Emptying is not an assignment: the binding need not be
  `mut`, and after the move it still cannot be assigned to unless it is. When the operand is reached
  through an indirection (`*p`, `p->f`, `s[i]`), that level must be mutable, because the move
  changes storage that others can see: `move(v[0])` on a `node* own@ v` is an error, since nothing
  may be taken out of what was only lent. Fields and elements of a local value count as the local.
  Moving a zero value yields a zero value. `move` and `del` of a module-level constant (D7.10) are
  errors: it lives in read-only memory.
  A whole aggregate move transfers its complete value, including borrowed and scalar fields.
  It leaves the source aggregate at its zero value. A projected move empties only its selected
  place.
  A move does not rebase borrowed addresses into inline storage.
  The proof tracks transfer separately from generated clearing of fixed temporary storage.
- history: Amended 2026-10-01 (T-272): The proof distinguishes heap sources, owner slots, and
  aggregate transfer state.

### D17.7 Owning aggregates
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Owning aggregates. A struct or fixed array that contains an `own` reference by value
  (directly or through nested aggregates) is owning. Copying an owning lvalue into an owning place
  (initialization, assignment, a by-value parameter, a literal element) requires `move`; returning a
  local owning value is an implicit move. Functions therefore take `vec*` or `vec mut*`. `del` of an
  aggregate is an error: `del` is shallow, and a struct frees its own fields.
  The proof checks each owning leaf, including leaves within nested aggregates.
  Release or transfer owned descendants before deleting their containing allocation.
  Aggregate replacement must not discard live ownership obligations (D17.11, D17.14).
- history: Amended 2026-10-01 (T-272): Owning aggregates gained residual-descendant and overwrite
  proof obligations. Del stays shallow.

### D17.8 An owning temporary must land
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Temporaries must land. An `own` rvalue may only be bound to an `own` place, passed to an
  `own` parameter, or `del`ed. Anything else is a compile error ("owning temporary would leak"),
  because nothing could ever `del` it: converting or casting it to a non-`own` type (`node mut* n =
  new(node);`, `use(str.dup(x))`, `cast(new(node), node*)`), taking a span of it, indexing or taking
  `.ptr` of it (`new(u8, 8)[..4]`, `new(i32, 2)[0]`), accessing a field of an owning aggregate
  rvalue, and discarding it as an expression statement (`move(x);`, `str.dup(s);`).
  The proof preserves these landing rules. It tracks temporary storage to its source-defined end.
  An owning temporary supplies no exception for borrowing or retention.
- history: Amended 2026-10-01 (T-272): Temporary landing remains required under the ownership proof.

### D17.9 What del requires
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: `del(x)` requires an `own` operand of any mutability: an `own` pointer, `void* own`, `own`
  span or `string own`, as an lvalue or an rvalue. On an lvalue, `del` empties the operand under the
  rules of D17.6, with the same mutability requirement through indirections; on an rvalue it only
  frees. `del(null)` (the literal adopts `void* own`) and `del` of a zero span or string are no-ops,
  so `del(buf); del(buf);` frees once. Reading an emptied owner's zero value remains legal.
  Null comparisons and zero span-length inspections remain legal after `del` or `move`.
  The proof rejects a dereference through a proved empty owner. `del` of
  a view, a sub-span, a `.ptr`, a stack address or a literal is a compile error, because none of
  them has an `own` type. This supersedes the earlier "del does not null its argument".
  A zero-element allocation still has an allocation identity and a release or transfer obligation.
  Its zero length does not make it an empty owner. Del requires valid ownership of its allocation.
  Release invalidates borrows of that allocation. Refilling an owner place does not revive them.
- history: Amended 2026-10-01 (T-272): The proof preserves empty del and zero inspections. It tracks
  allocation identity apart from length.

### D17.10 Ownership in a range loop
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: A range `for` lends its collection storage.
  An owning span, `string own`, or owning fixed array never moves that storage.
  The loop never copies elements as owning values.
  A span or string lends through a header copy without its outermost `own`.
  The loop holds that header before its first iteration (D7.5).
  The checker refuses `del`, assignment, and `move` of the collection or containing storage inside
  the loop body. This rule also applies through aliases.
  The checker permits a fort call or another ownership operation only when it proves that the
  operation preserves the collection storage.
  Element writes remain legal if they do not invalidate the collection storage.
  A loop that changes its collection storage uses `while`.
  The loop variable's type is the element type without its outermost `own`.
  For `node mut* own mut@ own kids`, the variable is `node mut* c` in `for (node mut* c : kids)`.
  An `own` loop variable is an error.
  The checker refuses owning aggregate elements, because a loop variable cannot copy them without
  `move`. Iterate those collections by index.
  Moving an element out is explicit: `move(kids[i])`.
  The storage loan lasts for the whole loop body. Local last-use reasoning does not shorten it.
  Alias and call proofs must preserve that duration (D19.8).
- history: Amended 2026-10-01 (T-263): The loop holds a lent header before its first iteration.
  The checker refuses operations that invalidate the collection storage inside the body.
  The previous rule lent an owning collection in its original place without this refusal.
  Amended 2026-10-01 (T-272): The ownership proof preserves the whole-body storage loan from T-263.

### D17.11 The overwrite check
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Overwrite check. In checked builds (D11.1), storing into an `own` reference-typed lvalue
  whose current value is not the zero value is a runtime error, `overwriting owned value` (D11.4),
  because the previous allocation would leak. `del` and `move` leave zero behind, so `del(v.data);
  v.data = new(...)`, `a = move(b)` after `move(a)`, and initialization from `{}` or `null` all
  pass. The check runs after the right-hand side is evaluated, immediately before the store, and is
  reported at the `=` token. Release builds store without checking. Assignments of owning aggregates
  are not checked field by field. A declaration is a store like any other and is checked too: the
  slot of an owning local is zeroed once in the entry block (D19.4), so the first execution of the
  declaration passes and a second one -- in a loop whose body did not `del` -- traps. Without that
  zeroing the check would read an uninitialized `alloca`, which is why declarations were skipped and
  why skipping them was wrong. A declaration's check is reported at the declared name rather than at
  its `=`: the parser records no location for that token and `ast_node_t` has no room for one, and
  the name is the better anchor in any case, being what a reader looks at to see whose allocation is
  about to be dropped (D11.4).
  Separately, the ownership proof checks destination leaves in all build modes.
  After right-side effects, each previous destination obligation must be empty or already
  transferred.
  This includes projected owners, owning aggregate leaves, and destinations reached through aliases.
  A removed runtime overwrite check supplies no proof of emptiness.
- history: Amended 2026-09-11: `for (i32 mut i = 0; i < 3; i++) { i32 mut* own p = new(i32); }`
  leaked one allocation per iteration with no diagnostic, while the same program written as an
  assignment trapped (T-022's review).
  Amended 2026-10-01 (T-272): Static overwrite obligations now include aggregate leaves. The
  runtime-check contract stays unchanged.

### D17.12 Owned strings
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: Strings. `string own` is an owned, immutable character sequence: `str.dup`, `str.concat` and
  `strbuf.take` return it; literals, sub-strings and `sys.args()` are `string`. `del(string own)` is
  legal and `del(string)` is not. A string built in a `u8 mut@ own` becomes a `string own` with
  `cast(move(buf), string own)` (the target says `own`, so the source must be moved, D3.14);
  `cast(buf, string)` lends a view instead.
  Borrowed strings retain their character-source relations through casts, copies, fields, and calls.
  A retained key does not extend its character allocation's lifetime (D17.14).
- history: Amended 2026-10-01 (T-272): Borrowed character sources gained retention proof
  obligations.

### D17.13 own in an extern signature
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: `own` may appear in `extern` signatures. It is erased, and it documents the C side's
  convention: `extern fn malloc(u64 n) void mut* own;` (the storage malloc answers is storage the
  caller may write, D3.11), `extern fn free(void* own p) void;` (a caller drops the `mut` at the
  call, D5.4). Signature identity includes `own` (D9.8). A C function whose result is owned on
  some calls and borrowed on others is declared in its borrowing form, because one symbol has one
  signature (D9.8). The owning form is a fort function that allocates the buffer itself:
  `std.libc` declares `realpath` with a `char mut*` buffer, and `std.os.real_path` allocates that
  buffer with `new`.
  An extern declaration is the implicit foreign trust boundary.
  Trust its declared ABI, types, ownership convention, storage validity, and extent.
  Check known fort argument sources before the call. Apply signature ownership transfers.
  An own input transfers its obligation. A borrowed input alone does not transfer ownership.
  An own result creates a fresh obligation under the declaration's uniqueness promise.
  A borrowed result has a trusted foreign source. Missing foreign bodies or summaries cause no
  error.
  Permit access and slicing of trusted results without inferred source relations or static extents.
  This trust does not prove non-nullness, initialization, alignment, or foreign storage lifetime.
  Foreign callers and implementations must satisfy those requirements.
  Keep known fort ownership facts across the call. Missing summaries do not block later cleanup.
  Hidden foreign aliases, retention, releases, writes, and callback effects remain outside the
  proof.
  Do not infer that foreign code has no effects.
  A foreign result may alias fort storage. Without a source relation, the proof cannot track that
  alias.
  Preserve foreign trust through copies, casts, fields, returns, and fort wrappers.
  Other fort source relations keep their proof obligations. A raw cast cannot create foreign trust.
  Trust arguments supplied at foreign entry points. Analyze fort callback bodies with symbolic
  inputs.
  This boundary requires no lifetime annotation, unsafe construct, contract syntax, or effect
  summary.
- history: Amended 2026-09-14 (T-086): `malloc` was declared `void* own`, because `void mut*` was
  not a type until D3.11 was amended.
  Amended 2026-09-29 (T-257): added the borrowing form for a C result that is owned on some calls
  only. A cast no longer adds `own` (D3.14), so an `extern` signature is the one way that C memory
  gets an owner.
  Amended 2026-10-01 (T-272): The previous extern rule described convention without the full
  foreign trust boundary. Hidden foreign effects stay outside proof.

### D17.14 The ownership proof
- owner: `type-system.md` (the `own` qualifier, placement, identity), `memory-model.md` (`move`,
  `del`, transfer, lending, the overwrite check), `core-language.md` (the `move` builtin,
  statements).
- rule: The compiler proves temporal storage and allocation ownership obligations.
  It checks fort operations in the complete checked import closure.
  It rejects an operation when a reaching path violates its obligation.
  It also rejects an operation when the analysis cannot prove its obligation.
  D17.13 supplies foreign trust.
  The proof covers dangling storage, lost ownership, invalid release, and invalid transfer.
  It separates place contents from source validity and allocation identity from owner-place
  identity.
  Copies, casts, retained fields, elements, globals, and available fort calls preserve source
  relations.
  Taking an address, retaining a value, or reaching storage through an alias ends no proof
  obligation.
  Release or transfer residual owned leaves before normal storage end.
  Local storage ends at its FIR dead marker. Temporary storage ends at its source-defined boundary.
  By-value parameter storage ends at each normal return after deferred effects, even without dead
  markers.
  Its residual owned leaves still require cleanup. Symbolic caller storage has a separate boundary.
  Check returned or retained borrows against their source boundary after deferred effects.
  Permit release after a borrow's last semantic use. An unused dangling local alone requires no
  error.
  Escaping results and retained fields preserve caller obligations beyond local last use.
  A replacement store need not read a dead borrow's previous value.
  Branch states may differ if later operations satisfy each reaching state.
  Loops preserve zero-iteration paths and residual obligations across iterations and scope exits.
  Summaries carry normal-return, abort, and unknown outcomes with relevant input conditions.
  Only proved absence of normal return removes a caller continuation. Unknown preserves normal
  cleanup.
  Abort paths require no cleanup. Allocation obligations cover memory, not scalar resources.
  Executable owning globals must be empty at normal exit.
  Libraries retain ownership between calls and provide explicit cleanup.
  The proof adds no lifetime annotations, unsafe syntax, runtime identity tracking, or source
  exemptions.
  It preserves mutable aliases, current ownership syntax, representations, and the ABI.
  D19.8 defines the analysis boundary and staged whole-feature selection.
  Detailed place, heap, raw-region, convergence, and global-boundary rules refine this base
  contract.
- history: Amended 2026-09-29 (T-257): the list held "two `own` copies made through `cast`". A cast
  no longer adds `own` (D3.14), and `cast(move(x), ...)` ends the source binding (D17.5).
  Amended 2026-10-01 (T-272): The previous rule deferred leak and dangling-view proof and left
  escaped fort storage untracked.

### D17.15 Place identity and aggregate storage
- owner: `memory-model.md` (2.6), `fir.md` (5.5 and 9.6), `type-system.md` (8.5).
- rule: The proof separates storage slots, pointed-to sources, and allocation identities.
  A slot has an object identity and a projected storage region.
  Bind a dynamic index to its value at the operation. Reassigning its local changes that value.
  Instantiate caller aliases before comparing storage or applying effects.
  Equal index values select equal slots only through the same proved backing storage.
  Different indices can prove disjoint element slots through proved non-overlapping regions.
  They never prove different pointees or allocations by themselves.
  Use finite equality, inequality, and range partitions for dynamic elements.
  A strong update requires one proved concrete destination region in each represented state.
  Apply the update to aliases of that region. Keep other regions unchanged.
  A weak update retains possible selected destinations and their guarded alternatives.
  It preserves unselected contents, live allocation obligations, and possible invalidation.
  Check an operation's obligations for each feasible destination. Imprecision supplies no exemption.
  Track nested owning leaves and borrowed fields through projected and whole-value operations.
  A projected move empties only its selected place. A whole move transfers the complete value.
  Both preserve heap allocation identities. Neither rebases an inline storage address.
  A slot address remains valid while its slot storage lives. It observes updated slot contents.
  End by-value parameter slots at normal return after deferred effects, without requiring dead
  markers.
  Check their residual owned leaves. Reject returned or retained borrows into those ending slots.
  Symbolic owning inputs admit live obligations. Current empty callers do not remove those inputs.
  Preserve separate symbolic caller sources referenced by borrowed parameter values.
  Bind aggregate _0 to the actual caller destination, including argument and global aliases.
  At each destructive result write, check previous overlapping owner obligations after operand
  effects.
  Do not assume fresh result storage. Preserve return, holding-temporary, and deferred-effect order.
  Check returned borrows against their original sources after deferred effects.
  Abstract transfer empties fixed temporary ownership even when generated code omits source
  clearing.
- rationale: Slot separation prevents false pointee separation and false inline-address relocation.
  Ordered result effects preserve caller ownership when result storage aliases input storage.
- history: Note 2026-10-02 (T-273): This entry refines D17.14 with dynamic places and aggregate
  storage boundaries. It changes no representation, runtime instruction, or calling convention.

### D17.16 Finite heap obligations
- owner: `memory-model.md` 2.7 (finite heap identities and inductive cleanup).
- rule: Each successful allocation creates a fresh identity and one release or transfer obligation.
  Repeated executions of one allocation expression do not identify their live allocations.
  A finite heap state retains distinct obligations or their simultaneous multiplicity.
  Alternative sources and simultaneous allocations are different facts.
  A release discharges only the selected allocation. It does not clear other allocations at that
  site.
  Joining or widening states cannot remove a possible residual obligation.
  Moving an owned edge transfers its target obligation and empties the source edge.
  Del stays shallow. Prove each owned leaf in the selected allocation empty before releasing it.
  A transferred descendant remains live until its new owner releases or transfers it.
  Finite structural predicates can represent unbounded acyclic chains and trees.
  Infer their construction, separation, and cleanup conditions from fort operations and summaries.
  Prove destructive cleanup by separating one node from the remainder, emptying its owned leaves,
  releasing that node, and preserving the remainder invariant.
  Pool cleanup also releases each block's owned payload before the block.
  A normal cleanup result requires an empty remainder and no detached residual obligation.
  Owned cycles and shared ownership cannot use an acyclic separation argument.
  A proved cycle cut can establish a shape that ordinary release rules accept.
  Shared borrows remain legal. Their uses retain the source-validity obligations of D17.14.
  Pool growth does not invalidate earlier block views. Pool cleanup invalidates the sources it
  releases.
  Lost shape, identity, or alias precision gives incomplete proof at an operation that needs it.
  It never gives successful cleanup. Add no annotation, recursive del, or runtime identity data.
- rationale: Allocation-site sets cannot count concurrent obligations or prove complete chain
  cleanup. Inductive predicates retain those facts in a finite state.

### D17.17 Raw storage and reference representations
- owner: `memory-model.md` (2.8), `type-system.md` (9.2).
- rule: Raw fort references retain their source, byte offset, access window, and alignment facts.
  Type erasure does not erase the original storage layout or its nested owning leaves.
  Distinguish the storage containing a reference from the storage that reference designates.
  A pointer cast changes neither source validity nor the permitted access window.
  A void-pointer round trip preserves these facts. No cast creates ownership or foreign trust.
  For a raw fort range, prove ordered nonnegative bounds and sufficient live storage.
  Check offset and size calculations without using wrapped results as extent proof.
  Extents can remain symbolic. Bind them to their captured allocation or view lengths.
  A fort typed access also requires enough accessible bytes and the required alignment.
  A cast itself adds no runtime alignment or range check.
  Ordinary span bounds retain their existing runtime checks (D6.9, D10.6).
  A complete borrowed representation copy preserves its original source and range relations.
  Infer that effect from the actual ordered reads and writes, including caller aliases.
  Partial reference writes preserve only the representation facts that their byte effects prove.
  Reject a later reference use or escape when its complete representation remains unproved.
  Before an overlapping write discards an owning leaf, prove its old obligation empty or
  transferred.
  A raw byte copy is not a semantic move. It cannot create a second owner.
  A complete copy that implements a semantic move transfers the original obligation once.
  Owner bits in a scalar byte buffer supply no transferable obligation or owning place.
  A later byte clear cannot repair an earlier invalid ownership duplication or loss.
  Byte interpretation cannot add mutability or bypass ownership conversion rules (D3.14).
  Padding supplies no reference source or ownership obligation.
  Preserve actual overlap and read order.
  Assume a source snapshot only when the operation proves it.
  Integer-to-pointer conversion alone supplies no storage source, even after an exact u64 round
  trip.
  Integer arithmetic and equal address bits supply no replacement source proof.
  The type system still permits the conversion. Reject memory operations that require its unproved
  source.
  A zero-length view and an end pointer permit no element access.
  An empty ordinary span can have no backing storage. A raw range still requires its source proof.
  A zero-element allocation retains its release or transfer obligation and zero logical extent.
  The runtime's minimum physical allocation size supplies no additional accessible element.
  Del stays shallow. Type erasure does not remove residual owned descendants.
  D17.13 permits foreign access without a static extent. Hidden foreign byte effects remain outside
  proof.
  Preserve that trust through reference copies and casts. Integer bits do not acquire foreign trust.
  Foreign extent trust does not override a known allocation's release or transfer state.
  Other known fort source relations retain their obligations at the foreign boundary.
- rationale: Reference bytes carry source relations separately from ownership transfer obligations.
  A byte pattern alone proves neither a live source nor a right to release an allocation.
- history: Note 2026-10-02 (T-275): This entry refines the raw-storage obligations of D17.14.
  It uses conservative rejection for integer reconstruction without a broader user ruling.
  It changes no representation, runtime instruction, arithmetic operator, or calling convention.

### D17.18 Finite ownership analysis and incomplete proof
- owner: `fir.md` (14.1), `toolchain.md` (4.2).
- rule: The ownership analysis uses a finite domain and deterministic work budgets.
  Bound paths, regions, predicates, identities, shape templates, target alternatives, and effects.
  Joins represent all predecessor states. Widening can lose precision but cannot remove obligations.
  Preserve release history, owned descendants, transferred regions, and retained source relations.
  Infer recursive summaries to a fixed point over a bounded symbolic interface.
  Keep provisional recursive results private. They prove neither absent effects nor absent return.
  Export a complete sound summary or an incomplete summary with unknown effects and outcomes.
  Incomplete fort summaries preserve possible caller continuation and its cleanup obligations.
  A noreturn type proves no caller return. It supplies no abort or process-cleanup classification.
  Keep normal process termination separate from function return, abort, and foreign termination.
  Compose outcome conditions before removing continuations or applying cleanup boundaries.
  Preserve normal-process-exit obligations even when a proved callee cannot return to its caller.
  Unknown fort effects receive no foreign exemption (D17.13).
  A precision loss gives no error when independent facts prove all affected obligations.
  Otherwise report the affected obligation as incomplete proof. A work-budget failure is an error.
  A validated reaching-path witness can establish an invalid operation.
  An abstract possibility without that witness establishes only failure to prove safety.
  Use canonical scheduling, stable event keys, and counted work for repeated-input determinism.
  Publish the implementation's numeric limits, measured coverage, and reproduction commands.
  A report limit cannot convert incomplete analysis into a successful verdict.
  These rules add no annotation, unsafe construct, runtime identity data, or ABI change.
- rationale: Finite summaries permit termination without accepting operations that lack proof.
  Separate witness classes prevent an imprecise state from claiming a concrete memory error.

### D17.19 Global ownership and process boundaries
- owner: `memory-model.md` (2.9), `toolchain.md` (2.1), `module-system.md` (11),
  `stdlib.md` (2.1 and 3).
- rule: A global retains storage across ordinary function returns.
  Its initializer establishes its initial contents. Transfers preserve allocation identity.
  A store must preserve or discharge old owning leaves after right-side effects.
  Fields and elements of global aggregates retain their separate obligations.
  Taking a global address creates no escape exemption.
  Normal executable exit requires empty owning globals in the complete checked closure.
  This includes runtime globals and live owned descendants reachable through global owners.
  Runtime cleanup precedes the final obligation boundary. A call entry is not that boundary.
  Generated startup creates the runtime argument owner before the source entry runs (D11.6).
  Positive argc creates a header allocation. Nonpositive argc leaves the owner empty.
  Normal entry return runs applicable source defers, flushes, and releases runtime argument storage.
  The final boundary precedes the generated C return or a known foreign normal-exit operation.
  `std.sys.exit` and `std.rt.exit` reach that boundary after ordered runtime cleanup.
  Establish that cleanup from analyzed fort or generated effects. A runtime name proves no release.
  Those exit functions do not execute caller defers or automatically delete user globals.
  Residual allocation obligations in suspended caller storage also require discharge there.
  Infer normal-termination requirements through fort wrappers and indirect fort targets.
  A cleanup followed by exit differs from an exit followed by unreachable cleanup.
  A source function return, including an ordinary call to a function named main, is not process
  exit.
  Libraries may retain owning globals between calls. Their summaries preserve those global states.
  They provide explicit cleanup through ordinary callable fort functions.
  Cleanup releases or transfers owned allocations and empties the library's owning global leaves.
  Library checking uses the owning globals of its complete checked closure.
  Foreign host storage outside that closure uses D17.13 trust.
  The host calls cleanup after final library use and before unload or normal host termination.
  Library checking uses symbolic global states and inputs. It infers caller requirements.
  It does not invent a host call sequence, generated startup, or executable-exit boundary.
  Foreign entries trust valid symbolic caller inputs under D17.13.
  Analyze callback-local storage and all available fort effects normally.
  Returned or retained borrows keep their source relations across ordinary returns.
  Abort paths, including panic and runtime traps, require no cleanup.
  The selected std.libc declarations exit(i32) noreturn and abort() noreturn establish normal and
  abort termination, respectively. Compatible declarations of the same C symbol share that class.
  Trust those standard conventions under foreign symbol interposition (D17.13).
  Resolve function-value aliases before composing wrapper outcomes. Keep mixed outcomes distinct.
  A noreturn type proves absence of caller return. It proves neither normal termination nor abort.
  The defensive trap after a noreturn call does not classify the callee's termination.
  Unresolved foreign process termination remains outside proof under D17.13.
  Accepting such a call supplies no proof of normal-exit cleanup and no abort exemption.
  Unknown fort bodies and targets receive no foreign exemption.
  These rules add no annotations, contract syntax, contract database, destructors, or identity
  tables.
- rationale: Global storage survives a function return.
  Executable normal exit ends its obligation.
  Ordered runtime cleanup permits argument access during execution without a runtime-global
  exemption.
  Foreign trust permits ordinary FFI without claiming proof of hidden foreign process effects.

## D18 Float printing in the runtime

Names the entry points that produce D11.7's float text and settles what D11.7 leaves to the runtime.

### D18.1 The two float entry points
- owner: `toolchain.md` (5.1 entry points).
- rule: The runtime exports exactly two float entry points, `std.rt.print_f32(i32 fd, f32 v)` and
  `std.rt.print_f64(i32 fd, f64 v)`. The print family (D12.2) calls one of them per float
  argument and passes the value in the argument's own type: an `f32` is never widened to `f64`
  first, because the digits printed depend on the type (D11.7). They stand in `std.rt` with every
  other entry point, so D9.10 settles their membership like every other declaration of that
  module: every closure holds them, whether or not a float stands in it. `std.rt` exports beside
  the two printers the two buffer formatters `append_f32(strbuf.str_buf mut* b, f32 v)` and
  `append_f64(strbuf.str_buf mut* b, f64 v)`, which give the same bytes to a `str_buf` rather than
  to a descriptor. Those two are library functions and not entry points: the compiler emits no
  call that names them, so the entry-point list of `toolchain.md` 5.1 stays the two printers
  (D18.4). One formatter serves both forms, so the two cannot disagree over a value, and that is
  why they stand in `std.rt` and not in `std.strbuf`; `std.rt` imports `std.strbuf` for them, so
  every closure holds `std.strbuf` and `std.mem` as well. What that costs a program that prints no
  float is measured: 8,549 bytes of `.text` and 105,914 bytes of emitted IR on 2026-09-14, against
  0 for the split this decision described before T-132. `notes/compiler.md` 7 holds the
  measurement and the command for each number.
- history: Amended 2026-09-11 (T-088): the two were C entry points named `fort_rt_print_f32` and
  `fort_rt_print_f64`, back when the runtime was C (D13.1 as amended). Amended 2026-09-13 (T-107):
  the module held the two printers alone, so a program could put a float on a descriptor and
  nowhere else, and the JSON writer of `src/lsp/json.ft` had no `write_f64`; `stdlib.md` 2.12
  specifies the two `append` functions and `std.rt_float` imports `std.strbuf` for them. Amended
  2026-09-14 (T-096): the rule as it stood gave the C bootstrap's refusal of floats as the reason
  for the split and put no end on it, which left the end of the split a hope; T-096 added the one
  condition that ends it, named the `fort_stage2` input that makes the condition checkable, and
  added the sentence that keeps the freeze of `bootstrap0/src` out of that condition. T-096
  measured the fold instead of making it, because the user kept the C bootstrap as the compiler
  that builds the compiler on that date; the measurement and its commands are in
  `notes/compiler.md` 7. The same amendment corrected the membership sentence, which until then
  read "loads `std.rt_float` as a root of every closure exactly as D9.10 loads `std.rt`": T-041
  had narrowed the loading rule to a closure that holds a float, and the narrowing reached
  `src/fort/modules.ft` and `notes/compiler.md` and never this field. T-096's first commit left
  the old sentence standing beside its own and so made one field state two loading rules; the
  review of 2026-09-14 found it, since `agents/check_decisions.py` compares two revisions and
  cannot see a field that contradicts itself. Amended 2026-09-14 (T-131): **the condition named
  above arrived on this date.** The `fort_stage2` target takes the last pin of `tools/bootstrap.ref`
  as its input and not the binary built from `bootstrap0/src`, so a fort compiler builds the fort
  compiler and `std/rt.ft` may hold a float. The fold of the two modules is T-132's and did not
  land with the condition. So between T-131 and T-132 the split stands although the condition
  that ends it holds; this note records that window rather than hiding it, and T-131's log
  carries it as a deviation. The sentences above that describe what the C bootstrap forces on
  `std/rt.ft` are the state before T-131 and hold no longer: the C bootstrap reads pin 0's
  library and never HEAD's (`notes/compiler.md` 8, invariant 5).
  Amended 2026-09-14 (T-132): **the window closed on the same date.** `std.rt_float` declares
  nothing and its four functions are declarations of `std.rt`, so the two entry points are
  `std.rt.print_f32` and `std.rt.print_f64` and the compiler names one module and not two. The
  paragraphs this rule carried until now stated the split, the reason for it, the on-demand
  loading rule that D9.10 does not have, and the one condition that ends it; all four are spent
  and the rule above replaces them with the fold and its measured cost. The fold moved no pin:
  fort has had floats since T-040, every pin implements them, and the split existed only because
  the C bootstrap compiled the whole import closure. The fold did move a **pin**, for the other
  reason `notes/compiler.md` 8 invariant 3 now states: pin 0's loader reads
  `<std-dir>/rt_float.ft` into any closure holding a float, and `std/rt.ft` holds floats after
  the fold, so pin 0 demanded a file HEAD means to delete. `bootstrap-1` is the commit of the
  fold, whose compiler asks for no such file, and `std/rt_float.ft` goes in the commit after it.

### D18.2 Shortest round-trip digits
- owner: `toolchain.md` (5.1 entry points).
- rule: Shortest round-trip. The digits are the shortest decimal that reads back as the value in
  that type and, among the strings of that length that read back as it, the one nearest the value,
  ties going to the even last digit. That text is what this decision fixes; how the runtime arrives
  at it is an implementation matter, described in `toolchain.md` 5.1. The layout around the digits
  is the runtime's own work, so the bytes are D11.7's and not a C library's `%g`.

### D18.3 The layout of float text
- owner: `toolchain.md` (5.1 entry points).
- rule: Layout. D11.7's choice of form is decided on the decimal exponent `e` of the leading digit:
  exponent form when `e < -4` or `e >= 17`, fixed form otherwise, so `1e-4` prints `0.0001` and
  `1e16` prints `10000000000000000.0`. `-0.0` prints `-0.0`, its sign taken from the sign bit
  (D6.12: `-0.0 == 0.0`, so no comparison can). A NaN prints `nan` whatever its sign bit and
  payload, since D11.7 lists `inf`, `-inf` and `nan` and no `-nan`.

### D18.4 No other float entry point
- owner: `toolchain.md` (5.1 entry points).
- rule: No other float entry point. The conversions of D3.14 (float to integer, saturating with NaN
  to zero, and integer to float) are emitted in the module the compiler generates (D19.1), not
  called for, so the list in `toolchain.md` 5.1 stays complete (D11.6).

## D19 Target: LLVM IR

Decided 2026-09-10, before any code-generation ticket had started; the earlier target, x86-64 GNU
assembly, survives only in the history of this file and of `toolchain.md`.

### D19.1 One textual LLVM IR module
- owner: `toolchain.md` (6, the IR contract).
- rule: The compiler emits one textual LLVM IR module (`.ll`, LLVM 18 syntax, opaque pointers) for
  the whole program (D9.10), built by string appending, and hands it to clang (D14.3). The
  translator of D19.8 writes each function: it scans a FIR function once to classify its locals and
  then appends in one forward pass over it. The compiler never links libLLVM or calls its C or C++
  API: the bootstrap stays a dependency-free C11 program and the self-hosted compiler needs no
  foreign bindings. The module carries `target triple = "x86_64-unknown-linux-gnu"` for Linux or the
  selected `arm64-apple-macosxM.m.p` for Mac. It carries no datalayout, module flags, comments or
  `source_filename`. Clang derives the target layout from the selected triple. Every emitted module
  must pass `opt -passes=verify`; the language-test harness checks that (`run_tests.py --verify-ir`)
  and the emitter suites verify each module they emit. The goldens of
  `bootstrap0/test/gen_module_test.c` are the reference for the form of a module until the contract
  below says otherwise.
- history: Amended 2026-09-15 (T-140): Mac IR uses the selected Apple triple without a datalayout.
  Amended 2026-09-25 (the user): the hand-written samples under `test/ir/` and the pipeline test
  that ran them were retired; until then they were the reference for the form of a module.
  Amended 2026-09-28 (T-209): the module was built "in one forward pass" over the checked tree
  alone. D19.8 adds FIR, and the translator appends from it.
  Amended 2026-09-30 (T-255): the rule said that the direct path of the emitter appends in one
  forward pass over the checked tree. T-255 deleted the direct path, and the translator writes
  every function. The same day `toolchain.md` 6 item 1 dropped "in one forward pass", item 10
  dropped the sentence that the tree walk never has to know its predecessors and named the
  blocks by FIR block number, and section 8 replaced the direct path with the pipeline of FIR.

### D19.2 Type mapping
- owner: `toolchain.md` (6, the IR contract).
- rule: Type mapping. `i8 i16 i32 i64` and `u8 u16 u32 u64` are the LLVM types `i8 i16 i32 i64`:
  signedness lives in the instruction (`sdiv` against `udiv`, `sext` against `zext`, `icmp slt`
  against `icmp ult`) and never in the type (D3.1). `char` is `i8` compared and extended as unsigned
  (D3.2). `bool` is `i1` as a value and `i8` in memory (D3.3), so a `bool` field or global has the
  layout and the two values of C's `_Bool`: every load of a `bool` place is a `load i8` and a
  `trunc`, every store a `zext` and a `store i8`. `f32` and `f64` are `float` and `double`. Every
  pointer, `void*` and function pointer is the opaque `ptr` (D3.11, D3.10); the pointee type is the
  compiler's business and appears only on the instructions that need it, so a function pointer is a
  `ptr` and a call through one an ordinary `call` whose function type is written out, `call i32
  (i32, i32) %t0(...)`, as item 8's variadic form is: an opaque `ptr` carries no signature, so the
  spelled type is the only description of the callee at that site. `T[N]` is `[N x T]` (D3.4); a
  span and `string` are the one type `%fort.span = type { ptr, i64 }` (D3.5, D3.7); a struct is
  `%struct.<dotted name>` with its fields in declaration order and never `packed`, because LLVM lays
  that type out exactly as C does (D3.8); an enum is `i32` (D3.9); `void` is `void` and only a
  result type. Float to integer uses the saturating intrinsics of D3.14 and every other conversion
  the obvious cast instruction.
- history: Amended 2026-09-10: the IR type was `%fort.slice` while spans were called slices (D3.5).

### D19.3 Aggregates live in memory
- owner: `toolchain.md` (6, the IR contract).
- rule: Aggregates live in memory. Only scalars are SSA values: a struct, fixed array, span or
  `string` always occupies a place (an `alloca`, a global, or memory reached through a `ptr`) and is
  never loaded or stored as one value, so the emitter never writes `insertvalue` or `extractvalue`
  on a fort aggregate (its only `extractvalue` takes apart the `{iN, i1}` that an overflow intrinsic
  returns, D19.6). Copying an aggregate is `llvm.memcpy`, zeroing one is `llvm.memset` and reaching
  a field or element is `getelementptr`. This is D9.9's model spelled in IR. The lowering of D19.8
  keeps the same model: a FIR local of aggregate type is a place, and an operand of aggregate type
  names a place.
- history: Amended 2026-09-10: spans were called slices (D3.5). Amended 2026-09-28 (T-209): the
  walk wrote LLVM text directly; D19.8 makes it write FIR. Amended 2026-09-30 (T-255): the rule
  said that this model keeps code generation "one tree walk with a destination place per
  expression (D19.1)", and that the lowering is that walk. T-255 deleted the direct path, which
  was the tree walk.

### D19.4 Entry-block allocas and SSA discipline
- owner: `toolchain.md` (6, the IR contract).
- rule: Entry-block allocas and SSA discipline. Every local, parameter copy and compiler temporary
  is an `alloca` in the entry block, before any other instruction and in declaration order, because
  LLVM's promotion passes look only there; no `alloca` is variable-length (array lengths are
  constants, D3.4, and `new` is the heap, D10.2). The emitter builds no `phi` and carries no value
  across a merge point: a temporary is used only in the block that defines it or in a block that
  block dominates, and `&&`, `||` and `?:` short-circuit through a stack slot (D6.3) that the
  optimizer promotes. Control flow is explicit blocks: `br` for `if`, `while`, `for`, `break` and
  `continue`; an LLVM `switch` with one case per label and a default block for a fort `switch`
  (D7.7); a fresh block for the statements D14.2 allows after a terminating one. Together with D19.3
  this is what keeps the module verifier-clean without any analysis in the emitter.

### D19.5 The emitted text is a function of the program and target
- owner: `toolchain.md` (6, the IR contract).
- rule: The emitted text is a function of the program, selected target and invocation inputs
  other than the `-o` output path.
  Two `-S` runs use identical arguments except the `-o` output path.
  That path does not enter the emitted IR text.
  A self-hosted compiler reaches a fixpoint when stage2 and stage3 emit byte-identical modules
  for the same sources and selected target in four modes: checked and `--release` (D11.1), each
  with and without `--no-bounds-check` (D10.6).
  The toolchain must make two comparisons in each of the four modes. It must compare with `cmp` the
  `-S` output that two distinct stages emit for one input, with `diff` as the debugging output, and
  it must compare two stage binaries byte for byte.
  On Mac, both links use one output pathname.
  The toolchain copies the first binary before relinking.
  Equal binaries are one program, so by the first
  sentence of this rule they emit one module for one input and selected target.
  The toolchain must not add a third
  `-S` run over stage3 to compare that text. That inference assumes the determinism this rule
  states, so the binary comparison tests a consequence of this rule and not the rule itself. The
  module comparison of two distinct stages narrows the gap, and no comparison this rule requires
  holds two runs of one compiler against each other. Hence: every value, parameter and block is
  named, so LLVM never numbers anything implicitly; per function and reset at each definition,
  instruction results are `%t<N>` in emission order, blocks are `%L<N>` by FIR block number with
  FIR block 0 always literally `entry` and the failure blocks numbered after the last FIR block in
  the order of the blocks that make them (`fir.md` 12.4), locals are `%<ident>.<slot>` by the
  local's index in the function, parameters arrive as `%<ident>.in`, an aggregate result pointer is
  `%ret.sret` and compiler-made places are `%tmp<K>` from a third per-function counter. A name that
  embeds a fort identifier always contains a dot and a name the compiler invents never does, which
  is what makes collisions impossible: an identifier cannot contain a dot (D2.3), so a local named
  `tmp` is `%tmp.0` and never `%tmp0`, and `%ret.sret` cannot be a local named `ret`, whose names
  are `%ret.<slot>` and `%ret.in`. Module-level counters (`@.str.<N>`, `@.file.<N>`) are assigned on
  first use and never deduplicated by content; enum tables are keyed by the mangled name. Order is
  by construction and never by iteration over a hash table: modules in dependency order,
  declarations in source order, runtime declarations in the fixed order of `toolchain.md` 5.1,
  intrinsics in a fixed table order, `extern` declarations in first-use order, attribute groups at
  fixed indices with unused indices simply absent.
  Apart from the selected target, nothing in the text depends on the environment:
  no timestamps, no compiler version, no comments, no `!llvm.ident`, and no path other than the ones
  D11.4 prints, which `@.file.<N>` holds exactly as the compiler opened them, so comparing two
  stages means using the same working directory and arguments except `-o`.
  Source roots, build mode and selected target still match; the IR is not path-free.
  An integer constant is printed in decimal without padding and with the
  signedness of its fort type (`store i8 -1` for an `i8`, `store i8 255` for a `u8`, and `i64` MIN
  as `-9223372036854775808`); a float constant is printed as the LLVM hex literal of its `double`
  bit pattern, an `f32` constant converted to `double` first, so no decimal rounding can differ
  between two stages.
- history: Amended 2026-09-10 with how the naming half is checked: `opt -passes=verify` cannot
  enforce it, because an instruction after a terminator makes the verifier *create* an implicit
  number rather than reject the module. `opt-18` exits 0 on a block whose `unreachable` is followed
  by a `br`, splits it and prints the remainder as `0: ; No predecessors!` -- an implicitly numbered
  block, which is the one thing this decision forbids. The verifier is a floor; the emitter's own
  tests assert block structure. Amended 2026-09-12 (T-039) with which comparison performs it.
  `tools/fixpoint.sh` does not run stage3 with `-S`: it compares the stage2 and stage3 **binaries**
  with `cmp`, and equal bytes are one program, which emits one module for one input. The module
  comparison it does run is stage1's against stage2's, which this decision does not ask for and
  which is stronger, since it holds the two compilers against each other rather than one compiler
  against itself. It runs in both build modes, as this decision requires, and `diff` is still the
  debugging output. A ticket that reads the sentence above must not add a third `-S` run to perform
  it literally. Amended 2026-09-13 (T-109) to state the two comparisons this decision requires.
  Until then the rule read "and the bootstrap script compares them with `cmp` over `-S` output,
  with `diff` as the debugging output", where "them" is stage2 and stage3, so the note above
  corrected the rule instead of the rule stating it. Two sentences of that note describe the log as
  it stood on 2026-09-12. This decision does ask for the module comparison of two distinct stages
  from this date, and "the sentence above" there means the sentence this note quotes. The rule
  names no script and no test: it states the requirement, and `notes/testing.md` 5 records how
  `tools/fixpoint.sh` and the ctest `bootstrap` meet it, so a rename of either amends no decision.
  The rule also carries the condition of the inference in the note above: equal binaries prove
  equal modules only under the determinism the rule itself requires, so that comparison tests a
  consequence of the rule. Nothing in the repository compares two runs of one compiler on one
  input. The module comparison of stage1 against stage2 on each run, and `tools/diff_ir.sh` over
  the 516 `.ft` files of the repository that compile (the ctest `diff-ir`, measured 2026-09-13),
  would both fail with high probability on a hash-seeded emitter; that is probability and not
  proof. The gap is T-039's inference and this amendment leaves it where it was.
  Amended 2026-09-14 (T-131): the pair the toolchain compares changed and this rule did not. The
  bootstrap became a chain of pinned commits of this repository (`notes/compiler.md` 8): the C
  compiler builds pin 0, each pin builds the next, and the last pin builds HEAD. The last pin and
  HEAD are different programs, so the module the last pin emits for `src/fort` differs from
  HEAD's on any commit that touches the emitter, and the comparison of stage1's module against
  stage2's would then report a difference that is not a defect. Both members of the pair must
  embody HEAD's sources. `tools/fixpoint.sh` therefore compares the module **stage2 and stage3**
  emit, where stage2 is HEAD's sources through the last pin and stage3 is HEAD's sources through
  stage2, and it compares the **stage3 and stage4** binaries with `cmp`. That is two distinct
  stages over one input, two stage binaries, two `-S` runs and no third, so the requirement above
  is met word for word. T-131 weakened nothing. The module comparison the script ran before was
  wider than the requirement above, because it held two compilers against each other; the
  comparison from this date is the one the two sentences above describe.
  Amended 2026-09-15 (T-140): the selected target now determines the IR header.
  Mac binary comparisons use an identical linker output path for both stages.
  Apple code signatures can differ when only the output path differs.
  The toolchain snapshots the first binary before it links the second at that path.
  Amended 2026-09-15 (T-140): two `-S` runs may use different `-o` paths without changing IR.
  Amended 2026-09-17 (T-160): the project removed C-to-fort comparisons. The fixed-point CTest
  entry is now `fixpoint`.
  Amended 2026-09-29 (T-261): the rule read "in both build modes" and "two comparisons in each
  build mode". The fixpoint is now checked in four modes, checked and `--release`, each with and
  without `--no-bounds-check`. The user ruled on 2026-09-29.
  `--no-bounds-check` removes the index and span checks from the stage binaries, so a stage3
  built with it is a different program that must reproduce itself too.
  Amended 2026-09-30 (T-255): blocks were `%L<N>` "in creation order", the order in which the
  direct path created them. T-255 deleted the direct path, and the translator names each block by
  its FIR block number.

### D19.6 Checks and failure blocks
- owner: `toolchain.md` (6, the IR contract).
- rule: Checks and failure blocks. Every runtime check (D10.6, D11.1, D11.3, D17.11) computes one
  `i1` that is true on failure and branches with the failure block as the first label; `assert` is
  the exception, since its operand is already the success condition (D12.2), and so is the `default`
  D7.7 gives an enum `switch`, which the switch's own default edge reaches. Failure blocks are
  emitted after every normal block of the function, in ascending label order; each holds exactly one
  call to the `toolchain.md` 5.1 entry point, with the offending values, the file constant and the
  line and column of D11.4's position rule, followed by `unreachable`. The failure entry points
  carry `cold noreturn nounwind` on their definitions, in the group `#8` of `toolchain.md` 6 item
  14: `noreturn` is truthful, since every one of them is `fn noreturn` in `std.rt` (D8.5, D13.1) and
  aborts, and `cold` lays the block out of line, which is what the out-of-line failure stubs used to
  do. `--no-bounds-check` removes exactly the index and span branches (D10.6).
- history: Amended 2026-09-11 (T-020): the `switch` default was added, with `std.rt.fail_enum` in
  `toolchain.md` 5.1, which was then the C `fort_rt_fail_enum`. Amended 2026-09-11 (T-088): the
  attributes stood on a `declare` of a C entry point and `noreturn` was truthful because the C
  function was `_Noreturn`; the runtime is fort (D13.1 as amended) and the module defines it, so
  they stand on the definition the compiler emits from its `noreturn` fort signature.

### D19.7 The trap after a noreturn body
- owner: `toolchain.md` (6, the IR contract).
- rule: The trap D8.5 requires after the body of a `noreturn` function and after every call to one
  is `call void @llvm.trap()` followed by `unreachable`.
  Linux x86-64 lowers `llvm.trap` to `ud2` and raises SIGILL.
  Mac arm64 lowers it to `brk` and raises SIGTRAP. Neither target writes a message.
  `unreachable` alone is not a trap; LLVM can let control fall through it.
  The compiler therefore emits the call and not only the terminator (D11.4).
- history: Amended 2026-09-15 (T-140): the trap signal now follows the selected target.

### D19.8 FIR, the representation between the checker and the IR text
- owner: `fir.md`.
- rule: The compiler lowers each checked function body to FIR, verifies it, runs its passes over it,
  and translates it to the LLVM IR text of `toolchain.md` 6 (`fir.md`). FIR follows the shape of
  Rust's MIR: a control-flow graph of basic blocks for each function, whose statements read and
  write places (a local or a global with projections for a field, an element or a dereference),
  whose operands say whether they `copy` or `move`, and whose runtime checks are `check`
  terminators. FIR holds fort types and is not in SSA form. The deferred statements are expanded on
  each path that runs them, and every statement carries its source location. The lowering makes
  every decision that depends on fort meaning and emits every runtime check. The build-mode pass
  removes the checks the selected mode drops and rewrites the operations it changes, and the flow
  analyses run before it, so an analysis gives one answer in every mode. The translator maps each
  FIR construct to LLVM text, chooses the LLVM instruction from the fort types, and makes the
  choices that depend on the target; it does not read the build mode. A feature of the language that
  is not in the core of FIR lowers into it, so the translator and the analyses do not change for it;
  a feature that needs a new projection or constant extends the core once. A function that the
  lowering does not support, or that needs a `std.rt` entry that the closure lacks for a check
  that the selected mode keeps, is a compile error (`fir.md` 9.8).
  Ownership analysis reads verified FIR for the complete checked import closure (D17.14).
  It includes all available fort bodies, including runtime bodies in std.rt from std/rt.ft.
  Infer source, alias, retention, and ordered effect summaries. Substitute actual aliases before
  effects.
  Bind aggregate _0 to its caller destination, including aliases with arguments and globals.
  Keep ordered result writes, reads, moves, and deferred effects from FIR.
  Unknown fort effects retain obligations and possible normal continuations.
  An address, global, projected place, or aggregate result supplies no permanent escape exemption.
  Ownership verdicts stay identical across checked, release, and bounds-check options.
  The proof does not derive safety from a runtime check that a build mode removes.
  During feature development, --ownership-check temporarily selects the complete ownership analysis.
  This whole-feature selection is not an operation-level proof exemption.
  Selected analysis never exempts an imported fort module or an available runtime body.
  Complete feature validation before migrating existing compiler, runtime, library, and LSP source.
  After migration, require the analysis by default and remove the temporary selection option.
  Staged delivery does not mean that the current compiler implements the complete proof.
  The analysis adds no runtime ownership checks or representation changes (toolchain.md 6).
- rationale: two passes derived one fact from the tree four times, and each time nothing compared
  the two answers: the parameter types of a runtime entry (T-072), lvalue-ness (T-193), whether a
  body can fall off its end, and the expansion of deferred statements. One lowering derives each
  fact once, and the translator and every flow analysis read the result. A fixed core with places
  and explicit moves is also where the features D15 defers need the least change: the user
  chose it over a model of the LLVM output on 2026-09-28, because the latter read like LLVM and
  gave only checks of the emitter's own text.
- history: Amended 2026-09-30 (T-255): the rule said that until the migration of `fir.md` 16
  ends, the direct path of the emitter writes each function that the lowering does not support.
  T-255 deleted the direct path. The same day the rule gained the compile error for a function
  that needs a `std.rt` entry that the closure lacks for a check that the selected mode keeps;
  until then the direct path wrote such a function and called the missing entry.
  Amended 2026-10-01 (T-272): Ownership analysis replaces permanent escape-based loss of checking
  and gains staged selection.

## D20 Editor support

Decided 2026-09-10. An editor underlines the construct a diagnostic is about and jumps to the name a
declaration introduces, so both are recorded from the parser on. The check mode is the compiler's
whole editor interface: it never grows a server. The identifier index is D20.3, decided once the
checker could resolve a name; D20.5 fixes what the self-hosted compiler's modules must be for a
language server to use them, while the server itself lands after the bootstrap fixpoint.

### D20.1 The check mode
- owner: `toolchain.md` (1, 4).
- rule: `fort --check entry.ft` lexes, parses, resolves the import closure, and checks each module.
  When ownership analysis is selected, it also lowers and verifies FIR and runs that analysis
  (D19.8).
  It then stops: no LLVM IR, no `--cc`, no temporary directory, so `-o`, `-S`,
  `-c`, `-l`, `--cc` and `-Xcc` are unused as they already are under `-S`. `--target` selects the
  three configuration values of D21.1. An unsupported target is a usage error. Exit 0 when nothing
  was reported, 1 when anything was, 2 for a usage, toolchain or internal error (D14.1). The entry
  module need not define `main`: under `--check` it is a module under inspection and not a program,
  so D8.6 is not applied. Every other rule holds, the diagnostics of D14.2 included.
  Check mode and build mode use the same selected ownership proof and diagnostic contract.
- history: Amended 2026-09-16 (T-156): `--target` now selects the D21.1 configuration values under
  `--check`. The earlier rule named `--target` as unused in this mode.
  Amended 2026-10-01 (T-272): Check mode previously stopped at the front end. Selected ownership
  analysis now includes verified FIR.

### D20.2 The JSON document
- owner: `toolchain.md` (1, 4).
- rule: `fort --check --json entry.ft` writes one JSON document to stdout and no text diagnostic;
  `fort: error:` lines stay on stderr, so a client tells a crash (exit 2, stdout empty) from a
  verdict (exit 0 or 1, one document). The document is built whole and written with one `fwrite`, so
  stdout is a complete document or empty and never a truncated one. `--json` without `--check` is a
  usage error: a build spawns `--cc`, which inherits stdout, so no build can promise that; `-S`,
  which spawns nothing, could be given the document by a later amendment. The document is
  `{"version": 1, "files": [...], "diagnostics": [...], "symbols": [...]}`, one line, `"version"` 1
  for this form. `"files"` lists every file the compiler read, in that order, as the `<file>` of
  D14.2, so a client can clear the stale diagnostics of a file that no longer has any. A diagnostic
  is `{"file", "line", "col", "end_line", "end_col", "severity", "message", "notes": [{"file",
  "line", "col", "end_line", "end_col", "message"}]}`, the notes of D14.2 nested under the error
  they follow. `"severity"` is `"error"` on every diagnostic the compiler reports as one, there
  being no warnings in v1, and `"note"` on a note that follows no error and so has none to nest
  under; a nested note carries no severity of its own, its place saying what it is. Positions are
  the 1-based byte columns of D14.2 and D20.4 with the end exclusive, and converting them to UTF-16
  is the client's job. `"symbols"` is the identifier index of D20.3, which is empty unless `--index`
  was given.

### D20.3 The identifier index
- owner: `toolchain.md` (1, 4).
- rule: `fort --index entry.ft` fills `"symbols"` with the identifier index. `--index` implies
  `--check` and `--json`, so the index is always a member of the document of D20.2 and is the empty
  array without it. The index holds one record per identifier occurrence the checker resolved, in
  every module of the closure that was checked: `{"file", "line", "col", "end_line", "end_col",
  "name", "kind", "type", "is_decl", "decl": {"file", "line", "col", "end_line", "end_col"} |
  null}`. The record's own range is the range of that one name token and never the construct's first
  token (D20.4), so an editor underlines the name the reader pointed at and nothing else. `"kind"`
  is the kind of what the name denotes, spelled as a diagnostic spells it: `module`, `fn`, `extern
  fn`, `struct`, `enum`, `enum member`, `field`, `constant`, `global`, `local`, `parameter` or
  `builtin` (D7.9, D7.10, D3.9, D12.2). `"type"` is the declaration's type as a declaration spells
  it (D5.2, D5.3). It is the empty string for a name that denotes no value type -- a module, a
  struct name, an enum name, a builtin -- and `null` when the declaration failed to check, whose
  type is the poison of D14.2 and says nothing a reader wants; a client renders `null` as unknown
  and the empty string as nothing at all, so the two cases stay apart. `"is_decl"` is true on the
  occurrence that declares the name here and false on every use of it. The `as` alias of an import
  is a declaration by that rule: it introduces the name in the importing module (D9.3), so its
  occurrence is `true` while its `"decl"` still points at the declaration it binds, since going to
  the definition of an alias must land on the thing and not on the import line; an import without an
  alias introduces the name the declaration already has, so its occurrence is a use like any other.
  `"decl"` is the range of the declaring name token, which for a record with `"is_decl"` is its own
  range except on an alias; it is `null` for a builtin, which no source declares, and the empty
  range at 1:1 of the module's own file for a module, which is declared by a file and has no name
  token (D9.1, and D14.2's position for what has none): going to the definition of a module
  qualifier then opens that file at the top, where `null` would be indistinguishable from a
  builtin's nothing to jump to. The consequence is that an `as` alias is the only kind `module`
  record whose `"is_decl"` is true: a module declared by a file has no name token for a rename to
  anchor on, and an import without an alias is a use like any other. Records are ordered by file, an
  imported module before its importers (D9.10), and within a file by the start of the occurrence. A
  name the checker did not resolve carries no record, so a file with errors still indexes everything
  that resolved; neither does a construct that has no name token of its own, nor a segment of an
  import path before its last, which names a search directory and not a module (D9.2, D9.3), nor the
  pseudo-fields `.len` and `.ptr`, which are a property of the type rather than a declaration of any
  module (D3.4, D3.5, D3.7). An occurrence and its declaration each carry the type and the
  declaration range in full: the document is read once and thrown away, so a client never resolves a
  reference into a second table.
- history: Amended 2026-09-10: the consequence read "no record has `"is_decl"` true for kind
  `module`", contradicting the alias rule three sentences above it and the index the compiler
  already emits, where `import m as alias;` gives `alias` an `"is_decl"` of true.

### D20.4 Ranges
- owner: `toolchain.md` (1, 4).
- rule: Ranges. A position is a range: from the first byte of its first token to one past the last
  byte of its last token, the start inclusive and the end exclusive, both 1-based byte columns with
  a tab counting as one column (D14.2). No token spans lines (D2.9), so the range of a single token
  ends on the line it starts on. Every syntax-tree node carries the range from its own anchor token,
  which is its first token except on the nodes named after an operator, where it is that operator
  (`toolchain.md` 4), to the end of the last token of the construct; the runtime-error position of
  `toolchain.md` 4 is the start of that range and is unchanged. A node whose start cannot move over
  the opening delimiter of the construct does not stretch over the closing one either: a
  parenthesized expression is the inner expression's own node, anchored at its own first token or at
  its operator, so it covers neither parenthesis rather than covering the `)` alone and reading as a
  range that begins inside the text it is about. Every node that a name declares or mentions also
  carries the range of that name token, and a node without a name carries the empty range for it. A
  node with no token of its own, such as the implicit block of a case body (D7.6), begins as the
  empty range where the construct it stands for begins. Extending a range moves its end to the later
  of the two ends and never moves its start, so extending it again with a token it already covers
  changes nothing and a node anchored at its operator never ends up with an end before its start. An
  error without a position in the file is the empty range at 1:1 (D14.2). The header line of the
  text form of D14.2 prints the start only. The underline under it shows the range.
- history: Amended 2026-09-27 (T-191): the text form printed the start only and showed nothing of
  the range, so the ranges changed no diagnostic text.

### D20.5 The self-hosted compiler is re-entrant
- owner: `toolchain.md` (1, 4).
- rule: The self-hosted compiler is re-entrant. A language server analyses one document many times
  in one process, so `src/fort` is written for that from its first module rather than retrofitted:
  no module-level mutable state outlives one analysis -- the diagnostic sink, every counter and
  every cache is a field of a session value passed down, never a global; every allocation an
  analysis makes comes from that session's pool and dies with it, so a thousand re-analyses of one
  document do not grow the heap; no library module ends the process, an impossible input being a
  `panic` at the API boundary that broke its precondition (D13.3) rather than an exit in a leaf; and
  every read of a source file goes through one function, so a server can hand it a buffer it holds
  in memory instead of a path. The C bootstrap satisfies none of these and is not required to: it is
  a batch process that exits when it is done, and the freeze of T-046 (`toolchain.md` 7.3,
  `notes/compiler.md` 8) leaves it that way. What a `panic` buys over an exit is a documented
  boundary and a stated precondition, not in-process recovery -- `std.rt.panic` aborts like any
  other failure (D11.4), and a server that must survive a
  malformed document runs the analysis where it can observe that abort. The protocol, the wire
  format and the server's own structure are not decided here and land after the bootstrap fixpoint,
  which the ctest `fixpoint` and `tools/fixpoint.sh` perform (T-039); this decision fixes only what
  the compiler's modules must be for such a server to be possible at all.
- history: Amended 2026-09-12 (T-105): two citations. The first said the protocol and the server
  "land after the bootstrap fixpoint (D19.5)". D19.5 requires the emitted text to be a function of
  the program and states the condition of the fixpoint, and it decides no milestone. Its rule
  still says the bootstrap script compares the two stages with `cmp` over `-S` output; T-039's
  history note records that `tools/fixpoint.sh` compares the stage2 and stage3 **binaries** instead,
  and that a ticket must not add a third `-S` run to perform the rule sentence literally. The
  citation was accurate on 2026-09-11, when D19.5 was the only statement of that comparison. It
  outlived its subject on 2026-09-12, when T-039 put the comparison that performs the fixpoint in
  `tools/fixpoint.sh` and the ctest `bootstrap`; it became wrong rather than being wrong from the
  start. The sentence names the ctest `bootstrap` and `tools/fixpoint.sh` now. The second citation
  said "D14.6's freeze (T-046)". D14.6 is the test-to-source ratio and holds no freeze; that
  citation was wrong when this decision was written on 2026-09-11, since D14.6 said the same thing
  then. The freeze of the C bootstrap is `toolchain.md` 7.3 and `notes/compiler.md` 8, which the
  sentence names now. Amended 2026-09-13 (T-109): D19.5's rule states both comparisons itself from
  that date, so the sentence above about what its rule "still says" describes the log as it stood
  on 2026-09-12.
  Amended 2026-09-17 (T-160): the fixed-point CTest entry is now `fixpoint`.

## D21 Compile-time selection

### D21.1 Configuration values
- owner: `core-language.md` (Expressions), `toolchain.md` (1), `grammar.md` (Expressions).
- rule: `$cfg(name)` is a constant expression of type `string`. The `name` uses the identifier
  syntax but is not a name lookup. One configuration map applies to the complete module closure.
  The expression returns the configured bytes. It reports a compile error when the key is absent.
  An empty configured value is present and returns the empty string.

  `--cfg key=value[,key=value]*` adds user pairs. The option is repeatable. A value ends at a comma,
  can contain `=`, and can be empty. An empty list entry, an assignment without `=`, an invalid key,
  or a duplicate key is a usage error. The compiler rejects a duplicate even when both values are
  equal.

  The selected target always supplies `target_os`, `target_arch`, and `target_abi`. Users cannot
  override these keys. `x86_64-linux-gnu` supplies `linux`, `x86_64`, and the empty string.
  `arm64-apple-macosxM.m.p` supplies `macos`, `aarch64`, and the empty string. `--target` selects
  these values during a build and during `--check`. `--tokens` and `--ast` parse `$cfg` without
  evaluating it.

### D21.2 Compile-time conditions
- owner: `core-language.md` (Compile-time selection), `grammar.md` (Declarations, Statements),
  `type-system.md` (Selection constant expressions), `toolchain.md` (1, 2, 4, 9.1).
- rule: `$if (condition) { ... } else $if (condition) { ... } else { ... }` selects declarations
  at module level and statements inside a function. Each branch holds constructs of its context.
  An `else $if` chain selects its first true branch. A chain with no true condition and no `else`
  selects nothing. One chain can contain at most 256 conditions. The compiler reports the 257th
  `$if` as a compile error.

  A condition must have type `bool` and must be a selection constant expression. Selection
  constant expressions use the ordinary expression grammar and the precedence of D6.1. The first
  version accepts literals, `$cfg`, `sizeof(T)`, fixed-array `.len`, and unconditional immutable
  declarations from the same module. Such a declaration must have an unqualified primitive type
  or plain `string`. It can depend only on the same inputs and on other such declarations.
  Operators combine these values under the ordinary constant rules. The selector rejects pointer,
  reference, array, and enum values. Imported declarations, declarations inside a `$if`, and
  run-time values are not available to a condition. The selector resolves only declarations that
  a condition reaches. It resolves them lazily and does not use source order. A reached cycle
  reports once, at the reference that closes the cycle. `&&`, `||`, and `?:` do not evaluate an
  operand that the result does not select.

  The lexer and parser read all branches. They report lexical and syntax errors in any branch.
  The compiler selects branches before module closure construction and ordinary name collection.
  Later passes inspect only selected branches. The selector does not evaluate a nested `$if` in
  an inactive branch. An inactive branch adds no name, semantic diagnostic, index item, data,
  function, or instruction. `--ast` prints all branches.

  A declaration branch contains declarations or nested declaration `$if` forms. A statement
  branch contains statements or nested statement `$if` forms. `$if` is not a field, enum member,
  parameter, type, or expression form.

### D21.3 Conditional imports
- owner: `module-system.md` (Import forms and resolution), `grammar.md` (Module structure),
  `toolchain.md` (2, 4, 9.1).
- rule: A module-level `$if` chain is an import selector when at least one recursive branch leaf
  is an `import`. Each other leaf must be an `import`, a nested import selector, or empty. A chain
  with no import leaf is a declaration selector under D21.2. This includes an all-empty chain. A
  chain with an import leaf and a declaration leaf reports one syntax diagnostic at its opening
  `$if`. A module-level statement remains a syntax error.

  An import selector occurs before each declaration and declaration selector. Parsed forms set
  this order. An inactive import selector after a declaration is still an import-after-declaration
  error. Each condition follows D21.2. A reached ordinary name is unavailable because the compiler
  has not collected declarations. `$cfg` remains available. Short-circuit evaluation does not
  resolve an unselected name or evaluate an unselected trap.

  The compiler selects the branch before it resolves an import path. An inactive import causes no
  file probe, source read, module identity, binding, duplicate-binding, or cycle operation. A
  selected import has the ordinary D9.3 semantics. It alone contributes its module and binding to
  the closure, later passes, generated LLVM IR, and identifier index. The AST retains both
  branches. Lexical and syntax diagnostics still report from both branches. The D21.2 limit of 256
  conditions applies without change.

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
