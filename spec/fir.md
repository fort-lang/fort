# FIR: the fort intermediate representation

This document specifies FIR, the representation that stands between the checker and the LLVM IR
text of `toolchain.md` 6. D19.8 is the decision that owns it. Where this document and
`decisions.md` disagree, `decisions.md` wins (D1.2).

Status: implemented. Written 2026-09-28 as a design (T-209); T-213 to T-255 implemented it, and
since T-255 (2026-09-30) the translator writes every function of a build. Section 16 gives the
migration, which T-255 ended, and section 17 gives the open questions and their rulings.

## 1. Why FIR exists

FIR has two jobs.

**One derivation of each fact.** The compiler has two passes that read a function body: the
checker and the emitter. Each pass derives the facts it needs from the syntax tree. When two
passes derive one fact, the two answers can differ, and no test compares them. The project has met
this four times:

- **The parameter types of a runtime entry.** The declaration in `std/rt.ft` gives them, and each
  call site that the emitter writes gives them again. In T-072, a call passed `i32` where the
  callee takes `i64`, and `opt` accepted it.
- **Whether an expression is an lvalue.** The checker derives it (D6.7), and `is_place_expr` in
  the emitter derived it again until T-193 removed the second rule: the checker now writes
  `ANN_LVALUE`, and the emitter reads it.
- **Whether a body can fall off its end.** `check_terminates` derives it (D8.4), and the emitter's
  `terminated` flag derives it again. The emitter writes `unreachable` because `check_terminates`
  says that the body terminates, so a disagreement is undefined behavior at run time.
- **Which deferred statements run at an exit.** The emitter's scope stack derives it (D7.8), and
  the frames and joins of a linear ownership analysis on the tree would derive it again. Such an
  analysis can call a path clean that the emitted code leaks on.

**A fixed core that new language features lower into.** A feature then touches two places: the
checker, which says what the feature means, and one lowering rule, which says what the feature is
made of. The translation to LLVM and every analysis read only the core, so they do not change for
a new feature. Section 15 measures this against the features that D15 defers.

FIR follows the shape of Rust's MIR: a control-flow graph of basic blocks for each function, whose
statements read and write places, whose operands say whether they copy or move, and whose runtime
checks are explicit terminators. Its name is FIR, the fort IR.

FIR is not these things:
- It is not an optimizer. LLVM optimizes (D19.4). FIR is not in SSA form and has no `phi`.
- It is not a serialized format. It lives in memory for one analysis (D20.5). The textual form of
  section 13 exists for tests, for `--fir`, and for a reader.
- It does not replace the checker. Type errors, name resolution and constant folding stay in the
  checker (`notes/compiler.md` 4).

## 2. Terms

Each term has one meaning in this document.

- **Lowering**: the pass that makes the FIR function of one fort function from its checked tree.
- **Verifier**: the pass that tests the rules of section 10 on one FIR function.
- **Pass**: a function that reads one FIR function and either reports about it or rewrites it.
  The verifier, the build-mode pass and each flow analysis are passes.
- **Translator**: the pass that writes the LLVM IR text of one FIR function.
- **Direct path**: the emitter of today, which writes LLVM text from the tree. It exists until the
  migration ends (section 16).
- **Function**: one FIR function.
- **Local**: one storage location of a function: the return place, a parameter, a named local of
  the source, or a temporary that the lowering made.
- **Block**: a list of statements that ends in exactly one terminator.
- **Terminator**: the last operation of a block. It names the blocks that control goes to next.
- **Place**: a local or a global, followed by zero or more projections. A place designates
  storage.
- **Operand**: what an rvalue reads: `copy` of a place, `move` of a place, or a constant.
- **Rvalue**: the right side of an assignment: an operand, an operation on operands, or an
  aggregate.
- **Check**: the terminator `check` of section 8, and nothing else. Every runtime check of D10.6,
  D11.1 and D11.3 is one. The overwrite check of D17.11 is one too, but the emptiness pass proves
  it at compile time and no build keeps it (section 11). The checker is the pass that resolves
  names and types. The verifier tests rules.
- **Failure block**: an LLVM block that the translator writes for a `check` or for a `fail`
  (item 14). FIR has no block kind for it.
- **Owning type**: an `own` reference (`T* own`, `void* own`, `T@ own`, `string own`; D17.1) or an
  owning aggregate (D17.7). A value of an owning type is an owning value.
- **Writer**: a `call`, an `alloc`, a `move` operand, or a `del`. A writer can change memory that
  a place designates.
- **Abort block**: a block whose terminator is `fail`, `trap` or `unreachable`. Control never
  leaves an abort block.
- **Exit**: a `return` terminator. An abort block is not an exit.

## 3. The pipeline

For each module in the order of D9.10, and for each function in source order:

1. The checker has checked the whole closure (`toolchain.md` 2, step 3).
2. The lowering makes the FIR function from the checked tree (section 9).
3. The verifier tests it (section 10). A violation is an internal error: the compiler calls
   `panic` with the rule, the function and the block (D13.3).
4. The flow analyses read it (section 14). They see every runtime check, in every build mode.
5. The emptiness pass proves each overwrite check (section 11, D17.11). It runs in every build
   mode and under `--check`. An unproved check is a compile error.
6. The build-mode pass rewrites it for the selected mode (section 11). The verifier runs again.
7. The translator writes its LLVM text into the module (section 12).

The module-level parts of the module stay where `toolchain.md` 6 puts them: the header, the named
types, the globals, the private data, the declarations and the attribute groups. The translator
writes them. The FIR module holds the list of functions and the list of module-level declarations
with their folded values, so the translator reads FIR and the symbol table and never the tree.

FIR belongs to the analysis that made it. The compiler frees it with the analysis, before
`check_free` (`notes/compiler.md` 5), because its types and symbols point into the checker (D20.5).

## 4. Principles

Each principle states a rule and its reason.

1. **Fort types, not LLVM types.** Every local, place and operand carries a `types.node*` from
   the checker's table. The translator maps a fort type to an LLVM type by `toolchain.md` 6
   item 2. The reason: an analysis needs `own` and `mut`, which LLVM does not have (item 18).
2. **Memory is places.** Every read and write of storage names a place: a local or a global with
   projections for a field, an element or a dereference (section 5.5). A new kind of aggregate
   adds one projection, not new statements. The reason: an analysis of ownership or of definite
   assignment is a dataflow over places, with no pattern match over loads and stores.
3. **An operand says whether it copies or moves.** `move place` reads an owning value and empties
   the place (D17.6). `copy place` reads and leaves the place as it is. The reason: the ownership
   rules of D17 become rules about `move`, and an analysis follows them without a model of the
   source.
4. **A place is read when D6.3 reads it.** A place operand is read when its statement runs. When
   a writer stands between the point where D6.3 reads the place and that statement, the lowering
   copies the place into a temporary at the point of the read (section 9.4). The reason: a place
   is a name for storage, and storage can change; the order of D6.3 must not depend on what a
   callee does.
5. **The core is small, and features lower into it.** FIR has the rvalues of section 6, the
   statements of section 7 and the terminators of section 8, and nothing else. A construct of the
   language that is not in the core lowers into it (section 9). The reason: the translator and
   the analyses stop growing when the language grows.
6. **Runtime checks are terminators.** Every runtime check is a `check` that names its kind and
   branches to the block that continues (section 8). Its condition is an ordinary `bool` operand
   that earlier statements compute. The reason: an analysis sees the checks as edges, and a pass
   can remove or prove one without touching the translator.
7. **The build mode is a pass.** The lowering emits every check. The build-mode pass of section
   11 removes the ones the mode drops and rewrites the operations the mode changes. The flow
   analyses run before it. The reason: an analysis gives one answer in every mode, and the
   translator never reads the mode.
8. **Not SSA.** A local is storage that many statements may write. The translator gives a local
   an `alloca` (D19.4), or a register when the local is written once (12.1), and LLVM builds the
   SSA form. The reason: the lowering needs no `phi` and no dominance, and `&&`, `||` and `?:`
   are ordinary control flow over a temporary.
9. **No lowering rule depends on the target.** FIR depends on the target only through the source
   that `$if` selects and the values that `$cfg` gives (D21). The checker resolved both, so two
   targets can give two FIRs for one file, as they give two programs. An analysis gives one answer
   for one selected source.
10. **Statement order is evaluation order.** The lowering emits statements in the order that
    D6.3 gives, and no pass reorders them.
11. **Exits are edges.** `break`, `continue` and `return` are terminators. The deferred
    statements that an exit runs are statements on the path to its target (D7.8, section 9.5).
12. **Every statement has a location and a statement index.** Every statement and terminator
    carries the `diag.loc` of the source construct that produced it, and the index of the source
    statement whose lowering produced it (section 9.7). A statement that an expansion of a
    deferred statement produced also carries the location of the exit that expanded it. The
    reason: the translator groups the statements of one source statement without the tree.
13. **Order is creation order.** Every number in FIR (local, block) is assigned in creation order
    (D19.5). No FIR order comes from a hash table.

## 5. The data model

### 5.1 The module

A FIR module holds:
- the functions, in the order that D9.10 and source order give;
- the module-level constants and globals, each with its symbol, its fort type and its folded
  value from the checker (D7.10);
- the entry module's `main` symbol, from which the translator writes the compiler-emitted
  `main` (item 22);
- the `extern fn` symbols that the calls name, in first-use order (item 8): the functions in
  module order, and the statements of each in block order. The translator writes the `declare`
  lines from this list (12.5).

### 5.2 The function

A function holds its symbol, its fort function type, its locals (5.3) and its blocks (5.4).

### 5.3 Locals

A local has a number, a fort type, a kind, a location, and, for a parameter or a named local,
its symbol. The numbers stand in this order:

- `_0` is the return place. Its type is the result type of the function. A `void` or `noreturn`
  function has `_0` of type `void`, and nothing reads or writes it.
- `_1` to `_n` are the `n` parameters, in declaration order.
- Then the named locals of the body, in declaration order. The lowering numbers them in a
  pre-scan, the walk of `collect_locals`, which follows the selected branch of each `$if`. That
  is the order of their `alloca`s (item 10, D19.4).
- Then the temporaries of the lowering, each numbered when the lowering makes it.

A named local carries its name for the textual form and for diagnostics; the number is its
identity. A local of an aggregate type is a place. Nothing holds an aggregate as a value (D19.3).

### 5.4 Blocks

A block has a number from 0 in creation order, its statements and one terminator. `bb0` is the
entry block, and no terminator names it. A block whose only operation is `fail` is where the
translator writes a failure block (12.4). A `fail(panic, ...)` may also follow statements in an
ordinary block (item 19).

### 5.5 Places and projections

A place is a base followed by zero or more projections:

| base | spelling | meaning |
|---|---|---|
| local | `_n` | the storage of local `n` |
| global | `global m.NAME` | the storage of a module-level constant or global (D7.10) |

| projection | spelling | applies to | meaning |
|---|---|---|---|
| deref | `(*p)` | a place of pointer type | the storage the pointer designates (D6.10) |
| field | `p.k` | a place of struct, span or `string` type | field `k`, by declaration index |
| index | `p[_i]` | a place of fixed-array, span or `string` type | element `_i`; `_i` is a local |

For a span `E@` or a `string`, field 0 is the pointer, of type `E*` or `char*`, and field 1 is the
length, of type `u64`. `mut` and `own` of the span carry over to field 0, so that an `aggregate`
builds an owning span from an owning pointer. A `copy` of field 0 reads the `.ptr` of the source,
which is a view (D17.3). The copy lends, and no `cast` makes it own again, because a cast never adds
`own` (D3.14, rule V6). The `.ptr` and `.len` of the source are these fields (D6.10). An index is a
local of type `u64` and never an expression: the lowering copies an index expression into a
temporary first, so that a place holds no computation and an analysis can compare two places by
their parts. The comparison has three answers, not two:
- two places are **equal** when they have the same base and the same projections, index locals
  included, no statement assigns an index local between the two uses, and neither place contains
  a `deref` or an index on a span or a `string`. A place that reads memory to find its storage is
  never equal to another use of it: the pointer it reads can change between the two uses;
- two places are **disjoint** when they have different local bases, or the same base and a field
  projection with different field numbers at the same position, and neither place contains a
  `deref` or an index on a span or a `string`;
- in every other case two places **may overlap**. `a[_i]` and `a[_j]` may overlap, because `_i`
  and `_j` can hold one value. A place that contains a `deref`, or an index on a span or a
  `string`, may overlap every place whose storage an address reaches (section 14).

A place designates fixed storage when its base is a local, no projection is a `deref`, and no
`index` projection applies to a span or a `string`. An index on a span or a `string` reads the
pointer field first, as a `deref` does. Every other place is reached through memory that a writer
can change (principle 4). The type of a place
follows from its base and its projections. The address of a place is the rvalue `addr` (section
6). A place that names a global constant is read-only (D7.10).

### 5.6 Operands and constants

| operand | spelling | meaning |
|---|---|---|
| copy | `copy p` | the value at `p`; `p` keeps it |
| move | `move p` | the value at `p`; `p` then holds the zero value of its type (D17.6) |
| constant | `const c` | a constant (below) |

