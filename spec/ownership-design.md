# Ownership checks: a coarse and sound design

Status: a design. The user ruled on its questions on 2026-10-23, and on assumption A8 on 2026-10-26
(section 12). It adds no rule to fort. `decisions.md` and `grammar.md` win over it (D1.2). A rule of
section 6 enters the language only through a decision of its own, in the order of section 11.

The plan of 2026-10-22 asks for this design as its step 4. The design applies the eight rules for
the next design of `notes/ownership-retrospective.md` 8, and section 13 checks each of them.

**The premise.** The user ruled on 2026-10-23: "Coarse and sound". The ruling changes no rule of
the language. It has five points, and this design applies each of them:
1. A call kills each fact about a memory place: kill rule 5 of D17.11 stays at each call. So the
   design has no private memory local and no assumption about the arguments of a call.
2. A borrow of an owner that goes anywhere other than a view local pins the owner. That covers a
   field, an array element, memory, the return slot `_0` and a call argument. It also covers a place
   of a type that is no reference, such as a `u64` through a cast. A later `del` or `move` of a
   pinned owner in the function is an error. Section 4.6 adds a call that can change a pinned owner,
   and the user confirmed that addition (Q4, section 12).
3. A place reached through a pointer may alias any memory place, of any type. An update through it
   is a weak update of each of them. "Reaches" and "borrows" do not follow chains of view places.
4. Each rule has a soundness argument, with one item for each place, operand, rvalue, statement
   and terminator form of `fir.md` 5.5 to 8 (section 5.4). One list holds the assumptions (5.1).
5. Precision comes later: one refinement in each PR, each with its own proof and review. Section
   7 lists the candidate refinements as later tickets. They are not part of the first version.

The **first version**, V1, is the set of rules of sections 4 to 6 with no refinement.

Each number says how this document got it:
- `measured`: a command or a probe ran on 90b50c0a. Section 14 gives each one.
- `estimated`: an argument gives the number, and no program counted it. An estimate says its
  direction when it has one: `high` or `low` against the exact count.
- `read`: a person read the code or a document.

A probe measures an approximation of a rule (section 14). The report-only ticket of each rule
replaces the probe count with the count of the real pass (section 11).

## 1. Goal and scope

The compiler proves one ownership rule today: the overwrite rule of D17.11. D15 defers linear
ownership, and D17.14 lists what ownership does not track.

This design adds the rules of the plan one at a time. Each rule is a compile error:
- (a) a store over an owner that can hold an allocation. This is D17.11, and it is done.
- (b) a use of a local owner after its `move` or `del`;
- (c) a local owner that can hold an allocation at the end of its scope, and a `del` or a `cast`
  that drops owners that can hold allocations (a leak);
- (d) a double `del`. Section 6.4 gives it no error of its own, by the ruling on Q3;
- (e) a view that the program reads after its owner gave up the allocation, or after the frame of
  its storage ended;
- (f) the rules through calls: in V1 the coarse call rule of 4.6; summaries are refinements (7, 8).

Four properties hold for each rule:
1. The oracle of section 3, an executable reference semantics, reports the event that the rule
   prevents, and the tests of each rule compare with it from its first commit.