`move` is legal on a place of an owning type. `copy` is legal on any place. An assignment into a
place of an owning type takes a `move` operand, the constant `null T` or `zero T`, or an rvalue
that produces an owning value (D17.5, rule V6). A `string` or `bytes` constant is a view of
private data and owns nothing, so it fills no owning place (D17.3, D17.9). A `copy` of an owning
place lends it (D17.4).

| constant | spelling | type |
|---|---|---|
| integer | `i32 -1`, `u8 255`, `u64 0x10` | the fort integer type, `char` or an enum |
| boolean | `true`, `false` | `bool` |
| float | `f64 0x3FF8000000000000` | the bit pattern of its `double` (D19.5) |
| null | `null T` | the pointer type `T` |
| zero | `zero T` | the zero value of any type `T` (`{}`, D6.5) |
| string | `"..."` | `string`; the translator makes the `@.str.N` and the header (item 5) |
| bytes | `bytes "..."` | `char*`: the address of a private NUL-terminated copy (item 5) |
| enum table | `enum_table m.E` | `std.rt.enum_member*`: the member table of the enum (item 21) |
| function | `fn m.f` | the function type of a fort function |

## 6. Rvalues

`T` is a fort type. `a`, `b` and `n` are operands. `p` is a place.

- `a`: the operand itself; the type of `a` (item 3).
- `add(a, b)`, `sub`, `mul`: wrapping integer arithmetic; the type of `a` (item 15).
- `add_overflows(a, b)`, `sub_overflows`, `mul_overflows`: whether the operation overflows the
  type of `a`; `bool` (item 15).
- `div(a, b)`, `rem`: division and remainder, signed or unsigned by the type; the type of `a`
  (item 15).
- `and(a, b)`, `or`, `xor`, `not(a)`: bitwise operations; `not` of a `bool` is `!`; the type of
  `a` (item 15).
- `shl(a, n)`, `shr`: shifts; `shr` is arithmetic for a signed type; the type of `a` (item 15).
- `fadd(a, b)`, `fsub`, `fmul`, `fdiv`, `fneg(a)`: IEEE float arithmetic with no
  fast-math flag; the type of `a` (item 15).
- `eq(a, b)`, `ne`, `lt`, `le`, `gt`, `ge`: comparison, signed, unsigned or float by the type;
  `bool` (item 15).
- `cast<T>(a)`: the conversion of D3.14; `T` (item 12).
- `addr(p)`: the address of `p`, which is `&lv`; a pointer to the type of `p` (item 3).
  `addr mut(p)` gives a `mut` pointer, as the checker typed the `&`.
- `slice(p, lo, hi)`: the span `p[lo..hi]` of a fixed array, a span or a `string` at `p`, a view
  of the elements (D6.9, D17.3); the span type of `p`, or `string` (item 16). `slice mut(p, lo,
  hi)` of a fixed array gives a `mut` span. `mut` appears only on a slice of a fixed-array
  place. A slice of a span place or a `string` place keeps the `mut` of that place, and a
  `string` place gives a `string`.
- `slice_ptr(a, lo, hi)`: the span `a[lo..hi]` of the pointer `a`, with no check (D6.9).
- `alloc<T>(n)`: `std.rt.alloc` of `n` elements of `T`, where `n` has type `u64`; `T mut* own`
  (item 17). `T` is neither `void` nor a span (D10.2).
- `aggregate T(a, ...)`: a struct literal with every field in declaration order, an array
  literal with every element, or a span or `string` header with its pointer and its length; `T`
  (item 3). `aggregate zeroed T(a, ...)` is a designated struct literal, whose omitted fields are
  `const zero`: the translator zeroes the whole destination, padding included, after it reads
  the operands, as the direct path did until T-255 deleted it (D6.5, T-253 review round 2).
- `call C(a, ...)`: a call, where `C` is `fn m.f`, `extern name`, or an operand of function
  type; the result type of `C` (items 7, 8).

Rules:
- The two operands of an arithmetic, bitwise or comparison rvalue have one type after `mut` and
  `own` are removed. The count of a shift has the type of `a`.
- `cast<T>(a)` covers every conversion of D3.14, including the ones that print nothing and the
  casts between aggregate types. The translator chooses the instruction from the two types
  (item 12).
- A `cast` carries `own` or drops it, and never adds it (D3.14). `T` marks a level `own` only
  where the type of `a` marks that level `own` too. When `T` owns, `a` is a `move`, which carries
  the owner (D17.5): a `copy` of an owning place lends it (D17.4), and a constant owns nothing,
  as a literal gives a view (D17.3). Rule V6 tests this.
- `alloc` gives a pointer. For `new(T, n)` into a span, the lowering builds the header with
  `aggregate` (section 9.4). `std.rt.alloc` takes a file, a line and a column after the count;
  the translator adds them from the location of the statement.
- The lowering writes `const zero` for a field that a designated literal leaves out (D6.5).
- `call`: an operand of aggregate type is a place, and the translator makes the caller's copy of
  item 7. A `noreturn` callee has the result type `void`, and the block ends in `trap` right after
  the call (rule V8).
- An rvalue of aggregate type can stand only on the right of an assignment whose left side is a
  place of that type. These are the rvalues of aggregate type: `aggregate`, `slice`, `slice_ptr`,
  a `call` with an aggregate result, a `cast` to an aggregate type, `copy` or `move` of an
  aggregate place, `const zero` of an aggregate type, and a string constant.
- The translator reads every operand of an rvalue before it writes the destination, in operand
  order, and each `move` empties its place at that point. An `aggregate` whose member reads the
  place it writes therefore sees the old value: `s = pair{s.b, s.a}` swaps (open question 5).

## 7. Statements

- `p = rvalue` writes the rvalue into `p` (items 3, 10).
- `del(move p)` frees the allocation that the owning value at `p` holds and empties `p` (D17.9,
  item 17).
- `live(_n)` says that the scope of the named local `n` begins here.
- `dead(_n)` says that the scope of the named local `n` ends here. An analysis reports an owning
  local that still holds a value.

Rules:
- `del` takes a `move` operand and nothing else. `del(e)` of an rvalue lowers to a temporary and
  `del(move _t)` (section 9.4). The translator frees and zeroes as item 17 says.
- `live` and `dead` mark the scope of a named local. They do not allocate: every local has
  storage for the whole function (D19.4). The lowering writes `dead` for each local of a scope at
  each exit of that scope, after the deferred statements of that scope (section 9.5).
- The lowering puts each store of D17.11, a write into a place of an `own` reference type that
  an lvalue of the source names, right after a `check(overwrite: p)` before the build-mode pass
  (D17.11, section 9.3). The emptiness pass proves each store that needs a proof, with a check or
  with none (section 11).
- Every statement and terminator carries its location and its statement index (principle 12).
  A statement that a pass makes takes the index of the statement it replaces.
- A statement or a terminator **reads** a place when it loads the storage of that place. It
  reads the place of each `copy` and `move` operand and the place of a `del`. It reads the index
  local of each `index` projection of every place that it names. The destination of an
  assignment, the place of `addr` and the place of a `slice` of a fixed array are no reads of
  their value: the item reads the pointer that each `deref` of such a place loads, and the span
  or `string` header before each index on a span or a `string`. A `slice` of a span or a
  `string` reads its place, and `check(overwrite: p)` reads `p` to test it (section 8); section 14
  counts that test as no use of `p`. Before that test, the check reads what the address of `p`
  loads: the pointer of each `deref` and the header before each index on a span or a `string`.
  Those reads are uses. A `return` reads `_0` unless `_0` is `void`. The reads come
  in evaluation order, each index local before its place, and the pointers of a destination come
  after the right side, where the translator computes that address (12.5). A `move` and a `del`
  empty the place after the read (section 6). Rules V12 and V13 test these reads.

## 8. Terminators

- `goto bb` goes to `bb` (item 10).
- `switch(a) -> [c1: bb1, ..., otherwise: bb]` goes to the block of the constant that equals
  `a`, and else to `bb`. `a` is a `bool`, an integer, a `char` or an enum (item 10).
- `return` returns the value at `_0` (D7.11, items 7, 10).
- `check(f, kind, args) -> bb` continues at `bb` when the `bool` `f` is false, and else aborts
  with the runtime entry of `kind`: `f` is true on failure (D19.6, item 14). The kind `user` is
  the exception: its operand is the argument of the source `assert`, and it is true on success
  (D12.2).
- `check(overwrite: p) -> bb` says that the owning value at `p` is null, and it continues at `bb`
  (D17.11, item 18). It is a compile-time check: the emptiness pass proves it or reports the error
  of D17.11, and the build-mode pass replaces it by `goto` in every mode (section 11). It has no
  runtime entry. It names the place, because its condition is the null test of the place itself.
- `fail(kind, args)` aborts with the runtime entry of `kind` (items 14, 19).
- `trap` is the trap of D8.5 (item 20).
- `unreachable` ends a body that the checker proved cannot fall off its end (D8.4, item 10).

The kinds, the runtime entry each calls, the operands it reports, and the build mode that removes
the check:

| kind | entry | operands reported | removed by |
|---|---|---|---|
| `overflow` | `fail_overflow` | none | release |
| `shift` | `fail_shift` | the count as `i64`, the type name as `bytes` | release, which masks it |
| `div_zero` | `fail_div_zero` | none | nothing |
| `div_overflow` | `fail_div_overflow` | none | nothing |
| `bounds` | `fail_bounds` | the index as `i64`, the length as `u64` | `--no-bounds-check` |
| `span` | `fail_span` | `lo` and `hi` as `i64`, the length as `u64` | `--no-bounds-check` |
| `alloc_count` | `fail_alloc_count` | the count as `i64` | nothing |
| `overwrite` | none | none | every mode, after the emptiness pass proves it |
| `enum` | `fail_enum` | the value as `i64`, the enum name as `bytes` | nothing |
| `user` | `assert_fail` | the source text of the argument as `bytes` | nothing |
| `panic` | `panic` | the pointer and the length of the message | nothing |

Every runtime entry of the table also takes a file, a line and a column, which the translator
adds from the location of the terminator (D11.4). The lowering computes the condition of a
`check` with ordinary rvalues: `ge` against the length for `bounds`, `add_overflows` for
`overflow`, `eq` against zero for `div_zero`.

A `switch` on an enum without a `default` clause names as `otherwise` a block that holds only
`fail(enum, ...)` (D7.7). `fail(panic, ...)` is the lowering of the builtin `panic`. Every other
`fail` stands alone in a block that a `switch` names.

## 9. Lowering

The lowering reads the checked tree of one function and the checker's annotations. It runs inside
the window of `notes/compiler.md` 5. It emits statements in evaluation order (D6.3) and numbers
locals and blocks as section 5 says.

### 9.1 What the lowering reads

From the checker:
- `node.ty`: the fort type of every expression.
- `node.sym`: the symbol of every name: a local, global, function, extern, field or enum member.
- `ANN_CONST` and `check_node_value`: a folded constant becomes a `const` operand.
- The `is_noreturn` mark of a callee's fort function type (D8.5).
- `ANN_MOVE`: the implicit move of `return x` (D17.5), and a call of the builtin `move`.
- `ANN_LVALUE`: whether an expression designates a place (D6.7). T-193 added it.
- `FLAG_SELECT_THEN`: the selected branch of a statement `$if` (D21.2).

From the parser: the kind of each node, its children, its `op` and its `name`; `FLAG_DEFAULT`,
`FLAG_DESIGNATED` and `FLAG_VARIADIC`; and the source text of the argument of `assert`.

The lowering reads no other fact. A fact that it needs and that neither the parser nor the
checker records becomes a new annotation, not a second derivation.

### 9.2 The entry block

1. For each named local whose type is an `own` reference: `_n = const zero T`. The emptiness
   pass reads this store as the entry zeroing of D17.11, so the first execution of the declaration
   passes its overwrite check (section 11).
2. The body, lowered as a scope of kind `fn` (section 9.5).

A parameter needs no statement. The translator stores each scalar parameter into its `alloca`
(item 10) and gives an aggregate parameter the caller's copy as its storage (item 7).

### 9.3 Statements

- **A block**: a scope of kind `block` (9.5). After a terminator, each later statement starts a
  new block that nothing names (item 10).
- **`T x = e;`**: `live(_x)`, then `e` into `_x`. For an `own` reference type: `e` into a
  temporary `_t`, `check(overwrite: _x) -> bb` at the declared name, and `_x = move _t` in `bb`
  (item 18).
- **`lv = e;`**: the place `p` of `lv` first, with its index temporaries and their checks
  (D6.3). When `e` contains a writer and `p` does not designate fixed storage (5.5), the lowering
  holds `_a = addr mut(p)` before `e` and writes `(*_a)` in place of `p`; the emptiness pass
  reads `(*_a)` as `p` (section 11). Then `e` into `p`. For an
  `own` reference type: `e` into a temporary, `check(overwrite: p)`, then `p = move _t`. The check
  and the store stand at the `=` (D17.11). The store of a scalar `e` stands at the `=` too, as
  section 13 stores `*p = 7`. An aggregate `e` writes into `p` at its own node.
- **`lv op= e;`**: the place `p` as above, then `_old = copy p`, then the operand `a` of `e`,
  then `p = op(copy _old, a)` with the checks of `op` before it (9.4). The old value is read
  before `e` (D6.3).
- **`lv++;` and `lv--;`**: as `lv += 1` and `lv -= 1`.
- **A call statement**: `_t = call ...` into a temporary that nothing reads; a `void` result needs
  no place.
- **`if`**: the condition into an operand, `switch(a) -> [true: then, otherwise: else]`, the
  two arms, and `goto done` from each arm that does not end in a terminator. Without `else`,
  `otherwise` is `done`.
- **`while`**: the blocks head, body and done; `goto head`; the condition in head; the body as a
  scope of kind `loop` whose `break` goes to done and whose `continue` goes to head.
- **`do ... while`**: the blocks body, test and done; `break` goes to done, `continue` to test.
- **`for`**: init in the current block; the blocks head, body, step and done; an empty condition
  is `goto body`; `continue` goes to step. A scope of kind `block` holds the whole `for`, so the
  local that init declares lives once for the loop and its `dead` stands in done (9.5).
- **A range `for`**: the collection into a place before the loop (D7.5, D17.10).
  An owning fixed array stays in its place. Another fixed array goes into a value copy.
  A span or string header goes into a temporary with its outermost `own` removed.
  That header copy holds the pointer and length once, and lends the collection storage.
  The checker refuses operations that invalidate that storage inside the body (D17.10).
  A counter `_i: u64 = const u64 0`; the
  blocks head, body, step and done; `_c = lt(copy _i, len)` and `switch(copy _c)` in head, where
  `len` is the constant length of an array or `copy c.1` of a span or `string`; `live(_x)` and
  `_x = copy c[_i]` in body; the body as a scope of kind `loop`; `_i = add(copy _i, const u64 1)`
  in step. The counter never passes the length, so its `add` needs no check. `live(_x)` stands
  inside the scope of kind `loop` of the body, so each exit of the body ends `x` (9.5).
- **`switch`**: the operand into an operand; for an enum `switch` without a `default` clause,
  first a block holding `fail(enum, _v, const bytes "<name>")`, where `_v = cast<i64>(a)`
  stands before the `switch`; then one block for each clause in clause order, then done;
  `switch(a) -> [labels: blocks, otherwise: default, the fail block, or done]`. The fail block
  takes the smallest number, so the translator writes it before the failure blocks of the
  clauses (item 14). Each clause is a scope of kind `case` whose `break` goes to done.
- **`break` and `continue`**: the unwind of 9.5, then `goto` the target.
- **`return`**: section 9.6.
- **`defer s`**: no statement. The lowering records `s` in the innermost scope (9.5).
- **A statement `$if`**: the selected branch as a scope of kind `block`, and nothing for a
  branch that the checker did not select (D21.2).
- **An error node**: the lowering never meets one. The compiler emits no module for a file with
  a syntax error (D14.2).

A construct makes its blocks when control reaches it, in the order that this section names them:
after the condition of an `if`, the init of a `for`, the collection and the counter of a range
`for` and the operand of a `switch`, and before the condition of a `while` and the body of a
`do`; so the blocks that a condition or a body makes take later numbers. The statements and the
terminators that an `if`, a loop or a `switch` makes for itself stand at its keyword with its
index (9.7), `break` and `continue` stand at their keyword with their own index, and in a range
`for` `live(_x)` stands at the name and `_x = copy c[_i]` at the collection.

### 9.4 Expressions

The lowering of an expression gives an operand, a place, or writes into a place. A scalar expression
gives an operand: a `const` for a folded constant, `copy p` for a place, and a temporary
`_t = rvalue` for an operation. An aggregate expression writes into the place that waits for it. A
place expression gives a place. The rules below write a nested rvalue, such as `not(and(...))`, as
shorthand for one temporary for each inner rvalue, and a bare `"..."` as shorthand for
`const bytes "..."`. An owning value in a temporary enters an owning place, parameter, field,
element or cast as `move _t` (D17.5). Every temporary that holds a `copy` of an owning place takes
the lent type of that place (D17.4, rule V6). The lent type is the type of the place with `own`
removed at the top level. It also drops `own` at each level that the place stores inline: the
elements of a fixed array, at every rank. `own` behind a pointer or a span stays, because the copy
lends the reference and not what the reference owns. So `u8 mut* own[2]` gives `u8 mut*[2]`, and
`u8 mut* own* own` gives `u8 mut* own*`. A struct type has no form without the `own` of its
fields. So the checker lends no struct that owns by value, and no fixed array of such structs
(D17.4). It refuses a copy of either and asks for `move`, so no temporary holds such a copy. The
one exception to the lent type is the temporary of `return` in 9.6 step 1, which takes the type
of `_0`.

**The materialization rule** (principle 4). The lowering lowers the operands of one rvalue or call
left to right. When an operand is `copy p` or `move p` and a later operand of the same rvalue or
call contains a writer, the lowering writes `_t = copy p` or `_t = move p` at that point and passes
`copy _t` or `move _t` instead. For `_t = copy p`, `_t` takes the lent type of `p` as above,
for a parameter and for an operand of an rvalue alike. The same holds for the callee operand of
an indirect call. A place of fixed storage whose base is a temporary is never held, because the
lowering takes the address of no temporary's own storage (5.5, D6.3). Example:
`G + bump()` lowers to `_1 = copy global G`, `_2 = call fn m.bump()`, `_3 = add(copy _1, copy _2)`,
so `G` is read before `bump` runs.

- **A folded constant**: `const`.
- **A name**: the place `_n` or `global m.NAME`; a function name is `const fn m.f`.
- **`a.f`, `p->f`, `s.len`, `s.ptr`**: the projections `.k` and `(*p).k`. The checker folds
  `.len` of a fixed array.
- **`*p`**: `(*_p)`, where `_p` holds the value of `p`.
- **`&lv`**: `addr(p)`.
- **`a[i]`**: `_i = cast<u64>(i)`, where the cast sign-extends a signed index, so that a negative
  index fails the one unsigned compare (D6.8); `_f = ge(copy _i, len)`; `check(copy _f, bounds,
  cast<i64>(copy _i), len) -> bb`; then the place `a[_i]` in `bb` (item 16). D6.3 evaluates `a`
  to its address before `i`. When `i` contains a writer and the place `p` of `a` reads memory to
  find its storage (5.5), `_a = addr(p)` holds that address first (D6.3). It is `addr mut(p)`
  when that storage is mutable. The place is then `(*_a)[_i]`. The header of a span or a `string`
  and the element are read after `i`, where they are used, with or without a deref (D6.3).
- **`a[lo..hi]`** on an array, a span or a `string`: `_lo = cast<u64>(lo)` and `_hi =
  cast<u64>(hi)` as for an index, with `0` and the length for an absent bound; `_f = or(gt(copy
  _lo, copy _hi), gt(copy _hi, len))`; `check(copy _f, span, cast<i64>(copy _lo), cast<i64>(copy
  _hi), len)`; then `slice(a, copy _lo, copy _hi)` into the destination (item 16). When a bound
  contains a writer, the place of `a` is held as for an index (D6.3). On a pointer:
  `slice_ptr(a, lo, hi)` with no check (D6.9). A stored span or `string` header is read after
  the explicit bounds. An absent upper bound uses the length from that read (D6.3).
- **`-x`**: `sub(const 0, x)` with the overflow check for an integer; `fneg(x)` for a float.
  `!x` and `~x`: `not`.
- **`x + y`, `x - y`, `x * y`** on integers: `_f = add_overflows(a, b)`, `check(copy _f,
  overflow) -> bb`, then `_t = add(a, b)` in `bb`. `+%`, `-%` and `*%` are `add`, `sub` and `mul`
  with no check (D11.1). On floats: `fadd`, `fsub`, `fmul`.
- **`x / y`, `x % y`** on integers: `_z = eq(b, const 0)`, `check(copy _z, div_zero) -> bb1`; for
  a signed type, `_o = and(eq(b, const -1), eq(a, const MIN))`, `check(copy _o, div_overflow)
  -> bb2`; then `div(a, b)` or `rem(a, b)` (D6.13). On floats: `fdiv`; `%` is an error on a float
  (D6.12).
- **`x & y`, `x | y`, `x ^ y`**: `and`, `or`, `xor`.
- **`x << n`, `x >> n`**: `_n = cast<i64>(n)` by its signedness, `_u = cast<u64>(copy _n)`,
  `_f = ge(copy _u, const u64 W)`, `check(copy _f, shift, copy _n, "<type>")`, then
  `shl(a, cast<T>(copy _n))` or `shr` (item 15).
- **Comparison**: `eq` to `ge`. A `string` equality is `call fn std.rt.str_eq(copy a.0, copy
  a.1, copy b.0, copy b.1)`, and `!=` is `not` of it. A string constant operand goes into a
  temporary `_t = const "..."` first, since `str_eq` reads the fields of a place (D19.3).
- **`&&` and `||`**: a temporary `_t: bool`; `_t = a`; `switch(copy _t) -> [true: rhs,
  otherwise: done]` for `&&` and the reverse for `||`; `_t = b` in rhs; `goto done`.
- **`c ? a : b`**: the condition; `switch`; each arm into the destination, a temporary for a
  scalar; `goto done` from each arm.
- **`cast(e, T)`**: `cast<T>(a)`, where `a` is the operand of `e`: `move p` when `e` is
  `move(lv)`, `move _t` when `e` is an owning rvalue in a temporary, and `copy p` or a constant
  otherwise. A cast between aggregate types goes into the destination. A cast of a place to a
  type that does not own lends the place (D3.14, D17.4, D17.12). A cast never adds `own`
  (D3.14), so a cast to a type that owns takes a `move` operand. The checker lets such a cast
  take only `move(lv)` or an owning rvalue. A constant of an owning type, the `const zero T` of
  `T{}`, goes into a temporary first: `_t = const zero T`, then `cast<T>(move _t)`.
- **`sizeof(T)`**: a folded constant.
- **`new(T)`**: `alloc<T>(const u64 1)`.
- **`new(T, n)`**: `_n = cast<i64>(n)` by its signedness; for a signed count `_f = lt(copy _n,
  const i64 0)` and `check(copy _f, alloc_count, copy _n)`; then `_u = cast<u64>(copy _n)`,
  `_p = alloc<T>(copy _u)` and `dst = aggregate T mut@ own(move _p, copy _u)`.
- **A call**: the callee first when it is a value (D6.3), then each argument left to right into
  an operand under the materialization rule, then `call`. An owning value in a temporary enters
  an `own` parameter as `move _t`; an owning place enters a parameter that does not own as `copy
  p` (D17.4, D17.5). An argument of an aggregate type is always a place: a string constant or a
  literal goes into a temporary first (D19.3, rule V7). A `noreturn` callee ends the block with
  `trap` after the call.
- **`move(lv)`**: the operand `move p`.
- **`del(e)`**: `del(move p)` for a place (`ANN_LVALUE`); for an rvalue, `_t = e` then
  `del(move _t)`. When the lowering of `e` already put the owning value in a temporary, that
  temporary is `_t`. The `del` stands at the name `del`, as section 13 shows.
- **The print family**: `fd` into an operand once, then one `call fn std.rt.print_*` for each
  argument, left to right, with the conversions of item 19: an enum as `cast<i32>(a)`,
  `const enum_table m.E` and the member count; a `string` as its two fields. `println` ends with
  `print_char` of `const char 10`. A `string` place gives `copy p.0` and `copy p.1` (D12.2). A
  folded string constant gives `const bytes "..."` and `const u64` of its length, with no
  temporary (D12.2).
- **`assert(c)`**: the operand of `c`, then `check(a, user, "<text>") -> bb`.
- **`panic(m)`**: the pointer and the length of `m`, as the print family takes a `string`, then
  `fail(panic, ptr, len)` (D12.2). The `check` of `assert` and the `fail` of `panic` stand at the
  name of the builtin, which the runtime reports (D11.4).
- **`T{...}`, `T[N]{...}` and `{...}`**: `const zero T` for `{}`, else `aggregate T(...)` with
  each member as an operand, in declaration or index order, and `const zero` for a field a
  designated literal leaves out (D6.5). A designated literal is `aggregate zeroed T(...)`. A
  member that is an operation goes into a temporary first. A member for a field or element of
  an owning type enters the aggregate as `move`: `move _t` for a temporary that holds an owning
  value, `move p` for a member written `move(lv)`. A
  string constant member stands as `const "..."`, and an empty literal `{}` or `T{}` as `const
  zero T` (D6.5). Any other member of an aggregate type is a place (D19.3). Every other member
  enters as `copy` or as a constant. A member that goes into a new temporary of a type with
  padding, such as a call, a `?:` or a positional literal, is built in a temporary that
  `_t = const zero T` zeroes first; so is the operand of a cast between aggregate types. The
  direct path built such a value in its destination and left the padding of the destination
  as it was. That padding is zero in a designated literal (D6.5), and in the field that a
  designated literal passes as the `sret` pointer of a call. So the translator leaves zero
  padding wherever the direct path did. A designated literal and a cast write every byte of their
  temporary, so they take no zero first, and the argument of a call takes none, since the
  direct path built it in a temporary too.

### 9.5 Scopes and deferred statements

The lowering keeps a stack of scopes. A scope has a kind (`fn`, `loop`, `case` or `block`), the
named locals declared in it, and the deferred statements that the walk met in it, in text order.