2. One analysis on FIR checks all the rules, one function at a time (section 4).
3. An obligation that the analysis cannot prove is a compile error, as in D17.11 ("Unknown is an
   error", the user, 2026-10-22). So no rule needs a ledger of known gaps.
4. Each rule names its scope. Outside it the rule makes no claim, and D10.7 says what stays
   undefined behavior. Section 5.2 lists each case outside a scope in one place.

Each rule is sound by construction, with a coarse rule where a precise one is hard (section 5).

## 2. Terms

- **Owner**: a tracked place of D17.11 whose type is an `own` reference (D17.1).
- **Local owner**: an owner in the fixed storage of a local (`fir.md` 5.5). Rules (b), (c) and
  (e) are about local owners.
- **Memory local**: a local that is memory by D17.11: the function takes its address. A
  **memory owner** is a local owner of a memory local.
- **Memory place**: a place reached through memory (D17.11). It is a place of a memory local or of
  a global, a place through a pointer, or a place with an index into a span or a `string`.
- **View**: a value of a reference type without `own` at the outermost level (D17.4).
- **View local**: a local of a view type that is not memory and is not `_0`. Temporaries count.
- **Origin**: what a view can designate in the function: the allocation that a local owner holds,
  or the frame storage of a local. A view **borrows** an origin when it can designate it.
- **Release**: an operation that can make an owner give up its allocation. It is a `move` or a
  `del` of a place that can be the owner, or a call that can change the owner (4.5, 4.6).
- **Stale**: a view local is stale when an origin that it borrows can have had a release, or its
  frame can have ended, since the view local got its value. **Pinned**: a borrow of the owner can
  be in storage that is no view local.
- **Event**: a report of the oracle (3.4). **Obligation**: what the analysis must prove at one
  **site**, the source position of the obligation and of its error.
- **False accept**: a function that the analysis accepts, while the oracle reports an event that
  the scope of the rule covers, for some input. **False reject**: an error of a rule at a site
  where no input gives such an event.
- **Migration count**: the sites of a rule over the corpora `src/fort`, `src/lsp`, `std`,
  `test/fort` (the module tests) and `test/lang`, before the rule becomes an error.

## 3. The reference semantics (the oracle)

### 3.1 Modules

| module            | file                          | holds                                 |
|-------------------|-------------------------------|---------------------------------------|
| `fir.interpreter` | `src/fort/fir/interpreter.ft` | the run of FIR: frames, values, calls |
| `fir.shadow`      | `src/fort/fir/shadow.ft`      | the shadow state, tags and events     |

The oracle runs FIR because the analysis reads FIR, so both see one program (`fir.md` 11 names it
as a second oracle). `fir.ft` declares nothing named `interpreter` or `shadow` (measured: `grep`),
so each import has one reading (`notes/style.md` 3.1 rule 1).

### 3.2 What it runs

- The FIR of a closure as the lowering makes it, before the ownership analysis and the build-mode
  pass. So each check of the checked mode is there, and the oracle also runs a program that the
  analysis rejects. The interpreter runs a `check` as a checked build does. A failed check ends the
  run with the runtime error of D11.4. A `check(overwrite: p)` continues.
- A whole program. The run starts as the emitted `main` of `toolchain.md` 6 item 22 starts: it
  calls `std.rt.args_init`, then `args`, then the entry `main`, then `std.rt.flush_all`.
- A FIR test function, with the arguments that the test gives. `alloc` and `del` are operations of
  the interpreter.
- The externs of this list, and no others. A `call extern` of one of them runs, from `std.rt` or
  from the program:
  - `exit`, `write`, `calloc`, `free`, `abort`, `isatty` and `strlen`;
  - `memcpy`, `memmove` and `memcmp`;
  - `__errno_location` and `__error`, the errno accessors of Linux and of Darwin;
  - `strtod` and `strtof`, each with a null end pointer;
  - `snprintf` only with the arguments of `std.libc.snprintf_float`: a buffer, a size from 1 to
    4096, the format `%.*e`, an `i32` precision and an `f64`.
  Three model externs serve the tests: one keeps its pointer argument in a hidden slot, one calls
  its function-pointer argument once with the other arguments, up to the parameters of that
  function, and one returns its pointer argument. Any other extern, and
  a call of a listed extern in another form, ends the run as `outside`, which is no oracle case.
  When the oracle keeps the output of a run, a `write` to a descriptor other than 1 and 2 also
  ends the run as `outside`.
- A call of `exit` ends the run at once, with no `leak at exit` test. That test runs only when the
  entry `main` returns.
- The memory: a 64-bit address space that the interpreter never reuses. Each global, frame local
  and allocation gets a new range. A pointer is an address, so a `cast` to `u64` works (D3.14).

### 3.3 The shadow state and the tags

- Each allocation has a range, a state (`live` or `released`) and an owner count: the number of
  owner slots that hold its start.
- Each aligned 8-byte word of storage is a slot. A store of an owning value marks the pointer word
  that it writes as an owner slot of the allocation whose start it holds. A `move` or a `del` that
  zeroes an owner slot marks it vacated. Any other store clears the marks of the slots it writes.
- The slots of a local die at its `dead` marker and at the return of its frame. The slots inside an
  allocation die at its release.
- The interpreter tests the owner counts at the end of each statement and terminator. During that
  test, the value that a `move` operand passes to the destination of its statement, and the value
  that a `return` passes in `_0`, count as owners. So a transfer makes no event.

A tag names an origin and the function F whose run made it. A value keeps its tags through a
`copy`, a `cast`, an `addr` or a `slice` of a place through it, and an arithmetic rvalue. A
comparison gives a `bool`, which has no tag.
- **Frame tag** `(F, x)`: the address of fixed storage of a local `x` of F.
- **Owner tag** `(F, o)`, made by F: F runs a `copy` of a local owner `o` of F or of `o.ptr`, a
  `slice` of `o`, or an `addr` of a place through `o`. A lend through a pointer counts too, such as
  a `copy` of `*pp` when `pp` holds the address of `o`.
- **Owner tag** `(F, o)`, made by a callee: the callee lends from `o` through a pointer that F
  passed in that call, or through a pointer that it derived from one. The interpreter numbers the
  pointers of each call for this test. The value gets the tag when the call returns it to F as its
  result, and only then. Any other lend that a callee makes from `o` gets no tag.
- **Kept**: a callee marks a value kept when it stores a value with a frame tag of F outside its
  own frame. A cast to an integer type that a callee runs removes each tag of F from the value.

### 3.4 The events

Each event names the function, the FIR location and one of these kinds:

| event               | when                                                                  |
|---------------------|-----------------------------------------------------------------------|
| `overwrite`         | a store writes an owner slot that holds a live allocation             |
| `leak`              | the owner count of a live allocation becomes 0 with no `overwrite`    |
| `leak at exit`      | at the end of `main`, no owner chain from a global reaches a live one |
| `double release`    | a `del` of the start of a released allocation                         |
| `invalid release`   | a `del` of an address that is no start of an allocation (D10.7)       |
| `use after release` | a load or a store in the range of a released allocation               |
| `use after move`    | a load or a store through a null that a vacated slot gave             |
| `use after scope`   | a load or a store in a slot of a dead local or of a returned frame    |
| `duplicate owner`   | an owner count becomes 2, or a `del` frees through no owner slot      |

`leak at exit` finds the allocations of a cycle of owners, whose counts never become 0. A run also
has an outcome: its output, its exit status, or its runtime error. A load through a null that no
vacated slot gave is the outcome `null dereference`, which is no ownership event.

Each event belongs to at most one rule, by the scope of that rule (sections 5.2 and 6). An event
that no rule covers is counted apart, and it is no false accept:

| event and condition                                              | rule            |
|------------------------------------------------------------------|-----------------|
| `overwrite` at a store into an `own` reference place             | (a)             |
| `overwrite` at a store of an owning aggregate or of another type | none (D17.11)   |
| `leak` at the `dead` or `return` of a local owner                | (c)             |
| `leak` at a `del` of an object, from an owner slot inside it     | (c)             |
| `leak` at a `del` of a span, from an owner slot in an element    | (c) or none: Q7 |
| `leak at exit`                                                   | none (X3)       |
| `use after move`, when F reads the vacated slot of a local of F  | (b)             |
| `use after move`, when a callee reads that slot                  | none (X2)       |
| `use after release`, `double release`, through an owner tag      | (e) part 1      |
| `use after scope` through a frame tag that is not kept           | (e) part 2      |
| `invalid release`, `duplicate owner`                             | assumption A2   |

### 3.5 The tests of the oracle

- The run corpus: each program of `test/lang/run` and `test/lang/programs` whose externs are in
  the list of 3.2, in the forms of that list. The oracle gives the output, the exit status and
  the runtime error that the test expects. The test counts the programs that run, those that are
  `outside` and the differences, which must be 0. It runs in 120 s or less in CI (estimated).
- Each event has tests at its boundaries: the ends of an allocation and a span of length 0. Other
  tests cover a `del` of `null` and of an interior pointer, and a cycle of two owners. More tests
  cover a tag through a cast to `u64` and back, and a kept frame tag.

### 3.6 The tests of a rule against the oracle

Three sets of programs test each rule:
1. Hand cases in FIR text and in fort: an accept case and a reject case for each item of 5.4 that
   the rule uses. A reject case has an input that gives an event of the rule, or the test lists it
   as a false reject with its reason. The 37 programs of 5.6 are hand cases from the first commit.
2. Generated cases: `test/fort/support/generator.ft` makes the families of 3.7, and the test runs
   the oracle and the analysis on each function and input in the process.
3. The tree: each build runs the analysis on the sources, and the oracle runs the run corpus.

`test/fort/owner/oracle_test.ft` prints the false accepts, the false rejects and the events outside
each scope, for sets 1 and 2. A PR merges only with 0 false accepts (retrospective rule 7).

### 3.7 The generator

The vocabulary covers each case that a rule claims:
- Locals: two `own` pointer locals, an `own` array of two elements, two index locals, two view
  locals, and a struct local with an `own` field and a view field. Also a pointer to an `own`
  local, a pointer to that pointer, a `u64` local, and an `own` global. Parameters: a pointer to a
  struct with two `own` fields, a `bool` and an index.
- Statements: `x = alloc`, `x = move y`, `del(move x)`, `x = null`, and a `del` and a `move`
  through each pointer. A lend into each view local, into the view field and into a field through
  the parameter, and a load through each view. A cast of a pointer to `u64` and back, a deferred
  `del`, a call of each model extern of section 3.2, and `panic`.
- Control: a call of a generated sibling, so that two functions form a component; an `if` on the
  `bool`; a loop of two iterations; and `return`. Inputs: each value of the `bool`, the index
  values 0 and 1, and each field of the struct parameter null or live.

The three families have their own bounds, so that each one reaches the length of its cases:
1. Exhaustive: each function of at most N statements over the vocabulary. The first ticket
   chooses N for 20 s or less in CI (estimated: N is 3).
2. Seeded: each hand case of section 5.6, with one statement of the vocabulary inserted at each
   point, or with one statement removed. That is about 37 × 8 × 40 = 11,840 functions (estimated).
3. Shapes: a chain of 1 to 4 pointers from `&x`, each a copy or the address of the one before.
   Then a release of `x`: a `del`, a `move`, a `del` through each link, or a call that can change
   `x`. Then a read through each link. Also each of 7 destinations of a borrow (a view local, a
   field, an element, memory, `_0`, a call argument, a `u64`) with each release and each read.

The generator keeps only the functions that the verifier of `fir.md` 10 accepts. The test states
the count of each family, and the three run in 60 s or less in CI.

## 4. The analysis

### 4.1 Modules

| module           | file                         | holds                                      |
|------------------|------------------------------|--------------------------------------------|
| `owner.place`    | `src/fort/owner/place.ft`    | places, keys, memory locals                |
| `owner.prover`   | `src/fort/owner/prover.ft`   | the state, the transfer, the obligations   |
| `owner.report`   | `src/fort/owner/report.ft`   | the text and the position of each error    |
| `owner.summary`  | `src/fort/owner/summary.ft`  | refinements only: summaries (section 8)    |
| `owner.alias`    | `src/fort/owner/alias.ft`    | refinement R6: the borrows, the exposure   |

Each module is one noun in the directory `owner/`, and no leaf repeats it (`notes/style.md` 3.1).
`prover` is a doer noun, so its functions start with its verb (`prover.prove`); `analysis` would
name `struct analysis` of `driver.ft` too. Tests mirror subjects: `test/fort/owner/prover_test.ft`.

There is one analysis. `owner.prover` replaces `fir.emptiness`, and the overwrite rule becomes its
first obligation. The ticket that moves it keeps every verdict of `fir.emptiness` (section 11). The
retrospective counts six analyses, each with its own model of storage (its section 2).

### 4.2 Where it runs

V1 runs where `fir.emptiness` runs today: step 5 of `fir.md` 3, one function at a time, in every
build mode and under `--check`, `--fir` and `--lib`. It runs before the build-mode pass, so it
gives one answer in every mode (`fir.md` 4 principle 7), and it adds no instruction to the IR
(D19.5). A refinement that reads a summary changes step 5 as section 8.1 says.

### 4.3 Places and keys

The tracked places are those of D17.11 and `fir.md` 11: locals, globals and places through a
pointer local that is not memory, with field projections and keyed index projections. The keys,
the separation and the freshness of D17.11 stay. Two places are the **same** when they have one
base and equal projections, and each pair of indexes has one key, which is the same index at run
time. Two places **can be the same** when they are not separate (D17.11) and not the same. Fixed
places of two different locals never overlap (`fir.md` 5.5).

A place through a pointer **can be** each memory place, of any type (premise 3): D17.4 drops `own`
at the second level with no cast, so `i32** q = &x` reads an owner `x` through a view type. The one
exception: when the last load of its access path reads an `own` reference, the place lies inside an
allocation, which shares no storage with a frame (A3, A4). An index on a span or a `string` loads
its pointer field (`fir.md` 5.5).

A **candidate** of a view local is an origin that one of its assignments can name. A pass before
the fixpoint computes the candidates, as D17.11 computes the keys before its dataflow.

### 4.4 The state

The state at a program point is a set of may-facts. The join at a block is the union
(`join_union` of `fir_flow`). `bb0` starts with the entry state, and each other block starts with
no fact, so a block that no edge reaches has no obligation that fails.

1. For each owner `p`, a set `S(p)` of the values that `p` can hold. The values are `entry`, the
   value that `p` held when the function started; `null`, a zero value that a store wrote;
   `vacated`, the zero value that a `move` or a `del` left (D17.6, D17.9); and `live`, an owning
   value that can be an allocation. The unknown state is the set of all four.
2. For each view local `v` and each candidate `o` of `v`, the fact `B(v, o)`: `v` can borrow `o`.
3. For each view local `v`, the fact `T(v)`: `v` can be stale.
4. For each local owner `o`, the fact `P(o)`: `o` can be pinned.

The entry state:
- `S(p) = {entry}` for a parameter, a global and a place through a pointer parameter;
- `S(p) = {null}` for a named local, by the entry zeroing of `fir.md` 9.2, and for a temporary,
  which holds no allocation before its first store;
- the unknown state for a place through a pointer local that is no parameter;
- no `B`, `T` or `P` fact. A view parameter borrows no origin of the function.

The emptiness fact of D17.11 for `p` is `S(p) ⊆ {null, vacated}`. So the verdicts of
`fir.emptiness` are a function of this state, and the move of section 4.1 can keep them.

### 4.5 Transfer

The order inside a statement stays that of D17.11. The reads of the operands come first, in operand
order, then the kill of a call, then the update of the store, then the destination of the borrows.
A `move` operand vacates its place after its read.

**Uses and reads.** The reads of a statement are those of `fir.md` 7, with the loads of each
access path.
- A read of a local owner `p` is a **use** of `p`. A use is a load through `p`, a `slice` of `p`,
  or a `copy` of `p`, of `p.ptr` or of a place that contains `p`. Four reads are no uses (Q5): the
  operand of `move` and of `del`, a read of `p.len`, the test of `check(overwrite: p)`, and a
  comparison of `p` or `p.ptr` with `null`.
- A load through a pointer, of any type, is a use of each memory owner (4.3). So a load through
  any alias is a use, with no chain to follow.
- A read of a view local `v`, or a load through `v`, is a **read** of `v`. Three reads are exempt:
  a `copy` of `v` into a view local, a comparison with `null`, and a read of its length.

**The borrows of a value.** A value borrows these origins:
- a `copy` of a local owner `o` or of `o.ptr`: `o`. A `copy` of a place that contains local owners:
  each of them. A `copy` of a view local `v`, or of its field 0: each origin of `B(v)`;
- a `copy` of a place through a pointer, of any type: each memory owner (4.3). It borrows no frame,
  because a frame borrow in memory is an error of rule (e) part 2, or a callee kept it (X4);
- `addr(p)` of a fixed place: the frame of its local. `addr(p)` through a pointer: the borrows of a
  `copy` of the pointer that the last load reads. `slice(p, ...)` of a fixed array: the frame. A
  `slice` of a span or a `string`: the borrows of a `copy` of `p`;
- `use`, `cast` and `slice_ptr` of a `copy` operand: the borrows of that `copy`, for each target
  type. A `move` and a constant borrow nothing;
- `aggregate`: the borrows of its members. A `call`: section 4.6. `alloc`, an arithmetic, bitwise
  or comparison rvalue: no origin. An integer operand with a tag came from a `cast` that already
  pinned its owner or failed rule (e) part 2.

**Where a borrow goes.**
- A store into a whole view local `v` sets `B(v)` to the borrows of the value. It sets `T(v)` to the
  `T` of the view locals that the value copies. A store into a field of a view local, such as the
  pointer of a span header, adds them: a weak update.
- Any other destination pins each owner that the value borrows: `P(o) = 1`. That is a field or an
  element of another local, a memory place, a global, `_0`, a local of a type that is no view, and
  a call argument (4.6). A frame origin there is the error of rule (e) part 2, except in a call
  argument.

**Updates.**
- `move p` and `del(move p)` with `p` fixed: `S(q) = {vacated}` for each owner `q` that is the same
  as `p` or below it. Each owner that can be the same as `p` or below it gets `vacated` added to
  `S(q)`: a weak update. Each local owner that changes has a release.
- `move p` and `del(move p)` through a pointer: the tracked place `p` gets `{vacated}`. Each memory
  place gets `vacated` added (premise 3). Each local owner among them has a release.
- A store of `r` into an owner `p`: `S(p)` is `{null}` for a zero constant and `{live}` for `alloc`.
  It is the value of the `move` operand that `r` holds, with `entry` read as `live` and `vacated` as
  `null`. For each other `r` it is `{null, live}`. In an owning aggregate place, each owner below it
  takes the value of its part of `r`. Each owner that can be the same gets the value added.
- A store through a pointer: the tracked place gets the value. Each other memory place gets `null`
  and `live` added: a weak update. A store straight into a memory local or a global adds them to
  each tracked place through a pointer. These are kill rules 3 and 4 of D17.11, with their two
  exceptions, so the emptiness fact dies where D17.11 kills it, and rule (a) keeps each verdict.
- A store into a pointer local `n`: each fact through `n` gets the unknown state (kill rule 1). A
  store of a fresh `new` into `n` then gives `S(q) = {null}` for each owner `q` through `n` (gen
  rule 5 of D17.11).
- A store is no release: rule (a) proves an `own` reference place empty first. A store of an
  owning aggregate, or of another type over an owner slot, overwrites with no release.

**Releases.** A release of a local owner `o` sets `T(v) = 1` for each view local `v` with `B(v, o)`.
If `P(o)` holds, the release is the error of rule (e) part 3. `dead(_n)` sets `T(v) = 1` for each
view local `v` that borrows the frame of `_n`.

### 4.6 Calls

A call applies these steps in order. A call of a fort function, a call through a function pointer
and a call of an extern are the same.
1. The reads of the callee operand and of the arguments, in operand order (4.5).
2. Each owner that an argument borrows gets `P(o) = 1` (premise 2).
3. Kill rule 5 of D17.11: each owner reached through memory gets the unknown state. Each memory
   owner has a release (4.5). So a pinned memory owner is an error at each call.
4. The result borrows each origin that an argument borrows, and each local owner of each frame
   that an argument borrows. A callee can lend each of them through the argument and return the
   lend (3.3). A result of a type that cannot hold a view borrows nothing (X4).

Step 3 adds a call to the `del` and the `move` of premise 2. A callee can release a pinned owner and
then read a pinned borrow of it, and the program can end before any later obligation of F. Only
step 3 rejects the hand case `h10` of 5.6, which does that. The user confirmed it (Q4).

A callback that an earlier call registered is no special case. A `signal` handler that a later
`raise` runs is inside the call of `raise`, and step 3 covers each owner that it can reach.

### 4.7 Obligations

| rule | site                                          | the analysis must prove                 |
|------|-----------------------------------------------|-----------------------------------------|
| (a)  | `check(overwrite: p)`                         | `S(p) ⊆ {null, vacated}` (D17.11)       |
| (b)  | a use of a local owner `p` (4.5)              | `vacated` is not in `S(p)`              |
| (c)  | `dead(_n)`; `return` for parameters and temps | `S(q) ⊆ {null, vacated}` for each owner |
|      |                                               | `q` of the local                        |
| (c)  | a `del` or an `own` cast of `p` (6.3)         | each owner of `*p` is tracked and empty |
| (e)1 | a read of a view local `v` (4.5)              | `T(v) = 0`                              |
| (e)2 | a frame origin in a destination of 4.5        | a view local or a call argument         |
| (e)3 | a release of a local owner `o` (4.5, 4.6)     | `P(o) = 0`                              |

There is one error for each site. A deferred statement expands into one site for each exit, and
the report keeps the first site of each location, as `fir.emptiness` does. An exit in an abort
block has no obligation, because a runtime error runs no deferred code either (D7.8, D11.4).

### 4.8 Work

Each transfer of a block during the fixpoint charges its statements and terminator times the facts
of the function. An owner gives 4 facts. A view local, a pair of a view local and a candidate, and a
local owner give 1 each. Past 2^24 for a function, the analysis proves no obligation of it. The
first error takes the note `the function exceeds the work limit; split it`.

With each origin as a candidate (high), the largest charges are 2,952,495 in `src/fort`
(`driver.fir_test_entry`), 11,757,610 in `test/fort` and 1,142,776 in `test/lang`. With the borrows
of the fixpoint (low) they are 976,365, 2,275,408 and 260,010 (measured, P4). None passes 2^24.

## 5. Soundness

### 5.1 The assumptions

These are all the assumptions of the rules. A program that breaks one gets no claim from the rules.
- **A1**: no operation accesses storage through a dangling pointer outside the scopes of rules (b)
  and (e). D17.11 already assumes this.
- **A2**: each allocation has at most one owner slot, and an extern whose result says `own` returns
  a new allocation (D17.13). The 15 casts of an address in `src` and `std` cast no owning value
  (measured with `grep`, then read). The oracle reports a break as `duplicate owner` or `invalid
  release`. A store of another type can copy an owner into a second slot, and that breaks A2 too. An
  example was `*q = cast(x, u64)` with `q = cast(&y, u64 mut*)`. A later `del(y)` frees through a
  slot that is no owner slot of its allocation, and the oracle reports `duplicate owner`. D3.14 now
  refuses `cast(&y, u64 mut*)`, because the view hides the owner `y`. Such a store still reaches an
  owner slot through an argument of an `extern fn` (A7) and through the three forms that A8 below
  names, which the rule of D3.14 does not read. A2 says nothing of types:
  D10.7 has no strict-aliasing rule, so no rule of V1 reads the type of a place through a
  pointer.
- **A3**: an `own` reference designates the start of a live allocation, or it is null (D17.1).
- **A4**: a frame and an allocation never share storage (D10.1).
- **A5**: one thread: no data race (D10.7).
- **A6**: the invariants of the FIR verifier hold (`fir.md` 10), such as rule V13.
- **A7**: the C code of an extern keeps A1 to A5, and it frees no allocation that fort still owns.
  D17.13 already trusts the C side of an extern signature, and V1 trusts C no further: an extern
  call follows 4.6 as each call does. No rule can check C, so the design takes A7 (section 15).
  A7 also covers a view that C gives back: a cast that is itself an argument of an `extern fn`
  may hide a pointer place (D3.14), and `memmove(dst, src, 0)` returns `dst` to fort code as a
  `u8 mut*`.
  A store through that result reaches the owner slot, so A7 asks that such a view not be used to
  write an owner slot either.
- **A8**: a value designates storage of a local of this run of F only when it derives from an
  address of that local that this run took. It derives through copies, casts, arithmetic, stores and
  loads, in F or in a callee. The user ruled on 2026-10-26: "Yes, add A8 to D10.7" (section 12).
  D10.7 now makes a read or a write of a local through a value that does not derive this way
  undefined behavior, and it gives the exact terms. Before the ruling, D10.7 listed no forged
  pointer. So a callee could return `cast(cast(p, u64) + 8, i32*)` for an argument `p`, and D10.7
  defined a read through that value. Rules (b) and (e) of V1 need A8 for such a read. Without A8 the
  read can name a local that is not memory, and it is then no use (5.5 rule (b)) and no borrow
  (lemma (ii)). Refinement R6 needs A8 for each local (section 7). Rules (a) and (c), and kill rule
  5 of D17.11, need only that no write and no release goes through a forged pointer. D3.14 gives
  that for a cast alone, because a cast from an integer marks no level `mut` and no level `own`.
  D10.6 gives it for an index in each build without `--no-bounds-check`, and D10.7 gives it for a
  slice. A same-size reinterpretation can still load a `mut` or an `own` pointer from the bytes of
  an integer. D10.7 (A8) makes an access to a local through such a forged value undefined. A write
  through a same-size reinterpretation of a value that derives from the address of the slot stays
  defined as an access, but D3.14 refuses the cast that makes the view: at the first level that
  its result marks `mut`, a cast must keep each pointer place of its source with its exact type,
  owner or not, with none hidden, none added and none changed. So
  `*cast(cast(&o, void mut*), u64 mut*) = cast(&y, u64)`, which forges the owner slot of a local
  `o`, does not compile. Nor do six forms that compiled before the user ruled "Generalize"
  (2026-10-26) and "Full static: no mut punning of pointers" (2026-10-27): a target that adds an
  owner place (`cast(&x, wrap mut*)` for a `u64 mut x` and a `struct wrap { u64* own p; }`, then
  `del(w->p)`); a dropped `own` behind a `mut` level 2 under an immutable level 1
  (`cast(slots, node mut* mut*@)` for a `node mut* own mut* mut@ own slots`, then
  `*view[0] = &local`); a store of another pointer through a `mut` level 1
  (`cast(&po, u64** mut*)` for a `u64* own mut* mut po`, then `*t = &fake`); the same store where
  the chain of the target ends (`*cast(&po, u64 mut*) = cast(&fake, u64)`); a pointer seen as an
  integer (`*cast(&slot, u64 mut*) = cast(&o, u64)` for a `u64 mut* mut slot`, then a store
  through `slot`); and a pointer in a field of a struct (`cast(&h, holder2 mut*)` for a
  `struct holder { wrapper* p; }` and a `struct holder2 { u64 mut* q; }`, then `*g->q = 1234`).
  The guarantee of rules (a) and (c) now covers an owner slot in a local, a global or an
  allocation against such views, but three forms stay outside it, and each compiled on
  2026-10-27 (probed):
  - a view that C gives back from an exempt argument (A7):
    `cast(memmove(cast(&o, u8 mut*), cast(&o, u8*), 0), u64 mut*)` for a `u64* own mut o`, then a
    store of an integer;
  - a call through a function pointer to which a cast through `void*` gave another type:
    `cast(cast(take, void*), fn (u64) void)` for a `fn take(u64* own p) void`, then a call with
    the address of a local, which `take` frees; a later change reads it;
  - a read-only view whose pointer place carries `mut` or `own` where the source holds other
    bytes, a pointer of another type included: `cast(&bits, holder2*)` for a `u64 bits` that
    holds the address of a `struct wrapper { u64* own x; }`, and `cast(&h, holder2*)` for a
    `struct holder { wrapper* p; }`, then `*g->q = 1234` over the owner `x`. No rule of D3.14
    reads a mark of a pointer place in a read-only view, and a later change forbids the form.

  An owner slot that one of these three forms forges is outside the guarantee of rules (a) and
  (c).

### 5.2 Outside the scopes

These cases are outside the scope of every rule. The oracle counts their events apart (3.4).
- **X1**: nullness: a load through a null that no vacated slot gave.
- **X2**: rule (b): a vacated slot of a local of F that a callee reads through a pointer.
- **X3**: rule (c): a leak through a cycle of owners (`leak at exit`), and a store over an owning
  aggregate (D17.11). A leak on a path that calls `exit` or ends in a runtime error. With option
  (1) of Q7, a `del` or a `cast` of a span whose elements hold owners.
- **X4**: rule (e): a lend that a callee makes from an owner of F and keeps or writes out through
  an out-parameter. Such a lend has no tag. A frame tag that a callee keeps, and each value that a
  function reaches through it. A tag of F that a cast to an integer in a callee removes (3.3).
- **X5**: rule (e): a view of an owner in a global or through a pointer, such as `p->data` in a
  callee. To cover it costs 93 more sites in 20 more functions of `src/fort`, 6 in `std` and in
  `test/fort`, and 40 in `test/lang` (measured, P4). The sources hold one global owner (read).
- **X6**: no rule checks a store of another type over an owner slot, because rule (a) checks only
  `own` reference places (D17.11). D3.14 refuses the `mut` view that such a store needs, except
  for an argument of an `extern fn` and the three forms that A8 names. No rule
  checks a `double release` through a value with no tag (X4, X5).

### 5.3 The state lemma

At each point of F that a run reaches, five facts hold. The proof is an induction over the steps
of the run: each form of 5.4 keeps (i) to (v), and a join is a union, so it keeps them too.
- (i) each owner `p` holds a value of `S(p)`;
- (ii) each owner tag `(F, o)` of the value in a view local `v` names an origin of `B(v)` or a
  pinned owner. Each frame tag `(F, x)` of it names an origin of `B(v)` or a kept value. So a view
  local can read a value back from pinned storage, such as a view from a global;
- (iii) if a release of `o` or the end of the frame of `x` came after `v` got its value, and that
  origin is in `B(v)`, then `T(v) = 1`;
- (iv) if a value with an owner tag `(F, o)` is in storage other than a view local of F, also in an
  argument of an active call, then `P(o) = 1`;
- (v) a value with a frame tag `(F, x)` is in a view local of F, in an argument of an active call,
  or in a value that a callee kept.
- (vi) with refinement R6 (section 7) and A8: a value in storage that derives from an address of a
  memory local `x` that this run took is in a local `v` that is not memory, and then `B(v)` names
  the frame of `x`, or names `exposed` while `x` is exposed; in any other storage, `x` is exposed.
  `spec/fir.md` 11 (Alias) gives the proof for each form of 5.4.

### 5.4 The forms of `fir.md` 5.5 to 8

Places (5.5):
- A fixed place `_n` with fields and keyed indexes: one storage at run time. Equal keys give one
  index (D17.11), so an exact update is strong. Two fields of one object never overlap. Two keys
  of an index `p[_i]` on a fixed array can hold one value, so each place that can be the same gets
  a weak update. An index whose local is no key can be each element, so each gets a weak update.
- `global m.NAME`: storage of no frame, so it holds no local owner. A frame origin into it is an
  error, and an owner origin into it is a pin (4.5).
- A dereference `(*p)`, and an index on a span or a `string`: the storage can be any memory place,
  of any type (premise 3). 4.5 updates or reads each one. When the last load reads an `own`
  reference, the place lies inside an allocation (A3, A4), and it can be no local owner.

Operands (5.6):
- `copy p`: the uses and the reads of 4.5 test (i) and (iii). The value gets the borrows of 4.5,
  which keeps (ii), (iv) and (v).
- `move p`: the read, then `p` holds zero. The update is strong for the same place and weak for
  each place that can be `p`, so (i) holds. Each release sets `T` and tests `P`, so (iii) holds.
- `const c`: no read. `null` and `zero` give `{null}`; another constant owns nothing (`fir.md` 5.6).

Rvalues (6):
- `use a`, `cast<T>(a)` and `slice_ptr(a, lo, hi)`: the value keeps the tags of `a` for each `T`,
  and the borrows of 4.5 follow them. A `cast` that carries `own` to another pointee type has the
  obligation of rule (c) part 2.
- `add` to `ge`, the overflow tests, `and`, `or`, `xor`, `not`, `shl`, `shr` and the float forms: no
  pointer operand. An integer operand with a tag came from a `cast` into a local that is no view.
  That cast pinned its owner or failed rule (e) part 2, so (iv) and (v) hold.
- `addr(p)` and `slice(p, lo, hi)`: the frame tag of a fixed place or array. Through a pointer,
  the tags of the pointer that the last load reads, or of the span or `string` value at `p`.
- `alloc<T>(n)`: a new allocation with no tag: `S` is `{live}`, and the local is fresh (D17.11).
- `aggregate T(a, ...)` and `aggregate zeroed T(a, ...)`: the tags of the members. Each owner part
  takes the value of its member, and each omitted field of `zeroed` is `const zero`: `{null}`.
- `call C(a, ...)`: section 4.6. Step 2 keeps (iv) for the arguments. Step 3 keeps (i) and (iii)
  for each owner that the callee can reach: a local owner that is not memory has no address, so no
  callee can reach it. Step 4 keeps (ii): the result can carry a tag of F only through an argument
  (3.3).

Statements (7):
- `p = rvalue`: the reads, the kill of a call, the update of 4.5, then the destination of the
  borrows. A destination that is no view local pins (iv) or fails rule (e) part 2 (v).
- `del(move p)`: the reads of the loads of `p`, the obligation of rule (c), then the update of a
  `move`.
- `live(_n)`: no change. The storage of each local exists for the whole function (D19.4).
- `dead(_n)`: the obligation of rule (c). The frame of `_n` ends, so each view local that borrows
  it becomes stale (iii).

Terminators (8):
- `goto bb` and `switch(a) -> [...]`: the read of `a`, and the edges, with the union join.
- `return`: the read of `_0`, and the obligation of rule (c) for parameters and temporaries. The
  frame of F ends. By (v), a frame tag of F outlives it only in a kept value (X4). An owner tag of F
  outlives it only in pinned storage (iv). Its allocation left its owner by a release, which was an
  error, or it leaks with the frame, and then nothing releases it.
- `check(f, kind, args) -> bb`: the reads. The failure path ends the run (D11.4).
- `check(overwrite: p) -> bb`: the obligation of rule (a), after the loads of the access path.
- `fail(kind, args)` and `trap` end the run, so no obligation follows; no run reaches `unreachable`
  (D8.4).

### 5.5 The rules

**Rule (a).** An `overwrite` event at a store into an `own` reference place `p` needs a live
allocation in `p`. By (i), `S(p)` then holds `live` or `entry`, so the analysis reports the error.
The weak updates of 4.5 keep (i) for each memory place that a store through a pointer can write.

**Rule (b).** A `use after move` event in scope needs a read in F of a vacated slot of a local owner
`p` of F. A load through the null that it gave comes later. The read is a use of `p`. Either it
reads a fixed place of `p`, or it loads through a pointer, and 4.5 makes that load a use of each
memory owner, of any type. `p` is one of them, because a pointer to `p` makes `p` memory. A place
inside an allocation is no slot of a frame (A4). By (i), `vacated` is in `S(p)`, so the analysis
reports the error.

**Rule (c).** A `leak` event at the `dead` or the `return` of a local owner `p` needs a live
allocation in `p`, so by (i) `S(p)` holds `live` or `entry`. A `leak` event at `del(move p)` from an
owner slot inside `*p` needs a live allocation in an owner `q` through `p`. If `q` is tracked, by
(i) `S(q)` holds `live` or `entry`. If it is not tracked, the obligation fails at once. A `cast`
that carries `own` to another pointee type can drop `q`, and it has the same obligation, so the
later `del` of the new pointer needs none. A span with owners in its elements has no tracked
element, so its `del` always fails (Q7).

**Rule (e), parts 1 and 3.** Take a `use after release` or a `double release` event through a value
with the tag `(F, o)`. The allocation was released after `o` lent it. By A2, `o` was its one owner
slot, so the allocation left `o` by one of three things. A `move` or a `del` of `o`, or of a place
through a pointer that can be `o` (premise 3). Or a call while `o` is a memory owner, because only
a call can reach a memory owner from outside F. Each is a release in 4.5 or 4.6. If the value was
in a view local `v` at that release, (ii) gives two cases. If `B(v, o)` holds, `T(v) = 1` by (iii),
so the later read of `v` is an error. Else `o` is pinned, because `v` read the value back from
pinned storage, so the release is an error. If the value was anywhere else, `P(o) = 1` by (iv),
so the release is an error. A read in a callee after a release in that callee is that case too:
the value reached the callee in an argument or in storage, and both pin.

**Rule (e), part 2.** Take a `use after scope` event through a value with a frame tag `(F, x)` that
is not kept. The frame of `x` ended at its `dead` marker or at the return of F. By (v), the value
was in a view local, which `dead` makes stale and which the return ends, or in an argument of an
active call. A callee that keeps the argument marks it kept, and a copy that a view local reads back
stays kept (X4). Every other destination is an error of part 2. So the read is through a stale
view local, which is an error of part 1.

### 5.6 The cases of the reviews

V1 rejects each of the 37 hand cases of 3.6 (measured with P4, or read where the compiler rejects
the program before `--fir` prints it). The ticket log gives the rule for each program.

| case                                             | the rule of V1 that rejects it              |
|--------------------------------------------------|---------------------------------------------|
| `g1`: `box{&x}`, then a `del` through a chain    | (e) part 2 at `box{&x}`; (a) after the call |
| `g2`: `put(&arr[k])`, then `arr[0] = new`        | (a): the call kills `arr` (kill rule 5)     |
| `g3`: `pp = &x; del(x); **pp`                    | (b): the load through `pp` is a use of `x`  |
| `g4`: `cast(n, u64)`, then `del(n)`              | (e) part 3: the cast into `u64` pins `n`    |
| `g5`: `ps = &s; del(s.buf); *ps->buf`            | (b): the load is a use of `s.buf`           |
| `g6`: `holder{n}`, then `g(&n, &h)` and `del(n)` | (e) part 3 at the call and at `del(n)`      |
| `g7`, `g8`: `v = x; del(*pp); *v`                | (e) part 1: the weak update stales `v`      |
| `h10`: `g6` that ends in `panic` after the call  | (e) part 3 at the call (4.6 step 3) only    |
| `n1`, `n1b`: `i32** q = &x`, then `del(x)`       | (e) part 1 at `*v`; (b) at `**q`            |
| `n2a`, `n2b`: a `move` or a cast, then `del`     | (c) part 2 at the `del`, or at the cast     |
| `n3`, `n4`: `view_into(&b, &s)`, then a release  | (c), (b); its event is outside (e) by X4    |
| the first review: `fa1` to `fa13`                | (a), (c), (e) parts 1 to 3; see the log     |
| the hand cases `h1` to `h9`                      | (b), (c), (e) parts 1 to 3; see the log     |

## 6. The rules

Each rule follows the pattern of the overwrite rule (`fir/emptiness.ft` and its history). A
decision comes first, then a report-only mode that lists each site, then a migration, then the
error. A migration splits by directory when it passes one agent-day. The overwrite migration
(f022527f) used seven forms of edit, such as a `del` before the store.

The probe P4 of section 14 runs V1, and the column R1 adds the first refinement of section 7. Each
cell gives sites/functions (measured with P4). The direction says how the real count can differ.
The probe gives no key to an index that a local holds (high), and it loses the type of 72 places of
`src/fort` (low, section 14). `?` means both directions. `pass` marks a row that the report-only
mode of the real pass measured in place of P4. No cast in the corpora carries `own` to
another pointee type with owners, so part 2 of rule (c) has 0 sites at a cast (measured, P4 and
`--leak-report`).

| rule and variant                 | src/fort  | src/lsp | std   | test/fort    | test/lang | dir  |
|----------------------------------|----------:|--------:|------:|-------------:|----------:|------|
| (b) local, not memory            | 0/0       | 0/0     | 0/0   | 0/0          | 0/0       | ?    |
| (b) memory owner, direct         | 22/11     | 0/0     | 0/0   | 53/31        | 99/29     | ?    |
| (b) memory owner, by a pointer   | 39/15     | 0/0     | 0/0   | 103/50       | 6/3       | ?    |
| (c) `own` local, parameter, temp | 2/2       | 0/0     | 0/0   | 0/0          | 32/24     | pass |
| (c) owning aggregate local, V1   | 199/99    | 1/1     | 2/1   | 2,424/2,354  | 93/77     | ?    |
| (c) owning aggregate local, R1   | 106/48    | 1/1     | 0/0   | 2,116/2,076  | 35/30     | ?    |
| (c) part 2, a pointer            | 14/13     | 2/2     | 0/0   | 0/0          | 41/18     | pass |
| (c) part 2, a span (Q7)          | 29/21     | 4/4     | 0/0   | 2/1          | 8/5       | ?    |
| (d) a maybe-vacated `del`        | 5/4       | 0/0     | 0/0   | 1/1          | 63/34     | ?    |
| (e)1 stale view local read       | 122/44    | 0/0     | 0/0   | 2,137/699    | 39/18     | ?    |
| (e)2 frame origin stored         | 54/38     | 2/2     | 0/0   | 1,093/465    | 63/27     | ?    |
| (e)3 pinned owner: `del`, `move` | 83/43     | 0/0     | 21/11 | 222/143      | 178/80    | ?    |
| (e)3 pinned owner: a call (4.6)  | 1,269/92  | 0/0     | 2/1   | 16,249/1,846 | 1,296/64  | ?    |
| (e) all parts                    | 1,400/128 | 2/2     | 23/11 | 17,193/1,981 | 1,552/134 | ?    |

The causes of the (e) counts in `src/fort`, from variants of P4 (measured, `?`). A variant other
than R1 and R6 is a bound, and it is not sound:
- Premise 3 makes each copy through a pointer, of any type, borrow each memory owner. With
  refinement R6 instead (section 7), (b) falls from 61/26 to 29/13 and (e) from 1,400/128 to
  900/113. In `test/fort`, (b) stays 154/79 and (e) falls to 16,755/1,967.
- Step 4 of 4.6 gives a result each local owner of each frame that an argument borrows. Without it,
  (e)3 at a call falls from 1,269/92 to 1,116/70; with R6 too, to 229/20. In
  `diag.set_text(session.diags(&s), true)` the result borrows each owner of `s`, the argument pins
  them, and each later call fails.
- With results that borrow nothing, (e)2 falls from 54/38 to 8/7: `return containers.pool_intern(p,
  strbuf.view(&b))` in `driver.join_path` puts a frame borrow of `b` into `_0` through two results.
- With R1, R6, results without those owners and no pin by a call argument, (e) is 162/57
  (`test/fort` 2,048/592, `test/lang` 242/52): the floor of R3, R4 and R6. Without R6 it is 696/96.

### 6.1 (a) Overwrite

D17.11 holds the rule, and it is an error since d8a9e096. V1 keeps it. The weak updates of 4.5 end
the emptiness fact where kill rules 3 and 4 end it, so each verdict stays.

### 6.2 (b) A use after `move` or `del`

D17.21 holds the rule. It starts from the proposed text below and the ruling on Q5 (section 12).
It differs from that text in these points:
1. Refinement R6 (D17.11, Borrows) narrows "also through any pointer". A `move`, a `del` or a read
   of a place through a pointer reaches a memory owner only when the place can be a place of its
   local.
2. A read of a place that can be `p` and does not contain it is a use too, when an index of the
   place is no key or the place is through a pointer, because the proof of rule (b) in 5.5 needs
   it. A read of a tracked element `b[j]` uses `b[j]` only (point 9).
3. The exempt reads of Q5 hold for a place in fixed storage. The text says "A load through a
   pointer, of any type, is a use of each memory owner". The user ruled on 2026-10-27 that the
   operand of a `move` or a `del` and a comparison with `null` through a pointer are no uses. A
   read of a `.len` through a pointer stays a use, because the analysis reads no type there.
4. The user ruled on 2026-10-27 that the edge where a test finds a value that is not `null` ends
   the vacated state. The test takes each form of gen rule 6 of D17.11, `&&` and `||` included,
   and the test of `p.ptr` for a span or a `string`.
5. The implicit `move` of `return x` vacates `x` (D17.5). Inside one statement, a `move` operand
   vacates its place right after its read, in the order of `fir.md` 7.
6. A store ends the vacated state only when no store into a key of its destination follows it.
7. The message says "can be vacated", and there is one error for each location and each local
   whose owner the use reads.
8. The rule starts as a report, as D17.20 did. It also gives the position of the error, the choice
   of the note, `<place>` and a limit of work.
9. A read of a tracked place in fixed storage, such as `b[k]`, is a use of that place and of the
   owners below it only. A read through a pointer, or through an index that is no key, is a use of
   each owner that it can overlap.

Proposed text: Take a local owner `p`. A use of `p` is a compile error when the analysis cannot
prove that `p` is not vacated there. `p` is vacated after a `move` or a `del` of `p` or of a place
that contains it, until a store into `p`. A `move` or a `del` of a place that can be `p` gives `p`
the value `vacated` too, also through any pointer. When `p` is memory, each call gives `p` the
unknown state, which holds `vacated`. A use is a load through `p` (`*p`, `p->f`, `p[i]`), a slice of
`p`, and a copy of `p`, of `p.ptr` or of a place that contains `p`. A load through a pointer, of any
type, is a use of each memory owner. The ruling on Q5 fixes which reads are uses. The message is
`use after move or del: '<place>' can be empty here`. A note stands at one `move`, `del` or call
from which a path reaches the use: `'<place>' is moved here`, `'<place>' is deleted here` or
`'<place>' can change in this call`.

- What it cannot prove: a use of a memory owner after any call, and a use after a `move` of a place
  that can be the same. Each is an error. The fix moves the access into a function that takes a
  pointer, where the owner is no local owner of the function (X2).
- Migration (measured, P4): 0 sites for owners that are not memory; 26 functions of `src/fort`, 79
  of `test/fort` and 31 of `test/lang` for memory owners. In `src/fort`, each site follows a call:
  a read of a field of a memory local (`fir.func_predecessors`) or a load through a pointer
  (`check_stmt.check_range_for`, read).
- Size: decision S, report M, migration M, error S.

### 6.3 (c) A live owner at the end of its scope, and a shallow `del`

Proposed text, part 1: Take a named local at the end of its scope, or a parameter or a temporary at
a `return`. The analysis must prove that each local owner in it holds the zero value. Otherwise the
exit is a compile error, because the allocation can leak: `leak: cannot prove '<place>' is empty at
this exit`, with the note `del or move '<place>' before this exit`. The error stands at the `dead`
marker or the `return` of `fir.md` 9.5: the closing brace, or the `return`, `break` or `continue`
that leaves the scope. An exit into an abort block has no obligation.

Proposed text, part 2: Take `del(p)` where the type of `p` is `T* own`, of each mutability, and `T`
holds owners in its fixed storage. The analysis must prove each owner of `*p` empty, as a tracked
place of D17.11 through `p`. Otherwise the `del` is a compile error, because it frees an object
whose owners can hold allocations: `leak: cannot prove '<place>' is empty before this del`. A `cast`
that carries `own` from `T* own` to `U* own`, where `U` is another type, `void` included, has the
same obligation for each owner of `T`. When `p` is no pointer local that is not memory, D17.11
tracks no place through it, so the `del` or the `cast` is an error. By the ruling on Q7, a span
whose elements hold owners, for which no element is tracked, waits for R8.

- What it cannot prove: a value that the owner holds on some paths only, and a memory owner after a
  call. Each is an error. `del` is always a fix for part 1: it frees what the function still owns,
  and it does nothing on a vacated or a null owner (D17.9). For part 2, the fix moves each inner
  owner into a local first.
- Migration of part 1 (measured with `--leak-report`, then read; the row `pass` of the table is the
  count of the real pass, which replaces the 1/1 and 26/21 of P4): the `own` sites of `src/fort`
  are `containers.pool_free`, whose loop ends when `block` is null, so a `del(block)` after the
  loop fixes it, and `fir.interpreter.make_region`, a module after the probe, whose `return 0`
  after `if (p == null)` finds `p` null (R7). In `test/lang`, most programs end `main` with a live
  owner. The report counts each owner at each exit: two exits there each leak two locals, and the
  three tests of `fail/` whose overwrite error stops `--fir` add 4/3, so `run` and `programs` give
  26 locations in 21 functions, as P4 did. Most aggregate sites are memory locals after their last
  call. A test environment whose `close` frees two fields with two calls is an example: the second
  call kills the fact of the first, also with R1.
- Migration of part 2 (measured with `--leak-report`, then read; the row `pass` of the table is the
  count of the real pass, which replaces the 11/10, 2/2 and 34/16 of P4, and no site is a `cast`).
  Of the 14 sites of `src/fort`, 8 free an element of a vector of owning pointers before a store
  into it (`types.reserve`, `types.node_new`, `ast.arena_reserve`, `ast.ast_new`,
  `modules.owned_reserve`, `modules.admit`, `modules.used_reserve`, `modules.use_library`), as the
  2 sites of `src/lsp` do (`lsp.json.chunks_reserve`, `lsp.json.add_chunk`): no pointer local
  names the element, so no place through it is tracked, and the element is null at run time. 4
  free an object after a call that frees its inner owners (`ast.ast_arena_free`,
  `fir.interpreter.free` and two in `modules.free`), and the call kills their facts.
  `containers.pool_free` frees `block` after its loop, where it is null (R7), and
  `fir.interpreter.make_routine`, a module after the probe, frees a fresh `made` after a call. The
  41 sites of `test/lang` stand in 4 programs and 3 tests of `run/ownership`. The shallow `del`s of
  `programs/expr_eval.ft` (320:11) and `programs/json.ft` (439:11, 441:11) leak at run time
  (`EXPECTED_EVENTS` of `test/fir_run_test.py`). The pass and P4 were not compared site by site.
  A span with owners, as the span of `bits` in `fir.emptiness`, adds 21 functions (Q7).
- Size: decision S, report M, migration M (the `own` locals, part 2) or L (the aggregates), error S.

### 6.4 (d) A double `del`

There is no error for rule (d). D17.9 empties the operand of `del`, so `del(p); del(p);` frees once.
A double release at run time needs two owner slots of one allocation, which A2 excludes, or a `del`
through a view of a released allocation, which is rule (e). The ruling on Q3 keeps this reading.

### 6.5 (e) A view that outlives its owner or its frame

Proposed text, part 1: Take a local owner `o`. A lend of `o` (D17.4), `o.ptr`, a slice of `o` and an
address through `o` borrow `o`. When `o` is memory, a copy through any pointer, of any type,
borrows it too. A call result borrows `o` when an argument borrows `o` or its frame. A copy of a
view borrows what that view borrows. Each view local that borrows `o` is stale after a `move` or a
`del` of a place that can be `o`, and after each call when `o` is memory. A read of a stale view
local is a compile error, except a comparison with `null` and a copy into a view local, which is
stale too.

Proposed text, part 2: The address of fixed storage of a local borrows the frame of that local.
A frame borrow that goes anywhere other than a view local or a call argument is a compile error
at that store, also inside an aggregate or through a cast: `the address of '<local>' outlives its
frame`. A view local that borrows the frame is stale after the end of the scope of the local. This
part amends D6.7, which leaves the return of the address of a local undiagnosed.

Proposed text, part 3: A borrow of `o` that goes anywhere other than a view local pins `o`. That is
a field, an element, memory, a global, `_0`, a call argument, or a local of a type that is no view.
A later `move` or `del` of a place that can be `o` is a compile error, and so is a call when `o` is
memory.

The messages: `stale view: '<view>' can name storage that '<owner>' gave up`, with the note
`'<owner>' is moved here`, `deleted here` or `can change in this call`; and `'<owner>' is pinned: a
view of it can outlive this <operation>`, with a note at the store or the call that pinned it.

- What it cannot prove: a view of a memory owner after a call, a pinned owner that the function must
  free, and a frame address in a struct local. Each is an error. For a function that lends its owner
  to a callee and then frees it, V1 has no fix but a transfer into the callee and back.
- Migration (measured, P4): 128 functions of `src/fort` and 1,981 of `test/fort`. Most sites come
  from premise 3, from step 4 of 4.6 and from the pin of a call argument (the list after the table
  of section 6).
- Size: decision S, report M, migration L in at least five tickets, error S. By the ruling on Q1,
  rule (e) waits for refinements R3, R4 and a sound R6.

### 6.6 The decisions

The new decisions take the numbers D17.20 and up, because the retrospective cites D17.13 to D17.19
of the tag `ownership-wave-final`. Each one amends D15 and D17.14 by the part that it covers.

## 7. Candidate refinements

A refinement is a later ticket. It makes one rule accept more programs, and it adds no error. Each
one has a decision of the user, a proof in the form of section 5, a review at `rev-deep`, and the
counts of its rules before and after it. A report count can change the order of the list.

- **R1 `empty` lines.** A line says that the owner at the path `q` below parameter `k` is empty at
  each return of the callee: `S((*_k).q) ⊆ {null, vacated}`. The callee proves it with the kill
  rules of D17.11, so no alias breaks it. A caller that passes `&x.p`, an `addr` temporary of a
  fixed place, gives `x.p.q` the value `{null, vacated}` after step 3. Proof obligation: the
  parameter is never written and never memory. Gain (measured, P4): (c) for aggregates falls from
  99 to 48 functions in `src/fort` and from 77 to 30 in `test/lang`. In `src/fort`, 40 functions get
  a line. The 69 components with 245 functions took at most 3 rounds.
- **R2 `kept` lines.** The callee returns with `(*_k).q` holding its entry value, so the caller
  keeps `S(x.p.q)` after step 3. It helps (b) and (e)1 for memory owners.
- **R3 `result` lines.** A line names what a result borrows: the origins of argument `k`, or the
  owner at a path through it. It replaces step 4 of 4.6. Bound (P4): with R6, rule (e) in `src/fort`
  falls from 900/113 to no fewer than 365/87.
- **R4 `keep` lines.** A line says that the callee can store a borrow of argument `k` outside its
  frame. An argument with no line pins nothing; a call that can change an owner that an argument
  borrows stays an error. Bound (P4, with R1, R3 and R6): rule (e) falls to no fewer than 57
  functions of `src/fort`. An argument with no line can also expose no frame for R6. With no
  exposure by an argument, (e)3 at a call with R3 and R6 falls from 229/20 to 78/8 (a bound).
- **R5 view places for aggregates**: a struct local with a view field tracks its borrows, with weak
  updates. It helps (e)2 and (e)3.
- **R6 alias by borrow and exposure.** A path **exposes** a memory local `x` when it sends a frame
  borrow of `x` to a destination that pins an owner in 4.5. A call argument is one of them. `x` is
  exposed at a point when a path to that point exposes it. A copy borrows the origin `exposed` when
  its place is no view local, no local owner and no part of either. Such a place is a place through
  a pointer, a global, `_0`, or a place of a local that is memory or of a type that is no view. A
  call result borrows `exposed` too. Take a place through a pointer, and the pointer that its last
  load reads. The place can be a place of a memory local `x` only when that pointer borrows the
  frame of `x`, or borrows `exposed` while `x` is exposed. It can still be each other memory place.
  4.5 applies this to each copy, load, `move`, `del` and store through a pointer.
  Proof, with A8: the lemma of 5.3 gets (vi). A value that designates storage of a memory local `x`
  is in a view local `v` whose `B(v)` names the frame of `x`, or names `exposed` while `x` is
  exposed. In any other storage, `x` is exposed. An `addr` or a `slice` gives the frame borrow. A
  copy, a `use` or a `cast` into a view local keeps it (4.5), and each other destination exposes the
  frame. Each other copy borrows `exposed`, and by (vi) its frame is exposed already. A callee
  reaches a frame of F only through an argument or through memory. So it writes and returns
  addresses of exposed frames only. A join is a union, and no step ends an exposure. So a place that
  R6 removes is never the storage of the access. The updates, uses and lends of 4.5 keep (i), (ii)
  and (iv) for rules (a), (b), (c) and (e). In a call result, a frame that only an exposure reaches
  is kept (X4) or failed part 2 of rule (e), as in V1, so step 4 of 4.6 stays. Five items of 5.4
  change. A place through a pointer can be a place of a memory local only as above. `copy p` borrows
  `exposed` when `p` is no view local, no local owner and no part of either. Through a pointer, it
  borrows only the owners of the frames that the pointer reaches. `addr(p)` and `slice(p, lo, hi)`
  through a pointer take the borrows of a copy of that pointer, so they can borrow `exposed` too. In
  `call`, each argument exposes the frames that it borrows, and the result borrows `exposed`. In
  `p = rvalue`, a destination that pins an owner also exposes each frame that the value borrows. R6
  narrows kill rule 3 of D17.11 for memory locals, so its decision amends D17.11. Its facts keep
  each charge of 4.8 under 2^24 (P4: 12,009,988 at most, in `test/fort`).
  **The decision.** D17.11 (Borrows) holds R6 for the overwrite rule and the leak rule, and
  `spec/fir.md` 11 (Alias) gives its FIR form and the proof of (vi); the user ratifies it. The
  decision differs from the text above in five points, and lemma (vi) holds each of them:
  1. Each local that is not memory keeps its origins, not only a view local, and each operation
     carries the origins of its operands, an arithmetic and a comparison too. D10.7 derives a value
     through each operation, so a chain of comparisons can carry an address into an integer. A
     store into an integer or a struct local that is not memory then exposes nothing.
  2. The type decides the `own` pointer: a copy of a place of an `own` pointer type borrows no
     frame, and a place whose last load reads an `own` pointer is a place of no memory local (4.3).
     The text above gives a copy of a local owner its owner origin only.
  3. The address of a place of a global borrows no origin, and the frame of an aggregate `_0`, the
     storage of the caller, is no origin.
  4. R6 narrows kill rule 4 as well as kill rule 3: a store straight into a memory local `x` keeps
     a fact through a pointer that cannot be a place of `x`.
  5. Past the charge of 2^24, the kill rules read no origin, as before R6, so the limit turns no
     verdict into an error, and kill rule 5 stays at each call (premise 1).
  Measured over the five trees: the largest charge is 3,037,320 in `test/fort` and 2,883,987 in
  `src/fort`, in 370 functions with an origin on Linux and 371 on darwin, where `std.os.exe_path`
  takes one too (4,802), each within the limit. The 22 sites of D17.11 and the 22 of part 1 of
  D17.20 stay, and part 2 falls from 23 sites to 22: `test/lang` loses `062:30:9`, whose fact
  through the `own` pointer `step` a store into the memory local `head` killed. The tests of the
  examples of D17.11, `fail/ownership` 076 and 077, add two sites of D17.11, so `test/lang` holds 24
  at the head. The oracle test keeps 0 false accepts in each set, and its exhaustive family loses 72
  false rejects of rule (a) and 873 of part 1.
  k2 fails: `keep(&x)` exposes `x`, and `p = G` borrows `exposed`. So `del(*p)` and `*p = new(i32)`
  reach `x`, and `x = new(i32)` is an error of rule (a). Its rule (b) variant fails at `*x = 1`
  (P4). Gain (measured, P4, `?`): (b) from 61/26 to 29/13 and (e) from 1,400/128 to 900/113 in
  `src/fort`. The form of revision 4 gave such a copy no frame, and it accepted k2 and the hand case
  `n4`. The ruling on A8 that the proof needs is done (D10.7, 2026-10-26).
- **R7 edges of a null test**: a branch on `p == null` proves `p` empty there. It helps (c). Its
  decision is gen rule 6 of D17.11 and the amendment of part 2 of D17.20: a branch on a test of a
  tracked place `p` against `null` makes `p` empty on the edge where the test finds `null`, and the
  kill rules apply from the read of the test on; a `del` or an owning `cast` of a `p` that holds
  the zero value frees nothing, so its obligation of part 2 holds. `fir.md` 11 (Tests) gives the
  FIR form and the proof in the form of section 5. The value of `&&` and `||` makes no fact; only
  the right operand gets the fact of the left one. Gain (measured with `--leak-report` over the
  1233 entries of the five trees before this change): part 2 in `test/lang` falls from 33 sites in
  14 functions to 23 in 11, the sites of a local, a parameter or a field that a test finds null;
  rule (a) and part 1 keep each site of those entries, and none of them changes its status or its
  errors.
- **R8 the `del` of a span of owners**: a loop rule or a builtin that empties each element (Q7).
- **R9 privacy**: a memory local whose address only view locals hold keeps its facts at a call.
  The ruling of 2026-10-23 removed it from V1. It helps (c) for aggregates.
- **R10 typed aliasing**: a place through a pointer can be only a memory place of a matching type.
  D17.4 drops `own` at the second level and D3.14 allows casts, so it needs an assumption on the
  program that the user rules on. Revision 3 measured a typed form that missed D17.4.

## 8. Summaries

V1 needs no summary. Refinements R1 to R4 need one, and this section gives its form, so that each of
them adds one kind of line to one file: `empty <path>` (R1), `kept <path>` (R2), `result <token>`
(R3) and `keep <token>` (R4). A path holds fields and constant indexes of fixed arrays below
`(*_k)`, where parameter `k` is never written and never memory. A token is `_k`, the origins of
argument `k`, or a path.

### 8.1 The order and the fixpoint

D9.5 forbids circular imports, and the library build walks the modules in the post-order of D9.11,
so a cycle of direct calls stays inside one module. Inside a module, the analysis takes the
strongly connected components of the direct calls, callees first. A call through a function
pointer or of an extern reads no summary.

A component with one function and no call to itself takes one pass. In each other component, a
must line (`empty`, `kept`) starts present, and a round can only remove it. A may line (`result`,
`keep`) starts absent, and a round can only add it. So the rounds end, and the errors of the members
come from the last round. Each fixpoint is sound for a call that returns, by induction on the
depth of the recursion. Past a charge of 2^26 for a component, each member takes the safe value of
each kind: each must line (`empty`, `kept`) is absent, and each may line (`result`, `keep`) is
present. The may lines are finite: `result _k` and `keep _k` for each parameter `k`. They say that
the result can borrow each origin of each argument and that the callee can store a borrow of each
argument, which is what step 4 of 4.6 and V1 assume of a call.

### 8.2 The text form, the hash and the rebuild rule

A library build writes `summaries/ownership`. `name` is the name of the library, `symbol` a dotted
symbol of D9.7, `digit` a byte `0` to `9`, `nonzero` a byte `1` to `9` and `nl` one newline byte.
`toolchain.md` 2.2 gives the byte rules of the file, and this grammar is the same.

```ebnf
file     = "fort-summaries 1" nl "library " name nl { function } ;
function = "fn " symbol nl item nl { item nl } ;
item     = "empty " path | "kept " path | "result " token | "keep " token ;
token    = "_" digits | path ;
path     = "(*_" digits ")" { "." digits | "[" digits "]" } ;
digits   = "0" | nonzero { digit } ;
```

- Each function with a body and at least one item has an `fn` line, in byte order of the symbol.
  The items follow the order of the `item` rule, each kind in byte order. A field is its
  declaration index, which `interface.ft` fixes (`fir.md` 13). P4 gives `std.strbuf.free` the
  line `empty (*_1).0`.
- The reader holds the form of a line and not its meaning. It takes `_0`, a `k` past the
  parameters of the function and a number past the u64 range, so a refinement that reads a line
  checks `k`, each field and each index against the declaration first.
- The bytes are a function of the inputs of `interface.ft` (D9.11) and of the summary files of
  the used libraries, in every build mode. `--emit-summaries` writes the text to stdout.
- The interface hash of D9.11 becomes the SHA-256 of one byte string. For `interface.ft` and then
  `summaries/ownership`, the string holds the file name, a newline, the byte count in decimal, a
  newline and the bytes. So the two files cannot exchange bytes. The manifest line `interface
  <hash>` names both files, and rule 4 of the validation of `toolchain.md` 2.2 tests that string.
  The `use` line of a dependent holds the interface hash, so it covers the summaries too. The line
  `summaries 1` becomes `summaries 2`, and rule 5 also requires the file.
- The key of a dependent covers the interface hash of each used library (D9.11), so a change of a
  summary rebuilds each dependent. A change of a body that keeps every summary and declaration
  keeps the hash, so the crate rule of D9.11 holds. An `fn` line with no bodiless `fn` declaration
  makes the artifact corrupt. A reader reads the file once (`toolchain.md` 2.2, Reading).
- A whole-program build, `--check` (D20.1) and the language server (D20.5) compute the summaries in
  the process. That is one analysis for each function and round; the wave ran 18.6 for each body.

## 9. Budgets

- Lines: `src/fort/owner/` holds at most 8,000 lines, with the 1,456 that move from
  `fir/emptiness.ft` (measured). V1 needs 5,000: 900 for `owner.place`, 3,600 for `owner.prover` and
  500 for `owner.report`, and each of R1 to R4 adds 400 to `owner.summary` (estimated, low). The
  oracle holds at most 3,500 more (need: 2,800), so the caps give 11,500 (Q6). The wave held 61,355.
- Each PR writes `wc -l` of `src/fort/owner/*.ft` and of the oracle in its ticket; a PR that
  would pass a cap goes to the user. The test ratio stays above `agents/lines.py --min 3.0`.
- Time: `fort --check` of `src/fort/main.ft` takes 0.94 to 0.96 s (measured: three runs, 90b50c0a,
  VM slot 4). A rule or a refinement may add 0.10 s, and the subsystem 0.50 s. Each PR writes five
  interleaved runs before and after (`notes/environment.md` 1) in its ticket.
- CI: no step audits the tree, as the wave did (1.1 to 37.9 minutes). A rule may add 3 % to the
  fort jobs of PR CI. The oracle adds the run corpus (120 s or less) and the generator (60 s).

## 10. The review protocol

1. The rule comes first: the user rules on its decision before its report ticket starts. A review
   that wants a rule to change stops and goes to the user (retrospective section 5).
2. One rule or one refinement for each PR, in the order of section 11.
3. Probes come before fixes. A soundness finding is a program that the analysis accepts and for
   which the oracle reports an event in scope. It enters the test of the PR first and fails. The fix
   makes the rule sound by construction; a special case for one program is no fix.
4. Each PR prints its false accepts against the oracle (3.6), and the count must be 0. No ledger
   keeps known false accepts. A false accept found after a merge blocks the next rule PR until its
   fix merges. Each PR also counts its false rejects, in its test or its ticket (rule 7).
5. The review depth is `rev-deep` for the oracle, the analysis and each refinement, and `rev-std`
   for a migration. The limits of `AGENTS.md` (Review Workflow) hold. A class of findings that two
   rounds find goes to the user as a rule question, and a ticket that reaches round 3 splits.

## 11. Tickets

The ids are names. A ticket starts when its dependencies merge. S is hours, M is half a day, and L
is up to one agent-day. A decision comes before each code ticket of its rule (retrospective rule 6).
The order follows the ruling on Q1. The leak checks on `own` locals and the shallow `del` come
first. The aggregates follow R1, rule (b) follows a sound R6, and rule (e) follows R3, R4 and R6.

A rule of V1 has four tickets in a row. The decision is S, for the user. The report (M, impl-hard,
rev-deep) adds its facts and gives the count and the oracle test. The migration is impl-std and
rev-std, and the error is S, impl-hard and rev-deep. A refinement is a decision (S, the user) and
one PR (impl-hard, rev-deep). The oracle and the move are impl-hard and rev-deep.
1. `oracle-core` (L), after the rulings of section 12: `fir.interpreter`, the model externs, a pass
   `run` of the FIR tests, and the run corpus of 3.5.
2. `oracle-shadow` (M), after 1: `fir.shadow`, the tags of 3.3, the events of 3.4, their tests.
3. `oracle-generator` (M), after 2: `generator.ft` with the families of 3.7, and `oracle_test.ft`
   over rule (a): 0 false accepts of `fir.emptiness`, its false rejects counted, the cases of 5.6.
4. `owner-move` (M), after 3: `owner.place`, `owner.prover` and `owner.report` with the emptiness
   fact of D17.11 only. Each verdict of `fir.emptiness` stays, and `fir/emptiness.ft` goes. Each
   fact of 4.4 comes later, with the report of the first rule that needs it, after its decision.
5. `leak-*`, after 4: rule (c) part 1 on `own` locals, and part 2. The report adds `S`. Migration S.
6. `summary-format` (M), after 5 and the library tickets L5 and L6 of the plan: `owner.summary`,
   the text form, `--emit-summaries`, the file in the artifact and the hash of section 8.
7. `refine-empty` (R1, M), after 6.
8. `aggregate-leak-*`, after 7: rule (c) part 1 on owning aggregates. Migration L, split by
   directory.
9. `refine-alias` (R6, M), after 5: it adds `B` for the frames that view locals borrow and the
   origin `exposed`. It also adds one may-fact for each memory local: the local can be exposed.
   Its decision is D17.11 (Borrows), with the five points of R6 (section 7).
10. `move-*`, after 9: rule (b). Migration M. The ruling on A8 (5.1) is done (D10.7, 2026-10-26).
11. `refine-result` (R3, L), after 6. 12. `refine-keep` (R4, L), after 11.
13. `view-*`, after 9, 11 and 12: rule (e). The report adds `T` and `P`. Migration L in at least
    five tickets. The ruling on A8 (5.1) is done (D10.7, 2026-10-26).
14. `refine-span-del` (R8, M), after 5, by the ruling on Q7.
15. `refine-kept` (R2), `refine-view-aggregate` (R5), `refine-null-edge` (R7) and `refine-typed`
    (R10): M each, after 6 or 5, only when a report count needs one.
16. `refine-private` (R9, L), after 7, only if the user rules for it.

A report ticket adds a temporary option that lists each site, as `--overwrite-report` did, and the
error ticket removes it. A ticket that changes a pass or a test method updates `notes/compiler.md`
or `notes/testing.md`.

## 12. Rulings

The user ruled on each question on 2026-10-23, each as its recommendation says:
- Q1: option (2), staged. The leak checks on `own` locals and the shallow `del` come now. Rule (b)
  waits for a sound R6, the aggregates for R1, and rule (e) for R3, R4 and a sound R6.
- Q2: (A), no annotation. Q3: (1), no error for a `del` of an owner that can be vacated.
- Q4: (a) the call of 4.6 step 3 stays; (b) what a callee keeps or writes out stays outside (X4).
- Q5: (1), the list of 6.2, now in D17.21. Q6: (1), 8,000 lines for the analysis and 3,500
  for the oracle.
- Q7: (1), a `del` or a `cast` of a span whose elements hold owners waits for R8.

The user ruled on two questions of D17.21 on 2026-10-27. Through a pointer, the operand of a
`move` or a `del` and a comparison with `null` are no uses: "Exempt move/del and null test". The
edge where a test finds a value that is not `null` ends the vacated state: "Yes, the non-null
edge clears it".

The user ruled on assumption A8 (5.1) on 2026-10-26: "Yes, add A8 to D10.7". D10.7 now lists a read
or a write of the storage of a local of a run through a value that does not derive from an address
of that local that the run took as undefined behavior. So in each program that D10.7 defines, A8
holds for each read and each write of a local. D10.7 does not cover a `del`, so it gives A8 for no
`del` through a forged `own`. The rule of 5.1 on a cast that hides an owner place is to close that
case. Items 10 and 13 of section 11, and R6 of section 7, no longer wait for a ruling.

The questions, their options and their counts stay below as the record.

**Q1. Which rules of V1 become errors, and when?**
- Functions of V1 in `src/fort` (measured, P4): (c) on `own` locals 1, (c) part 2 10, (b) 26, (c)
  on owning aggregates 99 (48 with R1), (e) 128. In `test/fort`: (b) 79, (c) on aggregates 2,354
  (2,076 with R1), (e) 1,981. With R6, (b) is 13 and (e) 113 (measured, P4). The user ruled on 11
  and 111, the counts of the unsound form of R6 in revision 4.
- Options: (1) all of V1 now, in the order (c), (b), (e), with the migrations of section 6; (2)
  (c) on `own` locals and (c) part 2 now; (b) after a sound R6; (c) on aggregates after R1, and
  after R9 if the user rules for it; (e) after R3, R4 and a sound R6; (3) another order. Section 7
  gives the sound R6 of option (2).
- Recommendation: (2). The migration of (e) in V1 touches 1,981 test functions, and a function
  that lends an owner to a callee and then frees it has no fix in V1. After R1, R3, R4 and R6, (e)
  can fall to 57 functions of `src/fort`, and no lower (section 6). The user ruled on 47, from the
  unsound form.

**Q2. May a function state its ownership contract in source, as an annotation?**
- (A) No; each refinement infers its lines. A line can change with a body, and then a new error
  stands in a dependent, far from the change. (B) An optional checked annotation on a fort function.
  It needs a decision, `grammar.md`, the parser and the interface writer, and the pins of
  `tools/bootstrap.ref` cannot build it until a pin moves (`notes/compiler.md` 8). (C) A trusted
  annotation on an `extern fn` only, which is unsafe when it lies.
- Recommendation: (A). V1 needs no contract. Ask again after the report of R4.

**Q3. Does a `del` of an owner that can be vacated become an error (rule (d))?**
- (1) No. D17.9 frees once on a vacated owner. Rule (e) makes a `del` through a stale view of a
  local owner an error, and a `del` through any other view stays undefined behavior (D10.7).
- (2) Yes. Cost: 5 sites in `src/fort`, 1 in `test/fort` and 63 in `test/lang` (measured, P4). It
  forbids the idiom "free unless returned" of `memory-model.md` 2.4, where a deferred `del` meets a
  vacated owner after `return buf`. Recommendation: (1), because (2) prevents no run-time event.

**Q4. Rule (e): the call in part 3, and what a callee keeps or writes out.**
- (a) Step 3 of 4.6 makes a call that can change a pinned owner an error; the ruling names `del` and
  `move` only. To drop the call is unsound: rule (e) would then accept `h10`, which gives a `use
  after release` in scope (5.6). So the user confirms the call, and chooses how far the refinements
  may lower its cost. In V1 it costs 1,269 sites in 92 functions of `src/fort`. Two design choices
  raise it. Step 4 of 4.6 makes a result borrow each owner of each frame that an argument borrows;
  without that part (R3) the cost is 1,116/70. Premise 3 makes a copy through any pointer borrow
  each memory owner; with R6 in its place the cost is 780/68, and with both changes 229/20. The
  user ruled on 661/63 and 26/5, from the unsound form of R6. The typed form of revision 3, which
  missed D17.4, measured 658/63 and 12/3. Recommendation: confirm the call, and let R3 and a sound
  R6 lower its cost.
- (b) Two cases are outside the scope (X4). One is a lend that a callee keeps or writes out through
  an out-parameter, as `view_into(&b, &s)` in `n4`. The other is a frame address that a callee
  keeps. The option to cover both pins each owner of each frame that an argument borrows. Cost: rule
  (e) grows from 1,400/128 to 1,635/140 in `src/fort` and from 17,193/1,981 to 26,901/2,454 in
  `test/fort` (measured, P4). Each `strbuf` call on `&b` then pins `b.data`. Recommendation: keep
  both outside the scope; D10.7 leaves them undefined.

**Q5. Which reads of an emptied owner are uses under rule (b)?**
- (1) The proposal of 6.2: a load through it, a slice of it and a copy of it, of its `.ptr` or of a
  place that contains it. A read of `.len`, a comparison with `null`, `move` and `del` are no uses.
- (2) Each read except `move`, `del` and the test of `check(overwrite: p)`. Cost (measured, P4):
  27 more sites in 11 functions of `src/fort`, 64 in 36 of `test/fort` and 245 in 69 of `test/lang`.
- Recommendation: (1), because D17.6 and D17.9 define the zero value that each exempted read sees.

**Q6. Does the line budget of the plan, "under 8,000 lines", cover the oracle?**
- (1) No: 8,000 lines for the analysis and 3,500 for the oracle. (2) Yes: 8,000 in all, which
  leaves the analysis 5,200 (estimated). Recommendation: (1); V1 with R1 to R4 is 6,600 (low).

**Q7. Does part 2 of rule (c) cover a `del` or a `cast` of a span whose elements hold owners?**
- (1) Not in V1: the case is outside the scope (X3) until refinement R8 proves each element empty.
- (2) Yes in V1: each such `del` is an error with no fix, because no element of a span is tracked.
  Cost (measured, P4): 21 functions of `src/fort`, 4 of `src/lsp` and 5 of `test/lang`.
- Recommendation: (1), because (2) forbids each span of owners that a program frees.

The coordinator decided the rest and ratified it (section 15).

## 13. The rules for the next design, checked

| rule of the retrospective (its section 8)        | where this design meets it               |
|--------------------------------------------------|------------------------------------------|
| 1. a semantic model first, tested from rule one  | 3 and 5; tickets 1 to 3 test rule (a)    |
| 2. one analysis on FIR and `fir_flow`, not six   | 4.1 and 4.4: one module, one state       |
| 3. contracts or summaries at the boundaries      | 4.6 in V1; 7 and 8 for the refinements   |
| 4. a size budget and a CI time bound for a rule  | 9                                        |
| 5. a summary once for each library, not root     | 8.1 and 8.2                              |
| 6. rules and rulings first; one rule for a PR    | 6, 10 and 11: decisions before code      |
| 7. each count in the test or ticket of its PR    | 3.6 and 10, item 4                       |
| 8. module names are nouns, nested in a directory | 3.1 and 4.1                              |

## 14. The probes

The probes are throwaway Python scripts, and they are not checked in. The coordinator keeps a copy
with the evidence of the ticket. Each probe reads the FIR text that `fort --fir` (`fir.md` 13)
prints with the release compiler of 90b50c0a in VM slot 4:

```sh
B=build/Linux/release
$B/fort --fir --std-dir $B/std -I src/fort src/fort/main.ft
$B/fort --fir --std-dir $B/std -I src -I src/fort src/lsp/main.ft
$B/fort --fir --std-dir $B/std <a file that imports the 14 modules of std and declares main>
$B/fort --fir --std-dir $B/std <flags of the test> <each of the 514 entry files of test/lang>
$B/fort --fir --std-dir $B/std -I src/fort -I test/fort/support -I src <each of 328 module tests>
```

At 90b50c0a the compiler imported its modules by short paths, so these commands name the root
`src/fort`. Since the compiler imports them as `fort.<path>` (D9.11), each `-I src/fort` above
is `-I src`.

The corpora count each function once: 1,913 in `src/fort` outside `std`, 142 in `src/lsp`, 186 in
`std`, 3,811 in the test and support modules of `test/fort`, and 1,248 in `test/lang`.

The method of **P4**, the probe of this revision: a reader parses the FIR text. A table of the
struct declarations gives the type of each projection, by name in the module and then in its
imports. A worklist runs the state of 4.4 to its fixpoint, and one more pass records each site of
4.7. The approximations:
- An index that a local holds has no key, so each of its updates is weak (high).
- The probe tracks no global and no place through a global pointer (10 reads in `src/fort`).
- The table gives no type to 72 places of `src/fort` through pointers to `node` and `module`,
  names that more than one module declares. Those places update and read no owner (low).
- The column R1 computes the `empty` lines in components, callees first (8.1).
- The struct table reads the sources of 71728460, and the FIR is that of 90b50c0a. With the table
  of 90b50c0a, (b) for a direct memory owner in `test/fort` is 49/27, not 53/31 (measured).

The variants of section 6 change premise 3 (R6, sound), step 4 of 4.6 and the pin of a call
argument (bounds, not sound). P3 of revision 3 had the type filter that this revision removes, and
P0 to P2 measured rules of revision 1.

## 15. Choices that the coordinator ratified

These choices are the coordinator's, and the coordinator ratified them on 2026-10-23:
- the oracle as an interpreter of FIR (3), and the tags of 3.3, which set the scopes of (b), (e);
- `owner.prover` in place of `fir.emptiness` (4.1), and the value `entry` (4.4);
- assumption A7 for the C side (5.1), on which revision 3 asked the user, and the facts of 4.4
  that each report ticket adds after its decision (11);
- the three families of the generator and their bounds (3.7);
- the order of the refinements (7, 11), and the file `summaries/ownership` with the hash over both
  files (8.2), which amend D9.11 and `toolchain.md` 2.2;
- the numbers D17.20 and up (6.6), and the work limits 2^24 and 2^26 (4.8, 8.1).