- A block pushes a scope when it starts and pops the scope when it ends.
- `defer s` appends `s` to the innermost scope. It emits nothing (D7.8).
- `live(_x)` appends `x` to the named locals of the innermost scope. So an exit ends no local
  that is declared after it (D7.9), and the `dead` markers of one scope follow the order of the
  declarations.
- When control falls off the end of a block, the lowering lowers the deferred statements of that
  block's own scope, the last one first, then a `dead` for each local of the scope.
- An exit walks the stack from the innermost scope outward. For each scope, it lowers the deferred
  statements of that scope, the last one first, then a `dead` for each of its locals. It stops
  after the scope that the exit leaves: `return` after the `fn` scope, `continue` after the
  innermost `loop` scope, and `break` after the innermost `loop` or `case` scope. It then writes
  its terminator.
- While it lowers a deferred statement, the lowering raises a floor to the current depth. An exit
  inside the deferred statement never walks below the floor. The checker refuses `return` there,
  and a `break` or `continue` with no target inside the deferred statement (D7.8). So an exit that
  reaches the floor is an internal error.
- A deferred statement that ends its block (a call of a `noreturn` function) stops the expansion.

Each expansion is a new lowering of the deferred statement. Its statements carry the location of
the deferred statement and the location of the exit that expanded it (principle 12). A fall-off
stands at the last character of its block: the closing brace, or the last token of a clause. The
`dead` markers of a fall-off stand there, and the statements of its expansion carry that location
as the location of their exit. The scope around a `for` falls off at the closing brace of the
body. When an expansion holds an expansion of its own, the inner statements carry the location
of the inner exit.

### 9.6 Return and the end of a body

`return e`:

1. `e` into `_0`, except for an aggregate result when a deferred statement is open at this
   `return`: then `e` into a temporary `_t` of the type of `_0`, because `_0` of an aggregate
   type is the caller's storage (item 7) and the deferred code must not change the result
   (D7.8). A scalar `_0` is private to the function. For `ANN_MOVE`, the operand is `move _x`
   (D17.5), so the deferred code sees the zero value (D7.8).
2. The unwind of 9.5.
3. `_0 = move _t` when `_0` owns, and `_0 = copy _t` when it does not, at the `return`.
4. `return`.

At the end of the body, when the last block has no terminator, the fall-off rule of 9.5 first
lowers the deferred statements and the `dead` markers of the `fn` scope. Then a `void` function
ends with `return`, a `noreturn` function ends with `trap` (D8.5, item 20), and any other
function ends with `unreachable`, which rule V9 tests.

### 9.7 The statement index

The index of principle 12 comes from one counter for each function. The counter starts at 1.
It advances when the walk meets a node of one of these kinds of `src/fort/ast.ft`, and it gives
that node the new value: `k_var_decl`, `k_assign`, `k_incdec`, `k_call_stmt`, `k_if`, `k_while`,
`k_do_while`, `k_for`, `k_range_for`, `k_switch`, `k_defer`, `k_return`, `k_break` and
`k_continue`. `k_block` and `k_compile_if` only group other statements and advance nothing. The
tree decides, not the grammar: each `else if` is a `k_if` node of its own and takes its own
index, and the init and the step of a `for` are nodes of their own and take their own indices.
The walk meets the step of a `for` after the body, where the step block stands, so the step takes
its index after the statements of the body. A nested statement advances the counter when the walk
meets it, so an `if` takes an index and each statement of its arms takes a later one.
An expansion of a deferred statement lowers the
deferred statement again and advances the counter at each expansion, once for each statement
node inside it, so `defer { a(); b(); }` takes two new indices at each exit, and its inner
statements take none at the `defer` itself, which has its own index as a `k_defer` node. A
statement that a pass makes takes the index of the statement it replaces.

Two kinds of statement have no source statement of their own:
- A `dead` marker takes the index of the exit that produced it. At a fall-off, it takes the index
  of the innermost enclosing node that has one: the `if`, the loop or the `switch` whose block
  ends, or, for a bare block and a `$if` branch, the statement around them. In the block of a
  deferred statement, that is the exit that expands it. A bare block directly in the body has no
  such node, and its `dead` markers take index 0.
- Index 0 marks what the lowering emits outside the walk of the body: the zeroing of the entry
  block (9.2), and the `dead` markers and the terminator at the fall-off of the body (9.6).

### 9.8 What the lowering cannot lower

When a function holds a construct that the lowering does not support, the lowering stops for
that function and reports "not supported". The report names the kind of the tree node and the
location of the node: the token where the parser starts it, which is the operator of a binary,
unary or field node and the `(` of a call. `--fir` prints the report as the comment
`// not supported: <kind> at <line>:<col>` in place of the function. During the migration, the
direct path then wrote that function (section 16). Since the migration ended (16.6), a build
reports a compile error at that location and writes no module (exit 1, `toolchain.md` 1). The
error names the function and the kind: ``cannot lower `<f>` to FIR: the lowering does not
support the node `<kind>` ``. When the construct is a print or a string equality whose `std.rt`
function the closure lacks or declares with another signature, the error names that function
instead. A function that the closure lacks gives ``the runtime `std.rt` has no entry `<name>`,
which `<f>` needs``. A function with another signature gives ``the runtime `std.rt` has no entry
`<name>` with the parameters that `<f>` needs``. A print function or `str_eq` has another
signature in each of these cases:
- its parameters differ from those of `toolchain.md` 5.1 in count or in type;
- a pointer parameter has a `mut` or `own` mark that `toolchain.md` 5.1 does not give it;
- it is `noreturn`;
- it is `str_eq`, and its result is not `bool`.

The result of a print function that returns does not count.

A function whose `check` or `fail` names a runtime entry that the closure lacks, or declares with
other parameters than rule V7 requires, is a compile error too, at the name of the function:
``the runtime `std.rt` has no entry `<name>` with the parameters that `<f>` needs``. A
`check(overwrite: p)` names no runtime entry. Only a `check` that the build-mode pass of the
selected mode keeps (section 11) needs its entry: before the pass, the verifier excuses a missing
entry of such a check of a kind that the mode removes, and the pass removes those checks. After
the pass, the verifier excuses no kind. A `fail` always needs its entry. Only a stub `std.rt` of
a test or an old `--std-dir` meets either error of a runtime entry. The compiler tests the entries
of these errors alone: the print functions, `str_eq` and the entry of each check kind. It assumes
the other `std.rt` functions that the translator calls, such as `alloc`, `free`, `args_init`,
`args` and `flush_all`.

## 10. The verifier

The verifier runs on every function after the lowering and after every rewriting pass. It stops
at the first violation with `panic`. The panic names the rule, the function, the block and the
statement index.

- **V1**: every block ends in exactly one terminator, and no statement follows it (D19.5).
- **V2**: every block that a terminator names is a block of the function other than `bb0`
  (item 10).
- **V3**: every place is well formed: a `deref` projection applies to a pointer type, a `field`
  projection to a struct, span or `string` with that field, an `index` projection to a fixed
  array, a span or a `string`, and the index of an `index` projection is a local of type `u64`
  (5.5). No statement writes a place that names a global constant and reaches no memory through
  an address: the place of an assignment, of a `del` or of a `move` operand (5.5, D7.10, D5.9,
  D17.6).
  No statement writes `_0` of type `void` (5.3).
- **V4**: the two sides of an assignment agree in type, where two fort types agree when they are
  equal after `mut` and `own` are removed (D5.4, D17.4), except that a `cast` to an aggregate
  type agrees with its destination by the rule of D3.14; the operands of each rvalue have the
  types section 6 requires; the operand of `switch` and of `check` has the type section 8
  requires. No two arms of one `switch` have one value, because an LLVM `switch` takes each case
  value once (12.4). A `check` of the kind `overwrite` has the place form `check(overwrite: p)`
  and not the operand form (section 8). `addr` takes a place whose type is not `void`, which has
  no storage (5.3). `alloc` takes a type that is neither `void` nor a span (D10.2).
- **V5**: `move` applies only to a place of an owning type (5.6); `del` takes a `move` operand
  whose type is an `own` reference (D17.9).
- **V6**: an assignment into a place of an owning type takes a `move` operand, `null T`, `zero T` or
  an `alloc` (D17.5). It also takes an `aggregate`, `cast` or `call` rvalue whose type owns. An
  argument for a parameter of an owning type takes a `move` operand, `null T` or `zero T`. So does a
  member of an `aggregate` for a field or element of an owning type. A `string` or `bytes` constant
  is a view and fills none of these (D17.3, D17.9). A `copy` of an owning place never flows into a
  place, a parameter, a field or an element of an owning type (D17.4). No `copy` and no `move`
  adds `own` at any level. The position that the operand fills is a place, a parameter, a field
  or an element. Its type marks a level `own` only where the type of the operand marks that level
  `own` too (D3.14, D17.4). This holds whether the position owns or not. A `copy` lends and a
  `move` carries what its place owns. So `copy _4` of a `u8 mut**` fills no `u8 mut* own*`. A
  constant is no place, and its kind decides where it stands. A `cast` never adds `own`
  (D3.14), whatever place it fills. Its target marks a level `own` only where the type of its
  operand marks that level `own` too. A target that owns takes a `move` operand, which carries
  the owner (D17.5). A `copy` of an owning place lends it, field 0 of a span or a `string`
  included (5.5), and a constant owns nothing, so a `cast` of either to a type that owns adds
  `own`. An owning value lands only where something owns it (D17.8). These positions own: a
  place, a parameter, a field or an element of an owning type. The operand of a `cast` to an
  owning type owns too, and so does a `del`. An rvalue that produces an owning value fills no
  place that does not own. A `move` operand stands in no other position. Examples are an operand
  of `eq`, an argument for a parameter that does not own, and an operand of a terminator.
- **V7**: every `call` has the argument count of its callee's fort type, or at least that count
  for a variadic extern, and every argument agrees with its parameter as V4 defines agreement
  (items 7, 8). The reported operands of a `check` and a `fail` agree with the parameters of the
  runtime entry of its kind, the location parameters excluded. Before the build-mode pass of a
  build, a `check` of a kind that the mode removes needs no fitting entry, and a
  `check(overwrite: p)` needs none at any point; after the pass, no `check(overwrite: p)` remains,
  and every check needs its entry (9.8).
- **V8**: a `call` of a `noreturn` function is the last statement of its block, and the block
  ends in `trap` (D8.5).
- **V9**: no path from `bb0` reaches a block that ends in `unreachable` (D8.4). On a `switch`
  whose operand is a constant, the path takes only the edge that the constant selects.
- **V10**: `live(_n)` and `dead(_n)` name a named local.
- **V11**: the continuation block of a `check` has exactly one predecessor, the `check`.
- **V12**: no statement or terminator reads a temporary after a `move` of it with no assignment
  of it in between, on any path. An item reads a temporary when it reads a place whose base is
  that temporary (section 7). A `move` or a `del` of a place that designates fixed storage and
  whose base is the temporary is a move of it. An assignment of it writes the whole temporary.
- **V13**: every read of a temporary is preceded, on every path from `bb0`, by an assignment of it.
  A named local needs no such rule: the checker requires an initializer (D7.1), and the entry block
  zeroes every `own` reference (9.2). The rule matters most for a temporary that the translator
  keeps in an `alloca` (12.1): a read before any assignment would read undefined memory, and
  LLVM's verifier does not report it. The verifier tests V13 with a forward dataflow over the
  blocks: the set of temporaries assigned on every path, joined by intersection. `bb0` starts with
  the empty set and every other block with the set of all temporaries, so a block that no edge
  reaches, such as the block after a terminator (D14.2), passes. V13 follows every edge, the edge
  that V9 prunes on a constant `switch` included.
V9 closes the termination gap of section 1. The direct path wrote `unreachable` at the end of a
body because `check_terminates` said that the body terminates, and the translator writes the
`unreachable` that the lowering puts there for the same reason. With V9, a disagreement is an
internal error at compile time, not undefined behavior at run time. The constant clause exists
because `while (true)` lowers to `switch(const true)`, and D8.4 counts that loop as terminating.
`check_terminates` accepts a literal `true` in a `while`, a `for` with no condition, and a
`switch` that has a `default` clause or names every member of its enum, whose clauses all end
in a terminating statement, and that no `break` targets. T-210 added the last condition: before
it, a `break` inside such a clause left the `switch`, the body fell off its end, and V9 reported
it. The implementation must measure V9 over the corpus before it trusts the rule (section 16).

V7 closes the gap of T-072. The lowering takes the fort type of a runtime entry from the
declaration in `std/rt.ft` that the checker checked. The declaration and each call then have one
source (open question 2).

## 11. Passes

A pass reads one FIR function. A reporting pass writes diagnostics or a table. A rewriting pass
changes the function, and the verifier runs after it. The passes run in this order:

1. The verifier (section 10).
2. The flow analyses (section 14), which report and never rewrite.
3. **The emptiness pass**, a reporting pass (D17.11). The modules `owner.place`, `owner.prover`
   and `owner.report` (`src/fort/owner/`) hold it (`spec/ownership-design.md` 4.1). It runs in
   every build mode, under `--check` and under `--fir` (`toolchain.md` 1). It proves each store
   that needs a proof (**Stores**) and each `check(overwrite: p)`, and it reports the error of
   D17.11 for each one that it cannot prove. A function with an error reaches no later pass, and
   the run writes no module and no FIR.
   The leak rule of D17.20 uses the facts of this pass at each `dead` marker, at each `return`,
   and at each `del` and `own` cast of a pointer to owners. A later change adds it to this pass, as
   a report first and as an error after that.
   `--fir-verify-report` and the pass `build-mode` of a FIR test run it too, for its proof, and
   report nothing.
   The pass is the forward must-empty analysis of D17.11, and it reads FIR this way:
   - **Facts.** A fact says that a tracked place holds the zero value. A tracked place is a local
     `_n` or a global, followed by `field` and keyed `index` projections. It is also a local `_p` of
     pointer type that is not memory, a parameter included, followed by one `deref` and then `field`
     and keyed `index` projections. The fact is keyed by the base, the projections and the keys of
     the indexes. A tracked place below `p` starts with `p` and continues with `field` projections
     and `index` projections on fixed arrays only. A local `_n` is memory when an `addr`, or a
     `slice` of a fixed array, in the function names a place whose base is `_n` and that contains no
     `deref` and no `index` on a span or a `string`: a place that designates fixed storage of `_n`
     (5.5). Any other address names no byte of `_n`. The return place `_0` of an aggregate type is
     memory too, because its storage is the destination of the caller (5.3). A place reached through
     memory is a tracked place of a memory local or of a global, or a tracked place that contains a
     `deref` or an `index` on a span or a `string`.
   - **Keys.** The lowering copies each index into a temporary (5.5), so FIR names an element
     `p[_t]`, and two uses of one source index name two temporaries. The pass maps `_t` to a key.
     The key is the local `_k` when exactly one assignment writes `_t`, that assignment is
     `_t = copy _k` or `_t = cast<u64>(copy _k)`, `_k` is a local that is not memory, and no
     statement stores into `_k` on a path from that assignment to the use of `_t`. The key is
     the constant `c` when the one assignment of `_t` is `_t = const c`, with `c` an integer
     constant of any integer type, or `_t = cast<u64>(const c)`. Two constant keys are equal when
     their values are equal. A temporary that is memory has no key. An index that is itself a
     named local or a parameter that is not memory is its own key; the return place `_0` is no
     key. An `index` whose local has no key makes its place untracked.
   - **Separate.** Two places with one base are separate when neither has a `deref` or an `index`
     on a span or a `string` after its first projection, and, at the first projection where they
     differ, both projections are `field` projections. After such a projection a place can name
     any storage: `(*(*_1).1).0` (`p->next->g`) and `(*_1).0` (`p->g`) are not separate. This is
     not the "disjoint" of 5.5. Two `index` projections differ when their keys differ, and an
     `index` with no key differs from every `index`.
   - **Fresh.** A pointer local `_n` is fresh at a point when, on each path to that point, the
     last store into `_n` writes a fresh `new`, and after that store no `copy` or `move` operand
     whose place is `_n` itself reads it and no `addr` or `slice` names a place whose base is
     `_n`. The analysis computes freshness with the facts, by the same join. A place through a
     fresh `_n` is `(*_n)` followed by `field` projections and `index` projections on fixed
     arrays only. The rule assumes no access through a dangling pointer (D17.11).
   - **Held address.** When the right side of `lv = e` has a writer, the lowering holds
     `_a = addr mut(p)` and writes `(*_a)` in place of `p` (9.3). The pass reads the place
     `(*_a)` of that `check(overwrite: (*_a))` and of that store as `p`, for the fact, the kill
     rules and the gen rules. This holds when `_a` is a temporary that exactly one assignment
     writes, `p` designates no fixed storage, `_a` is no memory, and only that check and that
     store read `_a`. That `addr` makes no local memory and ends no freshness. An `addr mut` of
     fixed storage is no held address: it makes its local memory, so a store through `*&x`
     needs a fact of that place, which no gen rule gives (D17.11). Two kinds of statement make
     `_a` stale until its own store. The first is a store into a local that the address of `p`
     reads, its base or an index local, as a store into the source of an index temporary is
     (**Keys**). The second is a statement that can write memory: a `call` rvalue, a store
     through a pointer, or a store straight into a place of a memory local or of a global
     (**Kill**). An `index` on a span or a `string` reads the header of the span or the
     `string`, and that header can be in memory, so such a statement can change the place that
     `p` names with no store into a local. The second kind makes each held address stale, also
     one whose place reads no header in memory. A `del` and a `move` operand make no held
     address stale. A `move` of a header writes the zero value, whose length is 0, so each later
     `index` through it is out of bounds until a store or a call writes the header again. A `del`
     of a header also frees the storage that `_a` names, so the store through `(*_a)` writes
     through a dangling pointer. A `move` of field 0 of a header leaves `null` with the length,
     so a later `index` through it dereferences `null`. D17.11 assumes that each index stays in
     bounds (D10.6) and that no operation accesses storage through a dangling pointer or through
     `null` (D10.7), so these cases are outside its guarantee. A stale `_a` gives no tracked
     place, so the pass proves no check of `(*_a)`, and no item gives `(*_a)` a fact. The store
     through it kills as a store through a pointer and keeps no fact through a fresh pointer
     local, because the `addr` ended no freshness.
   - **Values.** A store writes a fresh `new` when its rvalue is `alloc<T>(c)`, or `move _t`
     where `_t` is a temporary that exactly one assignment writes, with an `alloc`, and that only
     this `move` reads. A store writes the zero value when its rvalue is `const zero T`, `null T`,
     an `aggregate` or `aggregate zeroed` whose operands are all such constants, or `move _t` where
     `_t` is a temporary that exactly one assignment writes, with one of these rvalues, and that
     only this `move` reads.
   - **Gen.** The store `_n = const zero T` of the entry block (9.2) and every other store of
     the zero value into `p` make `p` and each tracked place below `p` empty. `del(move p)`
     makes `p` empty. Each `move p` operand makes `p` and each tracked place below `p` empty
     when it reads `p`. A store of a fresh `new` into `_n` makes each tracked place through `_n`
     empty.
   - **Kill.** A store into a place `d` whose base is a local or a global kills the fact of each
     tracked place with that base that is not separate from `d`, so a store into `_p` kills each
     fact through `_p`. A store into a local that is a key kills each fact whose place has that key.
     A store into a place through a fresh pointer local kills nothing more. Every other store whose
     destination contains a `deref`, or an `index` on a span or a `string` (5.5), is a store through
     a pointer. It kills each fact about a place reached through memory. Two facts survive it: a
     fact about a place through a fresh pointer local, and a fact about a place with the base of the
     destination that is separate from it. A destination with a `deref` or an `index` on a span or a
     `string` after its first projection is separate from no place, so such a store kills each fact
     reached through memory except the facts through a fresh pointer local. A store straight into a
     place of a memory local or of a global, whose destination contains no `deref` and no `index` on
     a span or a `string`, kills each fact about a place that contains a `deref` or an `index` on a
     span or a `string`, except a fact about a place through a fresh pointer local: a pointer can
     hold the address of that local or global. It writes no byte of another local or another global,
     so it keeps their facts. A `call` rvalue kills each fact except the facts about a local that is
     not memory and about its places that are not reached through memory. `alloc` and `del` are no
     calls.
   - **Examples.** For a parameter `_1` of type `int_buf mut*`, the source `del(b->items);
     b->items = new(i32, 8);` lowers to `del(move (*_1).0)`, then `_3 = addr mut((*_1).0)`, the
     `new` into `_5`, `check(overwrite: (*_3))` and `(*_3) = move _5`. The pass reads `(*_3)` as
     `(*_1).0`, whose fact the `del` made, so the check is proved; without the `del`, it is
     unproved, because `bb0` starts with no fact of a place through a parameter. For parameters `_2`
     and `_4`, a store into `(*_2).0` keeps the fact of `(*_2).1`, and a later store `(*_4).0 =
     const i32 0` kills it, because `_4` can hold the value of `_2`. For a parameter `_1` of type
     `node_vec mut*` and the local `_5` (`k`), the source `u64 k = v->len; del(v->items[k]);
     v->items[k] = move(n);` gives `_6 = copy _5` and `del(move (*_1).0[_6])`, then `_8 = copy _5`,
     `_9 = addr mut((*_1).0[_8])` and `check(overwrite: (*_9))`. The pass reads `(*_9)` as
     `(*_1).0[_8]`, and `_6` and `_8` both have the key `_5`, so the check is proved. For a
     parameter `_1` of type `list mut*`, the source `node mut* own n = new(node); n->next =
     move(l->head); l->head = move(n);` passes: the held address of `n->next` ends no freshness, so
     the store into `n->next` keeps the fact of `(*_1).0` (`l->head`) that the `move` made.
   - **Order.** Within a statement, the pass applies the reads of its operands first, in operand
     order, then the kill of a `call`, then the test of a store that needs a proof (**Proof**),
     then the kill rules of the store, then its gen rule. `live` and `dead` change no fact.
   - **Join.** A block starts with the intersection of the facts at the end of its predecessors.
     `bb0` starts with the fact of each tracked place in fixed storage of a temporary, or of `_0`
     when its type is no aggregate, and with no other fact. That storage holds no allocation before
     its first store (`spec/ownership-design.md` 4.4). An aggregate `_0` starts with no fact: it is
     memory (**Facts**), and the caller can give it storage that holds an allocation. Every other
     block starts with all facts before the first iteration, so a block that no edge reaches
     passes, as in V13. The pass follows every edge, the edge that V9 prunes on a constant `switch`
     included, and it iterates to a fixpoint.
   - **Scratch.** A scratch local is a temporary, or `_0`, of an `own` reference type that is not
     memory. The pass keeps one bit for each scratch local, and the bit says that the whole local
     holds the zero value. `bb0` starts with each bit set, and the bits join as the facts join. A
     `move` operand whose place is the whole local, and a `del` of the whole local, set its bit
     with the reads of **Order**. A store of the zero value into the whole local sets its bit, and
     each other store into fixed storage of the local clears it, after the test of **Order**. No
     other statement changes the bit: no pointer names the storage of a local that is not memory,
     and no `call` writes it. A store into a whole scratch local takes its bit and no fact.
   - **Stores.** A store needs a proof when it is an assignment into a place of an `own` reference
     type, a store into a temporary or into `_0` included, with two exceptions that no check stands
     right before. A check stands right before a store when the store is the first statement of the
     continuation block of the check and the place of the store is the place of the check: one
     base and equal projections, index locals included. The first exception is a store into fixed
     storage of an aggregate `_0`, the destination of the caller (5.3). No lvalue of the source
     names `_0`, and the lowering stores into it once on each path to `return`. FIR text that stores
     into an aggregate `_0` over an allocation is the first of two shapes outside the guarantee of
     the pass; **Proof** gives the second. An entry fact for an aggregate `_0` would reject fort
     source: the storage of an `own` span or `string` result is memory, and each `call` before its
     store kills its fact. The second exception is the entry zeroing of 9.2: a store of the zero
     value into a whole named local in `bb0`, when no statement of `bb0` before it writes a place of
     that local or takes the address of one. No edge enters `bb0` (V2), so the local holds no
     allocation of the call there. Each other store needs a proof, with a check or with none.
   - **Proof.** A store that needs a proof is proved when its destination is a tracked place and its
     fact holds after the reads of its operands and the kill of its `call`, before the kill rules of
     the store (**Order**). That is the test of D17.11: the fact after the right-hand side and
     before the kill rules of the store. A store into a whole scratch local is proved when its bit
     holds at that point (**Scratch**). The pass reads the destination with the stale set before
     the statement. A `check(overwrite: p)` guards each store into `p` that a path from the start of
     its continuation block reaches before any other store into `p`. A store into `p` is an
     assignment whose place is `p` by the test of **Stores**, and the path follows every edge. The
     check is proved when `p` is a tracked place, its fact holds at the end of its block, and each
     store that it guards needs a proof and is proved: a store of an exception gives the check no
     fact. So a statement, a call or a `goto` between a check and its store applies its kill rules
     before the test of the store, and the check takes the verdict of the store. For example,
     `(*_1).0 = call fn f(copy _1)` after `check(overwrite: (*_1).0)`, and `_2 = const u64 1` and
     then `_1[_2] = move _3` after `check(overwrite: _1[_2])`, leave the check unproved. The
     lowering writes the right-hand side before the check and `p = move _t` first in the
     continuation block (9.3). The store then has the fact of the end of the block of the check: V11
     gives the continuation block one predecessor, and `move _t` makes no fact of `p` empty. So the
     store and its check have one verdict. The work of a function is the sum of three counts. The
     first is its statements and terminators times its `check(overwrite: p)` terminators and its
     stores that need a proof, that no check stands right before and that write no whole scratch
     local. The second is its blocks times its locals, and it bounds the dataflows of the fresh
     pointer locals and of the scratch locals. The third is, for each local `_k` that is not memory,
     the assignments whose place has the base `_k` times the temporaries `_t` that are not memory
     and that exactly one assignment writes, with the rvalue `copy _k` or `cast<u64>(copy _k)`. The
     third count includes such a `_t` whether or not an `index` reads it. The lowering stores into a
     temporary with no check. The second count takes such a store when the temporary is a scratch
     local, and the first count takes it otherwise (D17.11). The pass measures the work before any
     dataflow. When it passes 1048576 (2^20), the pass proves no check and no store of the function
     (D17.11). A store into a whole scratch local is then no error: no lvalue names it, and D17.8
     keeps the lowering from a store over a live one. So FIR text past the limit that stores over a
     live scratch local is the second shape outside the guarantee. The lowered FIR past the limit
     stays sound by D17.8. A scratch dataflow past the limit would not help: its sets are the second
     count, which alone puts `big` of `fail/ownership/060` past the limit. The first error of the
     function takes the second note of D17.11. A function with no check and no store that needs a
     proof has no work. The pass reports each check and each store that it does not prove, once for
     each location, in block order and the stores of a block before its check. It reports at the
     location of the item, which is the `=` of an assignment or the declared name of a declaration
     (9.3), so a check and its store of the lowering share one error. The message and its note name
     the source text of the lvalue, or the FIR place when the source is not there (D17.11).
   - **Rewrite.** The pass rewrites nothing, so the verifier does not run after it.
4. **The build-mode pass**, which rewrites for the selected mode (D11.1, D10.6, D17.11):
   - In every mode, it replaces each `check(overwrite: p)` that the emptiness pass proved by
     `goto`. A check that the pass did not prove stays. No build reaches the pass with one (item
     3), and the translator refuses one (12.4).
   - Under `--release`, it replaces `check(_, overflow)` by `goto`. It
     replaces `check(_, shift, _n, ...) -> bb` by `_m = and(copy _n, const i64 W - 1)` and
     `goto bb`, which is the mask of item 15. `_m` takes the statement index of the check. When
     the count `_n` is a whole temporary, the pass replaces each read of `_n` that the mask
     reaches by `copy _m`. The mask reaches a read when, on every path from `bb0` to the read,
     the mask stands after the last assignment of `_n`. The mask keeps its own read of `_n`, and
     a second mask of the same count ends the first. A count that is no whole temporary, such as
     a constant, a parameter, a named local or a field, keeps every read, and its mask goes as
     dead (T-247).
   - Under `--no-bounds-check`, it replaces `check(_, bounds, ...)` and `check(_, span, ...)` by
     `goto`.
   - It then removes each assignment `_t = rvalue` where `_t` is a temporary of the lowering,
     the rvalue is an operand, an arithmetic, overflow, bitwise, shift, comparison or `cast`
     rvalue, or `addr`, the rvalue contains no `move` operand, and no statement or terminator
     reads `_t`. It repeats until nothing changes, so the `add_overflows` that fed a removed
     check goes too. It never removes a `call`, an `alloc`, a `del`, an assignment into a named
     local, or an assignment into a place with projections. It removes no `let`, so a temporary
     whose assignments go keeps its number, and the translator gives it no storage (12.1).
   - Where a removed `check(overwrite: p)` stood between `_t = rvalue` and `p = move _t`, and
     nothing else reads `_t`, it rewrites the two statements to `p = rvalue`, which is the direct
     store that the direct path wrote in release mode. This fold applies in every mode.
   - It then merges the continuation block of each removed check, the shift check included, into
     the block of the check: rule V11 says that block has no other predecessor. It renumbers the
     blocks in ascending order and rewrites every target of every terminator.
   - It never touches `div_zero`, `div_overflow`, `alloc_count`, `enum`, `user` or `panic`.
5. The verifier again.
6. The translator (section 12).

FIR changes at two points of this order, and `--fir-after=<pass>` (D14.1) names them: `lower`
is the output of the lowering (section 9), and `build-mode` is the output of step 4.

A **check elimination** pass can come later and needs no change to FIR: it proves a `check`
false and replaces it by `goto`, such as the bounds check of `a[i]` inside
`for (i = 0; i < 4; i++)`. It runs after the build-mode pass, because a fact learned on the
continuation edge of a check holds only in the modes that keep that check (section 14).

The **interpreter** `fir.interpreter` (`src/fort/fir/interpreter.ft`) runs FIR and changes none
of it. It is the reference semantics of the ownership checks (`spec/ownership-design.md` 3), and
it can serve the compile-time function evaluation that D15 defers. It runs each function as the
lowering makes it, after the verifier and before the emptiness pass, so it runs each check of the
checked mode, and a `check(overwrite: p)` continues. It runs each function as the translator
writes it (section 12), and it starts a program as `toolchain.md` 6 item 22 starts it:
- Each global, data constant, function, frame and allocation takes a new range of a 64-bit
  address space, and no address comes twice. A pointer is an address, so `cast<u64>` keeps it.
  A place has the layout and the little-endian bytes of the target.
- The operands of an rvalue are read in order, a `move` zeroes its place after its read, and the
  address of the destination comes after the right side (sections 6 and 7, 12.5).
- A failed check calls the runtime entry of its kind with its reported operands and the file, the
  line and the column of the check (12.4). `alloc` calls `std.rt.alloc` and `del` calls
  `std.rt.free` (12.2).
- A call of an extern of the list of `spec/ownership-design.md` 3.2 runs, from `std.rt` or from
  the program: `exit`, `abort`, `write`, `calloc`, `free`, `isatty`, `strlen`, `memcpy`,
  `memmove`, `memcmp`, `__errno_location`, `__error`, `strtod` and `strtof` with a null end
  pointer, and `snprintf` only with the arguments of `std.libc.snprintf_float`. Three model
  externs serve the tests: `oracle_keep(void* p) void` keeps `p` in a slot of the run,
  `oracle_pass(void* p) void*` returns `p`, and `oracle_call(f, a...)` calls `f` with the other
  arguments, up to the parameters of `f`, and returns its result. A call of any other extern, a
  call of a listed extern in another form, and a call of a function with no body end the run as
  `outside`. When the run keeps the output of the program, a `write` to a descriptor other than
  1 and 2 also ends the run as `outside`.
- A run ends with the return of the function that it runs, an `exit`, an `abort`, a `trap`, an
  `outside`, or a fault: an operation with no defined result, such as a load through null or
  outside each live range.
- The shadow state `fir.shadow` (`src/fort/fir/shadow.ft`) observes each run and changes none of
  its outcome. It keeps the owner slots, the tags and the events of `spec/ownership-design.md`
  3.3 and 3.4. A run reports each event once, as the line `event <kind> in <function> at
  <line>:<col>`, where `<kind>` is a name of the table of 3.4 and the location is that of the
  statement or the terminator that runs. The line ends with the tags of the value of the access
  or of the `del` that name a local `_<n>`: ` through owner <function> _<n>`, then ` through frame
  <function> _<n>` or ` through moved <function> _<n>`. A frame tag that a callee kept adds
  ` kept`, and a vacated slot that a callee read adds ` by a callee`. An extern reaches memory
  through the tags of an argument whose value starts the access: of such arguments, the one with
  the lowest index that has a tag. A `leak` stands at the item where the owner count became 0,
  and an event inside the `std.rt.free` of a `del` stands at the `del`. A `use after release`,
  `use after scope` of a frame that returned, `double release` and `invalid release` stand
  before the fault that ends the run, and a `use after move` before the fault
  `null dereference`.

`fort --fir-run` runs a program with it, and the pass `run` of a FIR test runs a function or a
program (`toolchain.md` 1, 16.3). `fort --fir-run --fir-events` writes the events to stderr after
the run, each line after `fort: `, and the pass `run` writes them in its report.

## 12. Translation to LLVM

The translator writes one FIR function as LLVM IR text. It keeps every item of `toolchain.md` 6;
12.5 records where its text deviated from the text of the direct path, which T-255 deleted. It
does not read the tree and it does not read the build mode.

### 12.1 Locals and places

- A named local or a parameter `_n` is an `alloca` in the entry block, in local order, named
  `%<ident>.<n - 1>`, so that the numbers count parameters and locals from 0 (item 10). An aggregate
  parameter has no `alloca`: its storage is the incoming pointer (item 7). A scalar parameter
  arrives as `%<ident>.in` and is stored first (item 10).
- `_0` of an aggregate type is `%ret.sret` (item 7). `_0` of a scalar type is the `alloca`
  `%result`, which `return` loads. The name has no dot, so no local collides with it (D19.5).
- A temporary of scalar type that exactly one statement assigns, and that only statements and
  terminators with the same statement index read, the failure blocks of that statement included,
  is a register `%t<N>` and has no `alloca` (item 11). The lowering of section 9 reads such a
  temporary only in statements that it emits after the definition and on the same path, never
  after a join that the definition does not precede, so the definition dominates every read; each
  expansion of a deferred statement has its own index and its own temporaries. Every other
  temporary that a statement or a terminator names is an `alloca` named `%tmp<K>` (D19.5): the
  `&&` slot, which two statements assign, and the counter of a range `for`. A scalar temporary
  whose storage an `addr` takes is an `alloca` too, because a register has no address. So is a
  scalar temporary that an item reads before its assignment, in the order of the blocks and of
  the items in each block, because a register has no value before its assignment. The lowering
  writes neither case (V13, section 9). The next rule gives a temporary that nothing names no
  storage. The translator classifies the locals in one scan before it writes text, as the direct
  path collected the locals before it emitted. The direct path kept its scalar intermediates in
  registers and its aggregate temporaries in `%tmp<K>` slots, so this rule gave the same shape.
- A temporary that no statement and no terminator names gets no storage: no register and no
  `alloca`. A statement or a terminator names a local when the local is the base of one of its
  places or the index of one of its projections. The build-mode pass leaves such temporaries,
  because it removes assignments and keeps the `let` of every local (section 11).
- A place is its base address, then one `getelementptr inbounds` for each projection in the
  three shapes of item 3, then a `load` or a `store`. A `bool` loads as `i8` and `trunc` and
  stores as `zext` and `i8` (item 2).
- `copy` or `move` of an aggregate place is `llvm.memcpy`; `const zero` of an aggregate is
  `llvm.memset` (item 3). For an `aggregate`, the translator reads every operand before its
  first store: it loads each scalar operand into a register and copies each aggregate operand
  into a `%tmp` slot (section 6).
- `move p` reads the value into a register or a `%tmp` slot, zeroes `p` with `store ptr null`
  or `llvm.memset`, and only then writes the destination, in every case, because the destination
  can be `p` itself (item 18, `notes/compiler.md` 6). A `move` or a `del` of a temporary prints
  no zeroing, whether the temporary is a register or a `%tmp` slot: nothing reads a temporary
  after a `move` of it (rule V12), and the direct path zeroed no intermediate.

### 12.2 Rvalues and statements

- `add`, `sub`, `mul`, `div`, `rem`, the bitwise operations and the shifts are the LLVM
  instruction that the fort type selects: `sdiv` or `udiv`, `ashr` or `lshr` (item 15). The
  translator never writes `nsw`, `nuw` or `exact` (D16).
- `add_overflows` and its two siblings print the `llvm.*.with.overflow` intrinsic of the type,
  then `extractvalue 0`, then `extractvalue 1`, in the order of item 15. The `add` of the same
  two operands in the continuation block prints nothing and takes the name of `extractvalue 0`
  when three conditions hold: no assignment and no `del` follows the intrinsic in its block, the
  `check` that ends that block tests the result of the intrinsic, and the `add` is the first
  assignment or `del` of the continuation. Nothing then writes an operand between the two, and
  the lowering of section 9.4 writes each checked `add` so. The same holds for `sub` and `mul`.
  Every other `add` prints a plain `add`.
- `eq` to `ge` are `icmp` with the predicate the type selects, or `fcmp` for a float (item 15).
- `cast<T>` is the instruction of item 12, or nothing when the LLVM value types are equal, in
  which case the result takes the name of its operand. A `cast` between aggregate types is a
  `llvm.memcpy` of the header or the value.
- `addr(p)` is the address of `p` with no load.
- `slice` and `slice_ptr` write the two header fields of the destination (item 16).
- `alloc<T>(n)` is a call of `std.rt.alloc` with the size of `T`, `n`, and the file, line and
  column of the statement (item 17).
- `aggregate` writes each member into its field or element through a `getelementptr` (item 3).
  `aggregate zeroed` first writes `llvm.memset` of the whole destination and then each member
  that is no `const zero`. The word `zeroed` is the mark only when a type follows it, so a
  struct named `zeroed` stays a type.
- `call` is `call` with the signature of 12.3; an extern call carries `#3` (item 8).
- `del(move p)` loads the pointer (field 0 for a span or `string`), calls `std.rt.free`, and
  zeroes `p` (item 17).
- `live` and `dead` print nothing.

### 12.3 Signatures

One function maps a fort function type to its LLVM signature: the result type and its extension
attribute, the `sret` parameter, and each parameter with its extension attribute (item 7). The
translator writes every `define`, every extern `declare` and every `call` from that one map. For
an extern, the same function applies the target's ABI form (item 8). The reason: the definition
and every call of it then have one source. The call site of T-021 had a second source, and it
lost a `zeroext`.

### 12.4 Terminators and failure blocks

- `goto` is `br`. `switch` on a `bool` is `br i1`; on an integer, `char` or enum it is an LLVM
  `switch` with one case for each constant and the `otherwise` block as its default (item 10).
- `return` is `ret void`, or a load of `%result` and `ret` (item 7).
- `check(f, kind, args) -> bb` is `br i1 %f, label %fail, label %bb`, the failure label first
  (D19.6). The kind `user` branches to the continuation first, `br i1 %c, label %bb, label
  %fail` (item 14). No `check(overwrite: p)` reaches the translator in a build: the build-mode
  pass replaced each one, which the emptiness pass proved, by `goto` (section 11, item 18). A
  check that reaches it is an internal error (D13.3).
- The translator makes one failure block for each `check`: one call of the runtime entry of
  `kind` with `args`, the `@.file.N`, the line and the column, then `unreachable`. A FIR block
  other than `bb0` that holds only `fail` is a failure block too, written the same way, and its
  own number is not a label. The translator writes every failure block after the normal blocks
  of the function, in the order of the numbers of the FIR blocks that hold their `check` or
  `fail` (D19.6, item 14).
- A `check` loads its condition and its reported operands in its own block, before its `br`, so
  that each dominates the failure block. A block that holds only `fail` has no statement to hold
  those loads, and more than one block can branch to it. So it loads its reported operands
  inside its failure block, before the call, and that failure block holds more than the call
  and `unreachable`. The translator writes such a failure block when it reaches its FIR block in
  number order, so the `%t` numbers of its loads follow the block order and not the text, in
  which the failure block stands after the normal blocks (T-251).
- `bb0` is the entry block even when it holds only `fail`: its call and `unreachable` stand in
  place in `entry`, as a `fail` after statements does. LLVM needs an entry block, and no
  terminator names `bb0` (V2), so no label could reach a moved `bb0` (T-251).
- `fail(panic, ...)` after statements in an ordinary block is the call and `unreachable` in
  place (item 19).
- `trap` is `call void @llvm.trap()` and `unreachable` (item 20). `unreachable` is
  `unreachable`.

### 12.5 Names, data and deviations

- The translator numbers `%t<N>` from 0 for each function in the order it writes the LLVM
  results. It names `bb0` `entry` and a normal block `n` `%L<n>`. It names the `k`-th failure
  block `%L<B + k>`, where `B` is the number of FIR blocks, so that every label is `%L<N>` and
  the failure labels are the largest (D19.5). The number of a FIR block that holds only `fail`
  is not a label.
- It assigns `@.str.<N>` and `@.file.<N>` at first use, walking the modules in the order of
  D9.10, the global initializers of a module before its functions, and the blocks of a function
  in ascending number, with the data of a failure block at the `check` that makes it (D19.5).
  Intrinsics and attribute groups stand in the fixed order of items 8 and 14.
- Extern declarations stand in the order of the first call of each in the text of the function
  definitions, the definitions in module order (item 8). The extern list of a FIR function
  follows its blocks in number order, and the translator writes the blocks in that order, so the
  list follows the text (5.1). During the migration (16.1) the same rule held in a module that
  both paths wrote: the direct path recorded an extern where it wrote the call, after the calls
  of the arguments. The `declare` lines of the two paths differed only where the texts of their
  functions differed: for a `while` whose body breaks after an `if`, FIR numbers the block after
  the loop before the rest of the body, so a call after the loop can precede a call in the body.
- The translator gives `#8` to the definitions of the `noreturn` runtime entries that
  `toolchain.md` 5.1 lists, and to no other function (item 14).

The translation deviated from the text of the direct path in these ways, and in no other that
the design knew; T-255 deleted the direct path, and the list stays as the record of what the
migration changed and as the rules of `tools/fir_diff.py` (16.2). Each is a difference in text:
- Block numbers differ, because FIR creates its blocks in another order than the direct path
  allocated its labels, failure blocks take the last numbers, and the number of a `fail`-only
  block is not a label.
- A scalar `_0` is an `alloca` `%result` that each `return` loads; the direct path returned a
  register. The `&&`, `||` and `?:` slots and `%tmp` numbers can differ in order.
- `@.str.<N>` and `@.file.<N>` follow the block numbers, and the direct path followed its tree
  walk, so a string in a nested block can take a later number.
- The address of an assignment target that the lowering does not hold with `addr` (9.3) is
  computed at the store, after the right side, and twice for `lv op= e`; the direct path
  computed it once, before the right side. Nothing writes memory in between, so the address is
  the same.
- A place operand is loaded once for each rvalue that names it, so `x / a[i]` loads `a[i]` for
  the zero test, the overflow test and the division; the direct path loaded it once. So
  `fprint(fd, a, b)` loads `fd` again for each print, after the print of the argument before.
  The print entries of `std.rt` write no global of another module, so the value is the same.
- The failure blocks of nested constructs stand in another order, because they follow the
  numbers of the blocks that hold their checks; the labels still ascend (D19.6).
- The index of an index projection loads before the pointer that its base reads: `h->arr[i + 1]`
  computes and checks `i + 1` before it loads `h`. The direct path loaded `h` first.
- An operand that no temporary holds loads where the rvalue that reads it stands, after the
  temporaries of the other operands: `push_byte(b, cast(ch, u8))` loads `ch` before `b`. The
  direct path loaded the operands in order. The lowering holds an operand in a temporary before a
  later writer (9.4), so no write stands between the two loads.
- An aggregate reads every member before its first store (section 6): `pair{p.b, p.a}` loads
  both fields and then stores both, and `pair{f(), a + 1}` stores both members after the call
  and after the overflow check. An aggregate member of an aggregate goes into a `%tmp` slot
  first and then into its field with `llvm.memcpy`. The direct path wrote each member into its
  field as it read it, before the calls and the checks of the members after it.
- A designated literal zeroes the whole value with `llvm.memset` after it reads its members,
  and the direct path did before; both then store the named fields. A member of a literal that FIR
  builds in a temporary of a type with padding is built in a zeroed temporary, which one
  `llvm.memset` more zeroes, and then copied into its field (9.4). The direct path built it in
  the field, so the padding of that field is zero on the FIR path where the direct path left
  it as it was.
- An aggregate rvalue goes through a `%tmp` slot: the result of a call, a `slice` and a `cast`
  between aggregate types is a temporary, and an `llvm.memcpy` copies it into the place that
  reads it. `return t` of a local of an owning aggregate type copies `t` into one more `%tmp`
  slot before it copies it into `%ret.sret`. The direct path wrote such a value into its
  destination.
- `eat(move(s))` copies `s` with one `llvm.memcpy` into the `%tmp` slot of the argument and then
  zeroes `s`. The direct path wrote two: into an intermediate, and from it into the argument
  copy (T-251).
- An aggregate argument that is a place other than the storage of a temporary is copied into
  its `%tmp` slot at the call, after the other arguments and their checks (T-251, open question
  4 of that ticket). The direct path copied each argument where it evaluated it, so
  `mem.copy(buf, s[0..n])` copies `buf` before the span check of `s[0..n]`.
- A `switch` on a `&&`, `||` or `?:` slot loads the slot again, a `load i8` and a `trunc`, in
  the block that has just stored it. The direct path branched on the value that it stored.
- A string constant operand of `==` or `!=` on strings goes into a `%tmp` span, and the call of
  `std.rt.str_eq` loads the two fields of that span. The direct path passed the pointer and the
  length of the constant.
- A span check of `s[lo..hi]` tests `lo > hi` before `hi > len`, and `or` joins them in that
  order. The direct path tested `hi > len` first.
One difference is in behavior: an aggregate literal that reads its destination (section 6). The
user ruled on it on 2026-09-28 (open question 5), and D6.3 says so. The test
`test/lang/run/structs/014_literal_reads_old_value.ft` holds the behavior of FIR, and the oracle
of 16.2 leaves its function unclassified. The migration ticket that meets a further difference
lists it here or removes it (16.2).

## 13. The textual form

The textual form is what `--fir` prints (open question 1) and what a FIR test reads. One parser
reads it, so a test can write a function by hand, run one pass, and compare the result with an
expected text. A comment starts with `//` and ends at the line. A location `#line:col` is
optional after a `let`, a statement or a terminator. A `let`, a statement or a terminator
without a location has the empty location, and the printer writes no location for the empty
location, so a printed function reads back as it was. A statement index `s<N>` may follow the
location of a statement or a terminator; a missing statement index is index 0, so a test of the
translator writes the indices. A `let` may carry the name of a named local in parentheses, and a
parameter may carry its name in the same way.

```ebnf
function    = "fn" name "(" [ param { "," param } ] ")" "->" ( type | "noreturn" ) "{" { local }
              block { block } "}" ;
param       = local_ref [ "(" identifier ")" ] ":" type ;
local       = "let" local_ref [ "(" identifier ")" ] ":" type [ position ] ";" ;
block       = block_ref ":" "{" { statement } terminator "}" ;
statement   = ( assign | "del" "(" "move" place ")" | "live" "(" local_ref ")"
              | "dead" "(" local_ref ")" ) [ location ] ";" ;
assign      = place "=" rvalue ;
terminator  = ( "goto" block_ref | switch | "return" | check | fail | "trap" | "unreachable" )
              [ location ] ";" ;
switch      = "switch" "(" operand ")" "->" "[" { constant ":" block_ref "," }
              "otherwise" ":" block_ref "]" ;
check       = "check" "(" ( operand "," kind { "," operand } | "overwrite" ":" place ) ")"
              "->" block_ref ;
fail        = "fail" "(" kind { "," operand } ")" ;
rvalue      = operand
            | opname "(" [ operand { "," operand } ] ")"
            | "cast" "<" type ">" "(" operand ")"
            | "addr" [ "mut" ] "(" place ")"
            | "slice" [ "mut" ] "(" place "," operand "," operand ")"
            | "slice_ptr" "(" operand "," operand "," operand ")"
            | "alloc" "<" type ">" "(" operand ")"
            | "aggregate" [ "zeroed" ] type "(" [ operand { "," operand } ] ")"
            | "call" callee "(" [ operand { "," operand } ] ")" ;
callee      = "fn" name | "extern" name | operand ;
operand     = "copy" place | "move" place | "const" constant ;
place       = ( local_ref | "global" name | "(" "*" place ")" ) { projection } ;
projection  = "." integer | "[" local_ref "]" ;
constant    = type integer | "true" | "false" | "null" type | "zero" type | string
            | "bytes" string | "enum_table" name | "fn" name ;
location    = position [ "exit" line_col ] [ "s" dec_digit { dec_digit } ] ;
position    = "#" line_col ;
line_col    = dec_digit { dec_digit } ":" dec_digit { dec_digit } ;
integer     = [ "-" ] ( dec_digit { dec_digit } | "0x" hex_digit { hex_digit } ) ;
local_ref   = "_" dec_digit { dec_digit } ;
block_ref   = "bb" dec_digit { dec_digit } ;
```

`type` is a fort type in the spelling of `type_to_str`. The parser resolves a type spelling in
the scope of the prelude module of a FIR test alone, because `type_to_str` writes the bare name
of a struct or an enum: a FIR test declares its named types in its prelude, and the parser does
not read back the `--fir` text of a program whose modules reuse a name. `opname` is one of the
rvalue names of section 6. A `mut` after `addr` gives the result a `mut` pointer, and a `mut`
after `slice`, which appears only on a slice of a fixed-array place, gives a `mut` span (section
6). `kind` is one of the kinds of section 8. `name` is a dotted symbol name of D9.7. `identifier`,
`string`, `dec_digit` and `hex_digit` are those of `grammar.md` 1.

This program:

```fort
fn f(bool c) i32 {
    i32 mut* own p = new(i32);
    defer del(p);
    if (c) {
        return 1;
    }
    *p = 7;
    return *p;
}
```

has this FIR after the lowering, before the build-mode pass:

```fir
fn main.f(_1 (c): bool) -> i32 {
    let _0: i32;
    let _2 (p): i32 mut* own #2:18;
    let _3: i32 mut* own #2:22;           // the value of new(i32)

    bb0: {
        _2 = const zero i32 mut* own #2:18 s0;
        live(_2) #2:18 s1;
        _3 = alloc<i32>(const u64 1) #2:22 s1;
        check(overwrite: _2) -> bb1 #2:18 s1;
    }
    bb1: {
        _2 = move _3 #2:18 s1;
        switch(copy _1) -> [true: bb2, otherwise: bb3] #4:5 s3;
    }
    bb2: {
        _0 = const i32 1 #5:16 s4;
        del(move _2) #3:11 exit 5:9 s5;
        dead(_2) #5:9 s4;
        return #5:9 s4;
    }
    bb3: {
        (*_2) = const i32 7 #7:8 s6;
        _0 = copy (*_2) #8:12 s7;
        del(move _2) #3:11 exit 8:5 s8;
        dead(_2) #8:5 s7;
        return #8:5 s7;
    }
}
```

The deferred `del(p)` appears twice, once on each path to an exit, and nowhere else. An analysis
reads each path and finds the `del` on it without a model of D7.8. The statement indices count
the six statements of the body and the two expansions (9.7): `s2` is the `defer`, which emits
nothing, `s5` and `s8` are its expansions, and `s0` is the zeroing of the entry block. `_3` is
read only by `s1`. In every build mode, the emptiness pass proves the check: the entry block
zeroes `_2`, and no kill rule applies to `_2` before the check, because `alloc` is no call. The
build-mode pass then turns the `check` into `goto bb1` (section 11). It folds `_3 =
alloc<i32>(const u64 1)` and `_2 = move _3` into `_2 = alloc<i32>(const u64 1)`, at the location
of the `alloc`, and merges `bb1` into `bb0`, so `bb2` and `bb3` become `bb1` and `bb2`. After the
fold no statement names `_3`, so `_3` gets no storage (12.1).

## 14. Flow analyses on FIR

A flow analysis reads a verified function before the build-mode pass. It may assume these facts:

- every path from `bb0` ends in an exit or in an abort block;
- an abort block is a sink: runtime errors do not run deferred code (D7.8, D11.4), and no rule
  about the state at an exit applies to it;
- a `switch` whose operand is a constant has one edge only, the edge that the constant selects,
  as in rule V9. `while (true) { }` lowers to such a `switch`, and its false edge reaches a
  `return` that never runs;
- every deferred statement is on each path that runs it, and on no other path;
- `own` is on the fort type of every local and place;
- for a place that is not escaped (below), a value leaves it only through `move`, `del` or an
  assignment over it, and a value enters it only through an assignment; `alloc`, `aggregate`,
  `cast` and `call` are the rvalues that produce an owning value (D17.3), and a `call` transfers
  an owning argument by the rule of D17.5 for its parameter type. A `cast` never adds `own`
  (D3.14): a `cast` that produces an owning value takes it from its `move` operand, so the
  value leaves the place of that operand and enters the destination, as a `move` does;
- `check(overwrite: p)` reads `p` only to test it, and counts as no use of `p`. It exists to
  detect the state the analysis tracks (D17.11). The pointers that the address of `p` loads are
  uses, as section 7 says;
- `dead(_n)` is where an owning local goes out of scope, and where a leak is reported;
- every statement has a location, and a deferred copy has the location of its exit.

**Escape.** Storage that an address can reach can change without a statement that names its
place. A place is **escaped** at a point of the function when one of these is true:
- its base is a global, which every `call` and every write through a pointer can change;
- an `addr` or a `slice` of the place, of a place that contains it, or of a place that it
  contains, stands on some path from `bb0` to that point, whether the result is stored, passed to
  a call or dropped. A `slice` counts because the span it makes points into the place;
- it is `_0` of an aggregate type: `_0` is then the caller's storage (item 7), and the caller can
  pass a pointer to that same storage as an argument (`s = f(&s)`) or name it as a global
  (`G = f()`), so `_0` is escaped from `bb0`;
- it contains a `deref`, or an index on a span or a `string`: it is storage that a pointer reaches.

At a `call`, at every assignment or `del` whose place may overlap an escaped place, and at every
`move` operand whose place may overlap an escaped place (5.5), an analysis must treat every escaped
place that may overlap it as changed to an unknown state; a `call` changes every escaped place. A
`del` also frees the storage its value designates, so it changes every escaped place that contains a
`deref` or an index on a span or a `string`, as a `call` does. D17.14 leaves use after free outside
the ownership rules; an analysis that tracks views (T-190) needs this rule. The escape of a place is
permanent for the rest of the function: FIR has no rule that ends it. That is a deliberate loss of
precision, not an oversight: an address can be stored anywhere, and nothing in FIR says when the
last copy of it dies. An analysis that tracks the state of an escaped place, such as the escaped
state of the linear ownership analysis (T-182), takes that state from this definition. A place that
is not escaped changes only through the statements that name it, which is what makes an analysis of
locals exact.

An analysis must not derive a fact from the continuation edge of a `check` that a build mode
removes (`overflow`, `shift`, `bounds`, `span`): the fact holds only in the modes that keep that
check. The emptiness pass proves each overwrite check in every mode (D17.11). The continuation
edge of every other kind, and every edge of a `switch`, carries a fact that holds in every mode.

The linear ownership analysis, the refinement of null states and the analysis of local views
(T-182, T-188, T-190) are analyses of this kind. Each supplies a state for each place and a
transfer function for each statement. The first analysis that needs a framework for the join at
each block and the fixpoint of each loop builds it. FIR itself does not hold one.

## 15. Extending the language

Each feature that D15 defers, where it goes with FIR, and what changes in the core, the
translator and the analyses:

- **Linear ownership and definite assignment**: an analysis over `move`, `dead` and the
  places. No change to the core.
- **Compile-time function evaluation**: the interpreter of section 11, which lowers a function
  when it first calls it. The checker must then run a function while it checks another one, so
  the pipeline of section 3 changes to lower on demand. No change to the core.
- **Tagged unions and `Result`**: the checker, and a lowering to `switch` on the tag. One
  projection: the variant.
- **Closures and nested functions**: a lowering to a struct of captures and a function pointer.
  No change.
- **Generics**: the checker, and one pass that lowers a copy for each instantiation. No change.
- **String `switch`, struct and array equality**: a lowering to `call std.rt.str_eq` and to
  field compares. No change.
- **Integer-range `for` and labeled `break`**: a lowering to blocks and `goto`. No change.
- **Methods, overloading, default and named arguments, type aliases, visibility**: the checker
  only. No change.
- **A second back end, such as C or WebAssembly**: a new translator over FIR. No change.

Today each feature of this list touches the checker, the emitter's expression and statement code,
and the linear ownership analysis, each in its own way.

## 16. The migration

The compiler moved to FIR one function at a time. T-255 ended the migration (item 6); the items
below are the record of how it ran.

1. **Two paths in one module.** For each function, the compiler tried the lowering. When the
   lowering supported every construct of the function, the verifier, the passes and the
   translator wrote it. Otherwise, the direct path wrote it. The module-level parts did not
   change. Each ticket measured the share of functions that the lowering supports, over the
   corpus and over `src/fort`.
2. **The oracle.** The translator's text differed from the direct path's in the ways 12.5 lists,
   so the oracle is `llvm-diff` between the two, function by function, over the run corpus in
   both modes, which `tools/ir_snapshot.sh` (T-192) writes. Both hosts have `llvm-diff`:
   `llvm-diff-18` in the VM and Homebrew's `llvm@18` on the Mac. `llvm-diff` reports a changed
   `add` against `extractvalue` and an added `alloca` (measured 2026-09-28), so the first
   migration ticket must state which reports count as agreement: the differences of 12.5 and no
   other. Before the project relies on it, that ticket must also show that `llvm-diff` reports a
   dropped `zeroext`, a changed `align` and a changed `sret`, because the run corpus cannot see
   any of the three (`notes/compiler.md` 6). T-253 measured that `llvm-diff-18` reports none of
   the three, at a call or on a definition, and that its report names instructions that did not
   change. So `llvm-diff` alone is no oracle. `tools/fir_diff.py` runs it and then compares the
   text of each definition that differs. It reads each block, labeled by its place in a walk
   from `entry`, as its writes and its terminator in order. An operand stands as the expression
   that defines it, with its types, alignments and attributes, and a load carries the writes that
   can reach it. It counts each difference under an item of 12.5 whose rule accepts it, and a
   difference that no rule accepts is unclassified. Every definition that one of five mutants of
   the translator (a dropped `zeroext`, a doubled `align`, a dropped `sret`, and two swapped
   branch targets) or three edits by hand changed is unclassified (`notes/testing.md` 6). The
   rules accept a difference in text, and some rest on facts about `std.rt`, so the run corpus
   and the `fixpoint` ctest stay the witnesses of behavior.
3. **FIR tests.** A test under `test/fir/` holds a function in the textual form of section 13,
   the pass to run, and the expected text after it. An optional prelude of fort declarations
   gives the types and the symbols that the text names; `fort --fir-test` checks it as the
   module `main` (D14.1). The harness parses the input, runs the pass,
   prints the result and compares. The verifier's rules, the build-mode pass and each analysis
   get their tests in this form, one file for each rule or each rewrite. The pass `run` runs the
   module with the interpreter of section 11 and writes a report in place of the module: a line
   `stdout "..."` and a line `stderr "..."` for the bytes that the run wrote to the descriptors 1
   and 2, quoted as section 13 quotes a string, then a line for each ownership event of the run
   (section 11), then one line for its end: `return` with the type and the value of a scalar
   result (`true` or `false`, a decimal integer or address, or the binary64 pattern of a float),
   `exit <status>`, `abort`, `trap at <line>:<col>`, `outside <name>`, or `fault <what> at
   <line>:<col>`.
4. **The gen suites.** The suites `test/fort/gen_*_test.ft` hold emitted text. A suite that tests
   one construct moves to FIR when the lowering supports that construct. Its assertions then read
   the FIR text for meaning and the translated text for the ABI.
5. **V9 on the corpus.** Before V9 stops the compiler, a ticket runs it in report mode over the
   corpus and records each function that it flags. Each flag is a bug in the checker or in the
   emitter, or a gap in rule V9. T-210 was the first known flag, and the checker now refuses
   its program. `fort --fir-verify-report` (D14.1) is that report mode. T-253 ran it over
   `test/lang`, `src/fort`, `src/lsp` and `std` in the three modes, and it flagged no function
   there. No known source program breaks V9, so the tests of V9 on the paths of the compiler
   take a FIR function built by hand.
6. **The end.** The direct path was to go when the lowering supported every function of the corpus
   and of `src/fort`. The ticket that deleted it was to amend these texts: the sentence of D19.1
   that names the direct path, `toolchain.md` 6 item 1 ("built by appending text in one forward
   pass"), `toolchain.md` 6 item 10 ("the tree walk never has to know its predecessors"), the
   sentence of the rule of D19.3 that says "one tree walk with a destination place per expression",
   and `toolchain.md` 8. T-255 deleted the direct path on 2026-09-30, when `--fir-stats` gave
   `lowered N of N` over `test/lang`, `src/fort`, `src/lsp` and `std` in the three modes, and
   amended these texts and the block names of D19.5.

The self-hosting fixpoint (D19.5, `notes/testing.md` 5) must hold after every ticket. The FIR
modules must use only the language that the last pin of `tools/bootstrap.ref` accepts.

## 17. Open questions

The user ruled on questions 1, 2, 3 and 5 on 2026-09-28 ("all 5 except for 4"); each keeps its
default. The user then resolved question 4 by removing its subject: `fort_entry` goes (T-212).

1. **A `--fir` flag.** `fort --fir` prints the textual form of section 13, as `--ast` prints
   the tree, and `--fir-after=<pass>` prints the function after a named pass. The ticket that
   adds the flag amends D14.1. Ruled: yes.
2. **Runtime signatures.** The lowering takes the fort type of a runtime entry from the checked
   declaration in `std/rt.ft`, and `src/fort/runtime_sig.ft` goes. The gen suites emit with no
   runtime in the closure (`notes/testing.md` 6), so they then need a runtime stub. The C suites
   write one with `emit_with_runtime` in `bootstrap0/test/common/gen_helpers.h`, which the fort
   helpers can copy. Ruled: yes.
3. **The oracle.** `llvm-diff` with the agreed differences of 12.5, plus the run corpus and
   `fixpoint`, is the oracle of the migration, after the ticket of 16.2 proves what `llvm-diff`
   sees. Ruled: yes. T-253 measured what it sees, and the oracle compares the text beside it
   (16.2).
4. **`fort_entry` and `main`.** The translator wrote two entry functions from the signature of
   `main` (item 22). Should they be FIR functions? Ruled: no. `fort_entry` had no job left after
   T-088 moved the runtime into fort, and T-212 removed it, so the compiler-emitted `main` calls
   the program's `main` directly and is the one function the translator writes as fixed text.
5. **Aggregate literals that read their destination.** The direct path wrote a literal member
   by member, so `s = pair{s.b, s.a}` gave `s.b, s.b`. Section 6 reads every operand first, so
   the same statement swaps. Ruled: the swap. D6.3 gains the sentence "an aggregate literal
   reads every member before the destination changes", and the direct path's behavior was a
   known difference until T-255 deleted the direct path.

## 18. Coverage of the contract

Each item of `toolchain.md` 6 that describes a function body, and the part of FIR that keeps it:

- **Item 2, type mapping**: the translator (12.1).
- **Item 3, aggregates in memory**: places (5.5), `aggregate`, `addr`, and the translator (12.1).
- **Item 7, the fort calling convention**: `call` (6) and the signatures (12.3).
- **Item 8, extern declarations**: `call extern` and the signatures (12.3).
- **Item 9, normalization**: the extension attributes of 12.3, and `bool` in 12.1.
- **Item 10, locals and control flow**: locals (5.3), blocks (5.4), terminators (8), and the
  lowering of statements (9.3).
- **Item 11, SSA discipline**: principle 8; a local is an `alloca` or a register written once.
- **Item 12, casts**: `cast` (6, 12.2).
- **Item 14, checks and failure blocks**: `check`, `fail`, and the failure blocks of 12.4.
- **Item 15, integer checks**: `add_overflows` and its siblings, and the kinds `overflow`,
  `shift`, `div_zero` and `div_overflow`.
- **Item 16, bounds checks**: the kinds `bounds` and `span`; the index projection; `slice` and
  `slice_ptr`.
- **Item 17, `new` and `del`**: `alloc`, `del`, and the kind `alloc_count`.
- **Item 18, ownership**: `move`, the kind `overwrite`, and the zeroing of the entry block (9.2).
- **Item 19, builtins**: the lowering of the print family, `assert` and `panic` (9.4).
- **Item 20, `noreturn`**: `trap`, rule V8, and the end of a body (9.6).

Items 1, 4, 5, 6, 13, 21, 22 and 23 describe the module and its data. The translator keeps them
as the direct path did.
