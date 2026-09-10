# fort memory model

This document specifies where fort values live, how heap memory is obtained and released, what
pointers, slices and strings are at run time, which checks the compiled program performs, and
what is undefined. It implements the decisions in `decisions.md`, cited as `(Dn.m)`; where it
disagrees with `decisions.md` or `grammar.md`, they win and this document has a bug. Types and
mutability are specified in `type-system.md`; build modes and the runtime library in
`toolchain.md`.

## 1. Memory regions

| Region         | Holds                                                    | Lifetime            |
|----------------|----------------------------------------------------------|---------------------|
| stack          | locals, parameters, local fixed arrays and struct values | the enclosing block |
| heap           | storage obtained from `new`, held by `own` references    | until `del`         |
| read-only data | string literals, module-level constants `Type NAME = e;` | the program         |
| writable data  | module-level globals `mut Type g = e;`                   | the program         |

Stack storage is released when the block that declares it exits (D10.1); temporaries live until
the end of the enclosing statement (D6.3). The heap is reached only through `new` and released
only through `del` (D10.1); `new` yields an `own` reference and `del` takes one, so the type of
a value says whether freeing it is its holder's job (D17.1, section 2.3). String literals and
module-level constants are placed in read-only memory and are addressable (D3.7, D7.10). Frames
larger than one page are probed on entry, so a large local array combined with deep recursion
faults on the guard page instead of skipping over it (D10.8):

```fort
fn i32 deep(i32 n) {
    mut u8[65536] scratch = {};          // 64 KiB frame: probed page by page on entry
    if (n == 0) { return 0; }
    return deep(n - 1) + cast(scratch[0], i32);
}
```

A `string[] args` passed to `main` is built by the runtime from `argv`; each element is
NUL-terminated and lives for the whole program (D8.6, D11.6).

## 2. Allocation and deallocation

### 2.1 `new`

`new` is a keyword form that takes a type (D12.1). Every allocation is zero-initialized (D10.2),
so no value ever starts undefined, and every result is an owning reference: the `own` in its
type says that this value is the one responsible for freeing the allocation (D17.3; section
2.3).

| Form              | Result type          | Meaning                                          |
|-------------------|----------------------|--------------------------------------------------|
| `new(T)`          | `own mut T*`         | one zeroed `T`                                   |
| `new(T[n])`       | `own mut T[]`        | `n` zeroed elements; `n` is any integer type     |
| `new(T[n][K])`    | `own mut T[][K]`     | `n` zeroed rows of `T[K]`; `K` is a constant     |
| `new(T[n][K][M])` | `own mut T[][K][M]`  | `n` zeroed rows of `T[K][M]`                     |
| `new(T* own[n])`  | `own mut T* own[]`   | `n` owned slots, all `null` (D17.3)              |
| `new(T{...})`     | error                | allocate, then assign the fields                 |
| `new(T[])`        | error                | the count is mandatory                           |
| `new(void)`       | error                | `void` has no size                               |
| `new(mut T)`      | error                | the result is always fully mutable (D5.8)        |
| `new(own T)`      | error                | a prefix `own` does not parse; the result is owning |

The first bracket after the element type always holds a run-time count and always produces a
slice; later brackets are fixed-array dimensions of the element type (grammar section 6).
`new(i32[4])` is therefore an `own mut i32[]` of four elements, not an `i32[4]`. An untyped
constant count may take any integer type, and a negative constant count is a compile error
(D4.1). A negative count at run time, a total size that overflows, and allocation failure are
runtime errors (section 6); `n == 0` is allowed and yields a slice of length 0 with a non-null
`.ptr`, because the runtime allocates at least one byte (D10.2). `mut` does not parse inside
`new(...)`, and `own` only after a `*` of the element type (grammar section 6): `new(node*[n])`
yields an `own mut node*[]`, a slice of borrowed pointers, and `new(node* own[n])` yields an
`own mut node* own[]`, a slice of owned slots that are all `null` (D17.3).

The result is an rvalue that must land in an `own` place (D17.8): the initializer of an `own`
declaration, an `own` parameter, an `own` field or element, or `del` itself. Binding it to a
plain `mut T*` would leave nothing able to free it and is a compile error.

```fort
own mut point* p = new(point);       // p->x == 0, p->y == 0
own mut i32[] xs = new(i32[8]);      // eight zeroes
own mut u8[][16] rows = new(u8[n][16]);  // n rows of sixteen bytes
own mut i32[] none = new(i32[0]);    // none.len == 0, none.ptr != null; del it like any other
own i32[] ro = new(i32[4]);          // owned, read-only through this binding (D5.4, D17.4)
own mut node* own[] kids = new(node* own[4]);   // four null owned slots (D17.3)
own mut node* own[] k2 = new(own node*[4]);     // error: prefix own inside new does not parse
del(new(point));                     // allocated and freed in one statement
mut point* q = new(point);           // error: owning temporary would leak (D17.8)
own mut point* q2 = new(point{1, 2});  // error: new takes a type, not a literal
own mut i32[] bad = new(i32[]);      // error: new needs an element count
own mut void* v = new(void);         // error: cannot allocate void
own mut i32[] neg = new(i32[-1]);    // error: negative constant count (D4.1)
own mut i32[] neg2 = new(i32[k]);    // runtime error when k == -1: negative allocation count -1
```

### 2.2 `del`

`del(x)` is a universe function that yields no value (D12.2). Its operand must have an `own`
type, of any mutability: an `own` pointer, an `own void*`, an `own` slice or an `own string`
(D17.9). `del` frees the allocation the operand designates (D10.3) and, when the operand is an
lvalue, empties it: the pointer becomes `null`, the slice or string `{null, 0}` (D17.9, D17.6).
Emptying is not an assignment, so the binding need not be `mut`; an operand reached through an
indirection (`*p`, `p->f`, `s[i]`) needs that level to be mutable, because the store is visible
to everyone else who holds the pointer or slice (D17.6). A zero operand is a no-op, so
`del(buf); del(buf);` frees once, and a use after `del` dereferences `null` instead of freed
memory (D17.9). On an rvalue operand `del` only frees.

| Operand                                            | Effect                                    |
|----------------------------------------------------|-------------------------------------------|
| `own T*` or `own mut T*` lvalue from `new(T)`      | frees the object; the operand is `null`   |
| `own void*` holding an allocation address          | frees it; the operand is `null`           |
| `own T[]` or `own mut T[]` from `new(T[n])`        | frees the elements; operand `{null, 0}`   |
| `own T[][K]` from `new(T[n][K])`                   | frees the rows; operand `{null, 0}`       |
| `own string` (D17.12)                              | frees the characters; operand `{null, 0}` |
| an `own` rvalue: a call result, `move(x)`, a cast  | frees; there is nothing to empty          |
| `null` (the literal adopts `own void*`)            | no-op (D17.9)                             |
| a zero slice or string                             | no-op                                     |
| `T*`, `T[]`, `string`, `void*` without `own`       | error: a view, not an `own` type          |
| a sub-slice `s[lo..hi]` or `s[..]`, a `.ptr`       | error: slicing and `.ptr` yield views     |
| `&local`, a literal                                | error: not an `own` type                  |
| a function pointer                                 | error: not an allocation                  |
| an integer, struct or fixed array                  | error: `del` is shallow (D17.7)           |

```fort
own mut node* n = new(node);
del(n);                              // n == null afterwards
del(n);                              // no-op
n->value = 1;                        // dereferences null: a segfault, never a write to freed memory
own mut i32[] xs = new(i32[8]);
i32[] view = xs;                     // lends: view designates the same elements (D17.4)
del(view);                           // error: cannot del 'view': not an own type (D17.9)
del(xs[2..4]);                       // error: cannot del a sub-slice: slicing yields a view (D17.9)
del(xs.ptr);                         // error: cannot del a .ptr: it is a view (D17.9)
del(null);                           // no-op
own mut i32[] empty = {};
del(empty);                          // no-op
string s = "abc";
del(s);                              // error: cannot del a string: not an own type (D17.12)
own string d = str.dup(s);           // an owned copy (D13.5)
del(d);                              // frees the copy (D17.12)
i32 local = 1;
del(&local);                         // error: a stack address is not an own value (D17.9)
del(cast(s, u8[]));                  // error: not an own type; the cast lends a view (D17.12)
del(new(node));                      // frees the temporary: an own rvalue
```

Through an indirection, `del` needs the level it empties to be mutable, exactly like `move`
(D17.6, D17.9):

```fort
fn void drop(node* own[] view, node* own mut[] slots, mut node* n) {
    del(view[0]);                    // error: element level of 'view' is immutable (D17.6)
    del(slots[0]);                   // fine: the slots are mutable; slots[0] == null afterwards
    del(n->next);                    // fine: level 1 of n is mutable; n->next == null afterwards
}
```

Allocations carry no header: `new` is `calloc` and `del` is `free`, so memory from C `malloc`
may be released with `del` once it is adopted, and memory from `new` may be released with
`free` (D10.3). `own` in an `extern` signature is erased and records the C side's convention
(D17.13); adoption is a `cast` that adds `own`, the same unsafe escape as a cast that adds
`mut` (D17.3):

```fort
extern fn own void* malloc(u64 n);   // void* has no target level, so no mut (D17.13)
extern fn void free(own void* p);

own mut i32* p = cast(malloc(sizeof(i32)), own mut i32*);   // not zeroed: C did not clear it
*p = 1;
del(p);                              // same allocator as free; p == null afterwards
own mut node* q = new(node);
free(cast(move(q), own void*));      // also fine: the move empties q
```

### 2.3 Ownership: `own`, `move` and lending

Whether a reference is responsible for its allocation is part of its type. `own T*`,
`own void*`, `own T[]` and `own string` designate the start of a live allocation from `new`, or
adopted from C, on which `del` is meaningful; `T*`, `T[]`, `void*` and `string` without `own`
are views, which may be read and written through as their mutability allows but never freed
(D17.1). `own` is part of type identity and is erased at run time (section 2.5). It marks one
level only: a prefix `own` marks the reference the binding holds, an `own` right after a `*`
or `[]` marks the reference that suffix introduces, and `own mut` reads "owned and fully
mutable" (D17.2; `type-system.md` has the placement rules and the identity rules). Two rules
shape every idiom in this document:

- **Transfer is written `move`.** An `own` lvalue is copied into another `own` place (a
  declaration's initializer, an assignment, an `own` parameter, an `own` field or element of a
  literal, a `return` operand that is not a local) only as `move(lv)`, which yields the value
  and empties the operand to `null` or `{null, 0}` (D17.5, D17.6). An `own` rvalue (`new(...)`,
  a call result, `move(...)`, a `cast` to an `own` type) flows into an `own` place as it is, and
  `return x` of an `own` local or parameter is an implicit move (D17.5). Emptying is not an
  assignment: the binding need not be `mut`, but an operand reached through `*p`, `p->f` or
  `s[i]` needs that level to be mutable (D17.6). Moving a zero value yields a zero value.
- **Lending is implicit.** `own X` converts to `X` wherever a value meets an expected type,
  exactly like dropping `mut` (D5.4), and the two drops combine (D17.4). The conversion is
  monotone: `own` at an inner level may be dropped only together with every outer level and only
  where the target keeps every level outside that reference immutable, so
  `own mut node* own[]` lends as `node* own[]` or `node*[]` but never as `own mut node*[]`,
  which would leave the nodes owned by nobody, nor as `mut node*[]`, through which a borrowed
  pointer could be stored into a slot the source still owns (D17.4). Operands of `==`, `!=` and
  `?:` lend; `?:` yields `own` only when both operands are `own` rvalues or `null` (D6.2). An
  `own` rvalue may not be lent: converting it to a non-`own` type is the compile error "owning
  temporary would leak" (D17.8), because nothing could ever `del` it.

```fort
struct node {
    i32 value;
    own mut node* next;              // an owning field: node is an owning aggregate (D17.7)
}

fn i32 value_of(node* n) { return n->value; }   // borrows: any node* or own node* fits
fn void adopt(own mut node* n) { del(n); }      // takes ownership: the caller must not del

own mut node* a = new(node);         // an own rvalue lands in an own place
node* v = a;                         // lends: v is a view of the same node (D17.4)
own mut node* b = a;                 // error: copying an own lvalue requires move(a) (D17.5)
own mut node* c = move(a);           // a == null afterwards; c owns the node
i32 x = value_of(c);                 // lends for the call
adopt(c);                            // error: passing an own lvalue requires move(c) (D17.5)
adopt(move(c));                      // c == null; adopt is now responsible for the node
adopt(new(node));                    // an rvalue passes as it is
mut node* leak = new(node);          // error: owning temporary would leak (D17.8)
value_of(new(node));                 // error: owning temporary would leak (D17.8)
own mut node* z = move(a);           // a was already null: z == null (D17.6)
```

Through an indirection the emptied level must be mutable, so nothing can be taken out of what
was merely lent (D17.6):

```fort
fn void take(node* own[] view, node* own mut[] slots, mut node* own[] all) {
    own node* p = move(view[0]);     // error: element level of 'view' is immutable (D17.6)
    own node* q = move(slots[0]);    // fine: the slot is mutable, the node is not
    own mut node* r = move(all[0]);  // fine: the prefix mut marks every level
    del(q);
    del(r);
}
```

Structs and fixed arrays that contain an `own` reference by value are owning aggregates: they
are moved like `own` references, returned from a local by an implicit move, and never `del`ed,
because `del` is shallow and a struct frees its own fields (D17.7). Functions therefore take
`list*` or `mut list*`, and a range `for` over owning elements lends its loop variable, which
is declared without `own` (D17.10):

```fort
struct list {
    own mut node* head;
    u64 size;
}

fn list make() { list l = {}; return l; }     // returning a local: implicit move (D17.7)
fn void consume(list l) { del(l.head); }      // by value: the caller writes move

mut list l = make();
list l2 = l;                         // error: copying an owning value requires move(l) (D17.7)
consume(move(l));                    // l is list{} afterwards
del(l);                              // error: del of an aggregate; del its fields instead (D17.7)
own mut node* own[] kids = new(node* own[4]);   // four null owned slots (D17.3)
for (mut node* k : kids) { }         // each k lends one element; kids is iterated in place (D17.10)
for (own mut node* k : kids) { }     // error: a loop variable cannot be own (D17.10)
own mut node* first = move(kids[0]); // taking an element out is explicit
del(first);
del(kids);                           // frees the slots; the remaining nodes must be freed first
```

`new(node*[4])` yields `own mut node*[]`, slots that borrow; `new(node* own[4])` yields
`own mut node* own[]`, slots that own what is later moved into them (D17.3). The `cast` that
adds `own` is reserved for memory from C (section 4.5).

### 2.4 Cleanup idioms

Nothing is freed automatically. The function that calls `new` either frees the storage on every
path or hands it on, and the type records which: an `own` local is freed here, an `own` result
or an `own` parameter moves the responsibility. `defer` makes "every path" mechanical (D7.8):
deferred statements run when the enclosing block exits by falling off the end, `return`,
`break` or `continue`, innermost block first and in reverse order within a block; a `return`
operand is evaluated before the deferred code runs, and a runtime error runs nothing (D7.8,
D11.4).

**Free unless returned.** Because `return buf` of an `own` local empties `buf` before the
deferred code runs, `defer del(buf);` followed later by `return buf;` frees the buffer on every
path except the one that hands it to the caller, where the deferred `del` sees `{null, 0}` and
does nothing (D7.8, D17.5, D17.9):

```fort
fn bool checksum_ok(u8[] data) {
    mut u32 sum = 0;
    for (u8 b : data) { sum = sum +% cast(b, u32); }
    return sum % 7 == 0;
}

fn own mut u8[] pattern(u64 n) {
    own mut u8[] buf = new(u8[n]);
    defer del(buf);                      // runs at every exit below
    for (mut u64 i = 0; i < buf.len; i++) { buf[i] = cast(i, u8); }
    if (buf.len < 16) { return new(u8[0]); }   // buf is freed after the empty result is made
    if (!checksum_ok(buf)) { return new(u8[0]); }   // and here, after the call completes
    return buf;                          // implicit move: buf is empty when the defer runs
}
```

The same shape delivers a result through an out-parameter: `*out = move(buf);` empties `buf`,
so the deferred `del` frees only on the failure paths (section 9.2).

**Transfer into a container.** A container holds its storage as `own` fields; pushing moves the
new element in and popping moves it out, and every field it overwrites was emptied first, so
the overwrite check of section 2.5 passes (D17.11):

```fort
fn void push(mut list* l, own mut node* n) {
    n->next = move(l->head);             // l->head is empty afterwards
    l->head = move(n);                   // stores into an emptied field
    l->size += 1;
}

fn own mut node* pop(mut list* l) {      // null when the list is empty
    own mut node* n = move(l->head);
    if (n != null) {
        l->head = move(n->next);
        l->size -= 1;
    }
    return n;
}

fn void free_list(mut list* l) {
    while (l->head != null) {
        del(pop(l));                     // an own rvalue: freed on the spot
    }
}

mut list l = {};
push(&l, new(node));                     // an rvalue moves in
own mut node* n = new(node);
push(&l, move(n));                       // an lvalue needs move
push(&l, n);                             // error: passing an own lvalue requires move(n) (D17.5)
free_list(&l);
```

**Freeing a tree.** A node whose children are `own mut node* own[]` frees each child, then the
slots, then itself; the element level is reached through a mutable path, so `move` out of the
slice is allowed (D17.6):

```fort
struct tree {
    i32 value;
    own mut tree* own[] kids;            // owned slots holding owned subtrees (D17.2)
}

fn void free_tree(own mut tree* t) {
    if (t == null) { return; }
    for (mut u64 i = 0; i < t->kids.len; i++) {
        free_tree(move(t->kids[i]));     // the slot is null afterwards
    }
    del(t->kids);                        // the slots; t->kids is {null, 0} afterwards
    del(t);                              // the node; t is null afterwards
}
```

**An arena that hands out views.** One `own` block, many borrowed sub-slices: the callers can
use the memory but not free it, because slicing yields views (D17.3), and `del` of a view does
not compile (D17.9):

```fort
struct arena {
    own mut u8[] block;
    u64 used;
}

fn mut u8[] arena_alloc(mut arena* a, u64 n) {
    if (n > a->block.len - a->used) { panic("arena exhausted"); }
    mut u8[] chunk = a->block[a->used .. a->used + n];   // a view into the block
    a->used += n;
    return chunk;
}

fn void arena_free(mut arena* a) {
    del(a->block);                       // frees every chunk at once; block is {null, 0}
    a->used = 0;
}

mut arena ar = {new(u8[4096]), 0};
mut u8[] tmp = arena_alloc(&ar, 64);
del(tmp);                                // error: cannot del 'tmp': not an own type (D17.9)
arena_free(&ar);
```

`defer` captures nothing: the statement runs with the values its variables hold at exit (D7.8,
D16), so after `defer del(p);` a later `del(p); p = move(q);` makes the exit free `q`; had the
`del(p)` been left out, the assignment would trap on the overwrite check in a checked build
before any deferred code runs (section 2.5).
Prefer `defer` immediately after the `new` it undoes, before the variable is rebound.

### 2.5 Ownership at run time

`own` is erased: an `own T*` is the same eight bytes as a `T*`, an `own T[]` or `own string`
the same sixteen as a view, with the same alignment, layout and calling convention, and
`extern` signatures with `own` name the same C functions (D17.1, D17.13). The compiler emits
exactly three things for ownership:

- **`move(lv)`** loads the operand's value and stores its zero value into the operand: eight
  zero bytes for a pointer or `void*`, sixteen for a slice or string, the whole value for an
  owning aggregate (D17.6).
- **`del(lv)`** passes the pointer (or the slice's `ptr`) to the runtime's `free` and then
  stores the zero value into the operand; `del(rv)` only frees (D17.9; `toolchain.md` 6).
- **The overwrite check.** In a checked build (D11.1), every assignment to an lvalue of `own`
  reference type loads the current pointer word after the right-hand side has been evaluated,
  immediately before the store, and traps when it is non-null:
  `<file>:<line>:<col>: runtime error: overwriting owned value`, positioned at the assignment's
  `=` (D17.11, D11.4; section 7). The previous allocation would otherwise leak silently. `del`
  and `move` leave zero behind, so `del(v.data); v.data = new(...)`, `a = move(b)` after
  `move(a)`, and a store into a slot initialized from `{}` or `null` all pass. A release build
  stores without checking. Assignments of owning aggregates are not checked field by field,
  and a declaration's initializer is never checked, because fresh storage holds nothing.

```fort
own mut u8[] buf = new(u8[4]);
buf = new(u8[8]);                    // checked: overwriting owned value; release: the old buf leaks
del(buf);
buf = new(u8[8]);                    // fine: buf was {null, 0}
own mut u8[] other = move(buf);      // buf is {null, 0} again
buf = move(other);                   // fine
mut list l = {};
l.head = new(node);                  // fine: zero-initialized field
l = list{};                          // not checked: an aggregate assignment (D17.11); leaks
```

What is not tracked, exactly as in C (D10.7, D17.14): a view, or a copy of an `own` value made
before a `move` or `del`, that is used after the allocation was freed; an `own` value that is
never freed (a leak); two `own` references to one allocation made through `cast`; and `del` of
adopted memory that is not the start of an allocation. The first and last are undefined
behavior (section 8); a leak is merely a leak. The compile-time (linear) check that would make
leaks and use after `move` errors is deferred (D15); `move` and `del` zero what they take so
that those mistakes surface as `null` dereferences rather than as writes to freed memory.

## 3. Pointers

A pointer `T*` holds the address of one `T` or is `null`. There is no pointer arithmetic, so
the sources of a pointer are exactly the following (D10.4, D10.5), and each fixes whether the
result owns what it points to (D17.3):

| Source          | Type                                 | Owning?                              |
|-----------------|--------------------------------------|--------------------------------------|
| `null`          | whatever the context expects (D10.5) | usable in `own` and borrowed places  |
| `&e`            | `T*`, level 1 per D5.8               | no: a view of `e`                    |
| `new(T)`        | `own mut T*`                         | yes                                  |
| `s.ptr`         | `T*` with `s`'s element mutability   | no: a view of `s`                    |
| `cast(e, T*)`   | as written                           | as written; adding `own` is adoption |
| a function name | its function type                    | never: `own` on it is an error       |
| a call          | the declared result type             | as declared                          |

The operations are the same on `own` and borrowed pointers, because an `own` pointer lends
itself in every operand position (D17.4); only `del` and `move` distinguish them (D6.7, D6.10):

| Expression  | Meaning                                | Requires                                 |
|-------------|----------------------------------------|------------------------------------------|
| `&e`        | address of the lvalue `e`              | an lvalue; mutability per D5.8           |
| `*p`        | the object `p` points to, an lvalue    | a `T*`, never `void*` or a fn pointer    |
| `p->f`      | `(*p).f`                               | a pointer to a struct                    |
| `p == null` | `p` is null                            | a pointer, `void*` or fn pointer         |
| `p[lo..hi]` | unchecked slice of `hi - lo` elements  | a `T*` or `mut T*`                       |

```fort
mut i32 x = 10;
mut i32* p = &x;
*p = 11;                             // x == 11
mut point pt = {1, 2};
mut point* pp = &pt;
pp->x = 5;
i32 y = pp.x;                        // error: use -> on a pointer
i32 z = p[0];                        // error: pointers cannot be indexed
mut i32* q = p + 1;                  // error: no pointer arithmetic
p++;                                 // error: no pointer arithmetic
node* n = null;
bool none = n == null;
i32[] s = {};
bool empty = s == null;              // error: compare s.len or s.ptr
bool nn = null == null;              // error: null has no type here
own mut node* o = new(node);
node* w = o;                         // lends: the same operations, no responsibility
i32 val = o->value;                  // as on w
bool same = o == w;                  // operands of == lend (D17.4)
del(w);                              // error: cannot del 'w': not an own type (D17.9)
del(o);                              // o == null; w now dangles
```

`null` is the zero pointer and function-pointer value (D10.5). Dereferencing `null` or a dangling
pointer is undefined behavior, in practice a segmentation fault (D10.5, D10.7).

### 3.1 From a raw pointer to a slice

There is no pointer arithmetic (D10.4). The only way to view memory behind a raw pointer as
elements is `p[lo..hi]`, which yields a `T[]` (or `mut T[]` from a `mut T*`) with `ptr` advanced
by `lo` elements and `len == hi - lo`, performing no check at all (D6.9). The result is a view
whether or not `p` is `own` (D17.3). It is the explicit unsafe escape for foreign memory: a
range that extends beyond the object, or `hi < lo`, is undefined behavior (D10.7). Only the
two-bound form exists for pointers, because a pointer has no length; `p[lo..]`, `p[..hi]` and
`p[..]` are errors, as is any slicing of `void*` (D6.9).

```fort
extern fn u64 strlen(char* s);
extern fn char* getenv(char* name);

fn string env_value(string name) {   // name must be NUL-terminated, for example a literal
    char* p = getenv(name.ptr);
    if (p == null) { return ""; }
    return cast(p[0..strlen(p)], string);
}
char[] tail = p[3..];                // error: a pointer has no length
void* vp = cast(p, void*);
u8[] bytes = vp[0..4];               // error: void* cannot be sliced
own mut u8* raw = new(u8);
mut u8[] one = raw[0..1];            // a view of the allocation, not a second owner
del(one);                            // error: cannot del 'one': not an own type (D17.9)
del(raw);                            // frees the byte; one now dangles
```

### 3.2 `void*`

`void*` is an address with no pointee type: it cannot be dereferenced, cannot reach fields, and
cannot be indexed or sliced; every conversion to and from it is a `cast` (D3.11). It exists for
`extern` signatures and for storing an address whose type is recovered later with `cast`.
`own void*` is its owning form (D17.1): what `malloc` returns and `free` takes (D17.13), a
legal operand of `del`, and the type a container such as `ptr_vec` would use if it owned what it
stores. A `cast` between `own void*` and a typed `own` pointer yields `own` because its target
says so, and an `own` lvalue operand must therefore be moved (D3.14, D17.5). It is never
`own mut void*`: `void*` has no target level for the `mut` to describe (D17.13).

```fort
extern fn own void* malloc(u64 n);
own void* blob = malloc(16);
own mut point* pt = cast(move(blob), own mut point*);   // blob == null afterwards
own mut point* pt2 = cast(blob, own mut point*);        // error: copying own lvalue needs move
del(pt);
del(blob);                           // no-op: blob is null
```

## 4. Slices and strings in memory

### 4.1 Layout

A slice `T[]` is sixteen bytes: a pointer to the first element followed by a `u64` element count
(D3.5). A `string` has the same layout with `char` elements (D3.7). Both have alignment 8.
Whether the header owns its elements is part of the type, never of the bits: an `own` slice or
`own string` is the same two words as a view (D17.1). A view's elements live on the stack (a
sliced local array), on the heap (`new`, seen through a lent or sliced `own` value), in
read-only data (a literal, a sliced module constant) or in foreign memory (`p[lo..hi]`); an
`own` slice's elements always start a heap allocation (D17.1).

| Offset | Word  | Type  | Holds                                                    |
|--------|-------|-------|----------------------------------------------------------|
| 0      | `ptr` | `T*`  | the address of `s[0]`, or `null` in the zero value       |
| 8      | `len` | `u64` | the element count; `s[len - 1]` is the last element      |

`.ptr` and `.len` read the two words; neither is an lvalue (D6.7). The zero value `{null, 0}` is
what `= {}` produces and what a zeroed struct field holds. There is no expression form for a
zero slice outside a declaration initializer: to reset a borrowed slice-typed field, declare a
variable with `= {}` and assign it. An `own` slice field is reset by `del` or `move`, which
leave `{null, 0}` behind (D17.6, D17.9).

### 4.2 Slicing

`e[lo..hi]`, `e[lo..]`, `e[..hi]` and `e[..]` on a fixed array lvalue, a slice or a string produce
a slice (or string) of the same elements with `ptr` advanced by `lo` elements and `len` set to
`hi - lo`; omitted bounds are `0` and `len` (D6.9). The check `0 <= lo <= hi <= len` is against
the operand's own `len`, not the original allocation, so a slice can only shrink. The result's
element mutability is that of the operand's elements (D6.9). The result is always a view, even
`a[..]` of an `own` slice or `own string` (D17.3): it cannot be `del`ed (D17.9), and it enters
an `own` place only through the adoption `cast` (D17.3), which is meant for memory from C.

```fort
own mut i32[] a = new(i32[6]);       // {p, 6}
mut i32[] b = a[2..5];               // {p + 2 * 4 bytes, 3}
i32[] c = b[1..];                    // {p + 3 * 4 bytes, 2}
i32[] d = a[..];                     // same header as a, elements immutable
i32[] e = b[0..4];                   // runtime error: slice bounds 0..4 out of range for length 3
i32[] f = a[4..2];                   // runtime error: slice bounds 4..2 out of range for length 6
mut i32[4] arr = {1, 2, 3, 4};
mut i32[] g = arr[1..3];             // points into arr's stack storage
i32[] h = make_array()[..];          // error: a fixed array rvalue cannot be sliced
own mut i32[] i = a[..];             // error: a view cannot initialize an own place (D17.3)
del(b);                              // error: cannot del a sub-slice: it is a view (D17.9)
```

### 4.3 Aliasing and lifetime

A slice and its source designate the same elements: writes through one are visible through the
other, and both are invalidated together when the storage goes away.

```fort
own mut i32[] a = new(i32[4]);
mut i32[] b = a[1..3];
b[0] = 9;                            // a[1] == 9
del(a);                              // b now dangles; using it is undefined (D10.7)
```

A `move` changes which reference is responsible, not the allocation: views made before it stay
valid until the allocation is freed. A copy made by lending (`i32[] w = a;`) is a view like any
other, and the emptied source cannot dangle, because it is `{null, 0}` (D17.6, D17.9).

```fort
own mut i32[] a = new(i32[4]);
i32[] w = a;                         // a view
own mut i32[] c = move(a);           // a is {null, 0}; w still designates the elements
i32 first = w[0];                    // fine
del(c);                              // w now dangles (D10.7); a and c are {null, 0}
i32 gone = a[0];                     // runtime error: index 0 out of range for length 0
```

Slicing a local array produces a slice into the current frame. Returning it, storing it in a
heap object, or keeping it past the block is undefined behavior, exactly as returning `&local` is
in C; the compiler does not diagnose it (D6.7, D10.7).

```fort
fn i32[] window() {
    mut i32[4] a = {1, 2, 3, 4};
    return a[1..3];                  // undefined: points into a frame that no longer exists
}
```

Copying a slice copies the header only (D8.2); the elements are shared. A `for (T x : s)` loop
evaluates `s` once before the loop and copies each element at the start of its iteration (D7.5),
so replacing `s` inside the loop does not change what is iterated.

### 4.4 Strings

A string literal is stored in read-only memory with one NUL byte after its last character that
`len` does not count; its `.ptr` may be passed to a C function expecting a NUL-terminated
`char*` (D3.7). A sub-string shares its source's bytes and is not NUL-terminated. A string built
at run time (section 9.5) is NUL-terminated only if the program put a NUL there. `std::str`
provides duplication with a trailing NUL, and standard-library functions that hand a path to C
copy it into a NUL-terminated buffer first (D13.2, D13.4). Writing through a string that was
cast to `mut u8[]` is undefined when the bytes are read-only (D10.7).

```fort
string lit = "path";                 // bytes: p a t h NUL; lit.len == 4
string sub = lit[0..2];              // "pa", no NUL after 'a'
extern fn i32 open(char* path, i32 flags);
i32 fd = open(lit.ptr, 0);           // fine: a literal is NUL-terminated
i32 fd2 = open(sub.ptr, 0);          // opens "path": C reads past sub.len to the NUL
```

`own string` is the owned form (D17.12): `str.dup`, `str.concat` and `strbuf.take` return it,
while literals, sub-strings and `sys.args()` are `string` views (D13.5). It lends as `string`
in every operand position, its sub-strings are `string` views, `del(own string)` frees the
characters and `del(string)` does not compile. A cast among `string`, `char[]`, `u8[]` and
their `mut` forms yields `own` exactly when its target spells it (D3.14, D17.12), so a string
built in an `own mut u8[]` becomes an `own string` with `cast(move(buf), own string)`
(section 9.5): the target says `own`, so the `own` lvalue must be moved (D17.5), which leaves
`buf` empty and the bytes with exactly one owner; `cast(buf, string)` lends a view instead.

```fort
own string d = str.dup("path");      // a heap copy with a trailing NUL (D13.5)
string t = d;                        // lends
string head = d[0..2];               // a sub-string: a view
bool eq = d == "path";               // operands of == lend (D17.4)
del(head);                           // error: cannot del a string: not an own type (D17.12)
del(t);                              // error: cannot del a string: not an own type (D17.12)
del(d);                              // frees the copy; d is {null, 0}, which equals ""
```

### 4.5 Handing memory to C

Slices, strings, structs and fixed arrays never cross an `extern` boundary (D9.8, D13.4). Unpack
`.ptr` and `.len`, cast the pointer to the declared C type, and pass a struct by address.

```fort
extern fn i64 write(i32 fd, void* buf, u64 n);
extern fn void* memset(void* p, i32 c, u64 n);

fn void put(string s) {
    write(1, cast(s.ptr, void*), s.len);
}
fn void clear(mut u8[] b) {
    memset(cast(b.ptr, void*), 0, b.len);
}
extern fn void sum(i32[] xs);        // error: slices cannot cross an extern boundary
```

`own` in an `extern` signature is erased and records who frees (D17.13): a result type
`own void*` says the caller frees, a parameter type `own void*` says the callee does; `void*`
takes no `mut`, having no target level (D17.13, D5.5).
Memory received from C is used through `p[lo..hi]` (section 3.1) and released with `del` or the
C library's own function, whichever the C side documents; `new`/`del` and `malloc`/`free` are
interchangeable (D10.3). Adoption, a `cast` that adds `own` to a pointer or slice, is how
memory from C enters the `own` discipline (D17.3, D3.14); handing an `own` value to a C
function that frees it is a `move` into its `own` parameter. Declare a C result `own` when the
pointer itself will be `del`ed, and plain when it will be adopted as a slice, so that exactly
one `own` value exists per allocation (D17.14). `del` of adopted memory that does not start an
allocation is undefined (D10.7).

```fort
extern fn own void* malloc(u64 n);
extern fn void free(own void* p);
extern fn mut char* read_line(mut u64* len);   // C documents: the caller frees with free()

own mut u8* raw = cast(malloc(64), own mut u8*);   // own rvalue to own type; not zeroed
mut u8[] bytes = raw[0..64];                   // a view for filling
free(cast(move(raw), own void*));              // or del(raw); raw == null either way
mut u64 n = 0;
mut char* line = read_line(&n);                // a view until adopted
own mut char[] text = cast(line[0..n], own mut char[]);   // adopted as an own slice
del(text);                                     // frees what C allocated
own mut char[] mid = cast(line[1..n], own mut char[]);
del(mid);                                      // undefined: not the start of an allocation (D10.7)
own mut u8[] b = new(u8[64]);
free(cast(b.ptr, own void*));                  // adopts a view: b is now a second owner (D17.14)
```

The last line is legal and dangerous: after it `b` still looks live, so `del(b)` would free
twice and an assignment to `b` would trap on the overwrite check. Free `own` slices with `del`.

## 5. Fixed arrays and structs

A fixed array `T[N]` occupies `N * sizeof(T)` contiguous bytes with the alignment of `T`; a
struct occupies its fields in declaration order with natural alignment and padding, rounded up
to its largest field alignment, exactly like the same C struct (D3.4, D3.8). Both are values:
assignment, argument passing and `return` copy the whole object (D8.2). In the internal calling
convention they are passed by a hidden pointer to a caller-made copy and returned through a
hidden result pointer, so a callee never sees the caller's storage (D9.9); in LLVM IR that is a
plain `ptr` parameter and a leading `ptr sret(%T)` parameter (`toolchain.md` 6 item 7).

```fort
i32[4] a = {1, 2, 3, 4};
mut i32[4] b = a;                    // 16 bytes copied
b[0] = 9;                            // a[0] is still 1
fn void zero(mut i32[4] arr) { arr[0] = 0; }
zero(a);                             // a is unchanged: arr was a copy
fn void zero_in_place(mut i32[] arr) { arr[0] = 0; }
mut i32[4] c = {1, 2, 3, 4};
zero_in_place(c[..]);                // c[0] == 0: the slice points into c
```

Multi-dimensional arrays are arrays of arrays, laid out row-major (D3.6). `i32[3][4] m` is 48
bytes; `m[i][j]` is at byte offset `(i * 4 + j) * 4`; `m[i]` is an `i32[4]` lvalue. A slice of
rows, `mut i32[][4]`, comes from `new(i32[n][4])`.

```fort
mut i32[3][4] m = {};
m[2][3] = 1;                         // offset 44
i32[4] row = m[2];                   // copies 16 bytes
mut i32[][4] rows = new(i32[n][4]);  // n rows, zeroed
rows[0][1] = 7;
rows[1] = row;                       // copies a whole row into the slice's storage
u64 k = m[i].len;                    // 4: a constant; m[i] is not evaluated (D4.6)
```

## 6. Runtime checks

Every check below is compiled into the program. A failing check is a runtime error (section 7)
except where the table says otherwise. Casts never trap (D3.14) and float arithmetic never traps
(D6.12).

| Check           | Fires when                    | Message (values in decimal)                   |
|-----------------|-------------------------------|-----------------------------------------------|
| index `e[i]`    | `i >= len`, compared unsigned | `index 5 out of range for length 3`           |
| slice `e[a..b]` | not `0 <= a <= b <= len`      | `slice bounds 2..7 out of range for length 3` |
| `+ - *`, `-e`   | result does not fit           | `integer overflow`                            |
| `++ --`         | result does not fit           | `integer overflow`                            |
| `+= -= *=`      | result does not fit           | `integer overflow`                            |
| `<< >> <<= >>=` | count `< 0` or `>= width`     | `shift count 64 out of range for i64`         |
| `/ % /= %=`     | divisor is zero               | `division by zero`                            |
| `/ % /= %=`     | `MIN / -1` or `MIN % -1`      | `division overflow`                           |
| `new(T[n])`     | `n < 0` at run time           | `negative allocation count -1`                |
| `new`           | `n * sizeof(T)` exceeds `u64` | `allocation size overflow`                    |
| `new`           | the allocator returns `null`  | `out of memory`                               |
| `lv = e`, `own` | `lv` is not zero (checked)    | `overwriting owned value`                     |
| `assert(c)`     | `c` is `false`                | `assertion failed: <text>`                    |
| `panic(m)`      | always                        | `panic: <m>`                                  |
| `noreturn` guard | a `noreturn` function returns | none: `llvm.trap` (D19.7)                    |

The message texts are fixed (D11.4); the numbers shown (the index and the length, the two
bounds and the length, the shift count and the shifted operand's type, the negative count) are
the offending values of the failing check, written as signed decimals.

| Check                       | Checked (default) | Release (`--release`)    | `--no-bounds-check` |
|-----------------------------|-------------------|--------------------------|---------------------|
| index, slice                | runtime error     | runtime error            | check removed       |
| `+ - *`, unary `-`, `++ --` | runtime error     | wraps (two's complement) | unchanged           |
| `+= -= *=`                  | runtime error     | wraps (two's complement) | unchanged           |
| `+% -% *%`, `+%= -%= *%=`   | wraps             | wraps                    | unchanged           |
| shift count                 | runtime error     | count taken modulo width | unchanged           |
| division                    | runtime error     | runtime error            | unchanged           |
| `new` count, size, failure  | runtime error     | runtime error            | unchanged           |
| `own` overwrite (D17.11)    | runtime error     | no check: old value leaks| unchanged           |
| `assert`, `panic`           | runtime error     | runtime error            | unchanged           |
| `noreturn` guard            | trap              | trap                     | unchanged           |

Notes:

- Index and slice checks compare unsigned: a signed index is sign-extended and a negative value
  becomes a huge unsigned number that fails the single comparison (D6.8). A constant index out of
  range for a fixed array is a compile error instead (D6.8). `p[lo..hi]` on a pointer is never
  checked (D6.9).
- Overflow checks cover signed and unsigned integers alike, so `len - 1` on an empty slice traps
  in checked mode (D11.1, D16). The wrapping operators exist so hashes and counters behave
  identically in both modes (D11.2). Programs must not rely on either overflow behavior (D11.1).
- `<<` discards bits shifted out without a check, so `1 << 31` on `i32` is `-2147483648` in
  both modes; only the count is checked (D6.2, D11.1).
- Division checks apply at every width and in both modes (D6.13, D11.3).
- `assert` is active in both modes; its message carries the source text of the argument (D12.2).
- The overwrite check guards every assignment to an lvalue of `own` reference type and nothing
  else: declarations, `move`, `del` and aggregate assignments are never checked (D17.11,
  section 2.5). A release build stores without looking, and the old allocation leaks.
- The `noreturn` guard is `call void @llvm.trap()` followed by `unreachable`, emitted after the
  body of a `noreturn` function and after every call to one (D8.5, D19.7). It is reachable only
  when an `extern` declared `noreturn` returns anyway; the process dies with SIGILL and nothing
  is flushed.
- `--no-bounds-check` exists for benchmarking and is unsafe (D10.6).

```fort
own mut i32[] s = new(i32[3]);
i32 a = s[3];                        // runtime error: index 3 out of range for length 3
i64 k = -1;
i32 b = s[k];                        // runtime error: index -1 out of range for length 3
mut u8 c = 255;
c++;                                 // checked: integer overflow; release: c == 0
u64 n = s.len - 4;                   // checked: integer overflow; release: wraps
mut u32 h = 0;
h = h *% 31 +% 7;                    // wraps in both modes
i32 sc = 32;
i32 d = a << sc;                     // checked: shift count 32 out of range for i32; release: a
i32 zero = 0;
i32 e = a / zero;                    // runtime error: division by zero
assert(s.len == 3);                  // passes
assert(s.len == 4);                  // main.ft:15:1: assertion failed: s.len == 4
own mut u8[] buf = new(u8[2]);
buf = new(u8[4]);                    // checked: overwriting owned value; release: the old buf leaks
```

## 7. The runtime-error contract

When a check fails, or `panic` or a failed `assert` executes, the runtime (D11.4):

1. flushes every buffered output stream (D11.5);
2. writes exactly one line to standard error;
3. calls `abort()`, so the process dies with SIGABRT, which a shell reports as status 134.

Deferred statements do not run (D7.8) and no destructor-like cleanup exists. The line has one of
three forms:

- `<file>:<line>:<col>: runtime error: <message>` for the checks of section 6;
- `<file>:<line>:<col>: panic: <message>` for `panic`;
- `<file>:<line>:<col>: assertion failed: <expression text>` for `assert`.

`<file>` is the path the compiler opened (the search root as given plus the relative module
path); `<line>:<col>` is the position of the check's operator token, which for the overwrite
check is the `=` of the assignment (D17.11), or of the builtin's name for `new`, `assert` and
`panic`; the `assert` text is the source text of the expression, verbatim (D11.4). Test files
match these lines with `//! stderr: <substring>` and `//! abort` (D14.5).

```sh
$ fort main.ft && ./a.out
main.ft:7:13: runtime error: index 5 out of range for length 3
$ echo $?
134
$ fort leaky.ft && ./a.out
leaky.ft:4:9: runtime error: overwriting owned value
```

Output buffering (D11.5): `print` and `println` write to a runtime buffer for standard output;
`eprint` and `eprintln` are unbuffered; `fprint` and `fprintln` use one buffer per descriptor,
and `fprint(1, ...)` shares the stdout buffer with `print`. An `extern` write to a descriptor
bypasses the buffers, so it can overtake buffered output unless the program flushes first.
Buffers flush when full, on `io.close` and `io.flush`, at exit, and before any runtime error, so
output printed before a failure is never lost. The runtime exports `fort_rt_flush(i32 fd)` and
`fort_rt_flush_all()`; `io.close` and `io.flush` call the former. Program start and exit are
owned by the C runtime, which builds `args`, calls the compiled `fort_entry`, flushes, and exits
with `main`'s result masked to eight bits (D11.6).

## 8. Undefined behavior

Undefined behavior in v1 is limited to the following list; everything else is either defined or
a diagnosed error (D10.7). None of these is detected.

| Undefined behavior                             | Example                                    |
|------------------------------------------------|--------------------------------------------|
| a view or stale copy used after the free       | `i32[] v = a; del(a); i32 x = v[0];`       |
| `del` of adopted memory not starting an allocation | `i32 x = 1; del(cast(&x, own i32*));`  |
| dereferencing `null`                           | `node* q = null; i32 v = q->value;`        |
| dereferencing a dangling pointer               | `fn i32* f() { i32 x = 1; return &x; }`    |
| writing read-only memory through a cast that added `mut` | `cast("abc", mut u8[])[0] = 'x';` |
| `p[lo..hi]` beyond the object                  | `i32 one = 0; i32[] s = (&one)[0..4];`     |
| calling a null function pointer                | `fn void() f = null; f();`                 |
| data races                                     | two threads from `extern` writing one `g`  |

Returning or storing a slice of a local array is the slice form of the dangling-pointer case
(section 4.3). Reading an object after its `del` through any alias, including a copy of the
`own` value made by lending before the `del` or `move`, is the first row (D10.7, D17.14). Use
after `del` or `move` through the emptied reference itself is not in the list: it dereferences
`null` or indexes a zero-length slice, a segfault or a bounds error (D17.9); double `del`
through one reference is a no-op (D17.9), and through two copies it is the first row. `del` of
a view, a sub-slice, a `.ptr`, a stack address or a literal is a compile error (D17.9), so only
the adoption `cast` can turn such a value into the second row (D17.3). Not undefined and not
detected: an `own` value that is never freed, and two `own` references to one allocation made
through `cast` (D17.14).

Explicitly not undefined: there is no strict-aliasing rule. Reading an object through a pointer
to another type of the same size is defined and yields the bit pattern (D10.7):

```fort
f64 x = 1.0;
u64 bits = *cast(&x, u64*);          // 0x3FF0000000000000
```

## 9. Idioms

### 9.1 Ownership transfer

The function that allocates returns the `own mut T*` or `own mut T[]`; the caller owns it and
frees it, and the type says so (D17.3, D17.5). A function that takes an allocation over
declares an `own` parameter, and its callers write `move` for lvalues (D17.5). Fields that own
are declared `own` too, so that a struct's `free` function can `del` them (D13.5).

```fort
struct node {
    i32 value;
    own mut node* next;
}

fn own mut node* make_node(i32 v) {
    own mut node* n = new(node);     // zeroed: next == null
    n->value = v;
    return n;                        // implicit move: responsibility passes to the caller
}

fn void free_chain(own mut node* head) {
    while (head != null) {
        own mut node* next = move(head->next);   // level 1 of head is mutable (D17.6)
        del(head);                   // head == null afterwards
        head = move(next);           // stores into an emptied binding: no trap (D17.11)
    }
}

fn void demo() {
    own mut node* a = make_node(1);
    a->next = make_node(2);          // an rvalue into a zeroed field
    node* second = a->next;          // lends
    free_chain(move(a));             // a == null afterwards; second dangles
    free_chain(a);                   // a null chain: nothing happens
    free_chain(second);              // error: passing a view to an own parameter (D17.3)
}
```

### 9.2 Out-parameters and error returns

Functions return `bool` or an error enum and deliver results through `mut T*` out-parameters;
`-1` and `null` sentinels are used where C convention expects them; `panic` is for programming
errors (D13.3). A slice result uses a pointer to an `own` slot, `mut u8[] own* out` (D3.6,
D13.5, D17.2): the callee moves the header through `*out`, and the caller, who initialized the
slot to `{}` so that the store passes the overwrite check (D17.11), owns the elements.

```fort
enum parse_error { none, empty, bad_digit, overflow }

fn parse_error parse_u32(string s, mut u32* out) {
    if (s.len == 0) { return parse_error.empty; }
    mut u32 acc = 0;
    for (char c : s) {
        if (c < '0' || c > '9') { return parse_error.bad_digit; }
        u32 d = cast(c, u32) - '0';
        if (acc > (4294967295 - d) / 10) { return parse_error.overflow; }
        acc = acc * 10 + d;
    }
    *out = acc;
    return parse_error.none;
}

fn bool find(i32[] xs, i32 key, mut u64* index) {
    for (mut u64 i = 0; i < xs.len; i++) {
        if (xs[i] == key) { *index = i; return true; }
    }
    return false;
}

fn bool read_all(i32 fd, mut u8[] own* out) {
    own mut u8[] buf = new(u8[4096]);
    defer del(buf);                  // frees buf on every failure path
    // ... fill buf, returning false on failure ...
    *out = move(buf);                // buf is {null, 0}: the deferred del is a no-op
    return true;
}

fn bool read_wrong(i32 fd, mut u8[] own* out) {
    own mut u8[] buf = new(u8[16]);
    *out = buf;                      // error: copying an own lvalue requires move(buf) (D17.5)
    return true;
}

fn void use_both() {
    mut u32 v = 0;
    switch (parse_u32("42", &v)) {
    case parse_error.none: println(v);
    case parse_error.empty, parse_error.bad_digit, parse_error.overflow: println("bad input");
    }
    own mut u8[] data = {};
    if (read_all(0, &data)) {        // &data is mut u8[] own*: a pointer to an own slot
        defer del(data);
        println(data.len);
    }
}
```

### 9.3 A growable buffer without generics

One struct per element type: an `own` backing slice plus a count of the elements in use
(D13.5). The backing slice is replaced by a larger one when full, and because `del` empties the
field first, the replacement passes the overwrite check (D17.11). The struct is an owning
aggregate and is passed by pointer (D17.7); `std::vec` provides `int_vec` and `ptr_vec` on this
pattern (D13.2).

```fort
struct int_buf {
    own mut i32[] items;             // backing storage; items.len is what is allocated
    u64 count;                       // elements in use
}

fn void push(mut int_buf* b, i32 x) {
    if (b->count == b->items.len) {
        own mut i32[] bigger = new(i32[b->items.len * 2 + 8]);
        for (mut u64 i = 0; i < b->count; i++) { bigger[i] = b->items[i]; }
        del(b->items);               // no-op the first time; leaves {null, 0}
        b->items = move(bigger);     // the store finds an empty field: no trap
    }
    b->items[b->count] = x;
    b->count++;
}

fn i32[] contents(int_buf* b) {
    return b->items[..b->count];     // a view; del of it does not compile (D17.9)
}

fn void free_buf(mut int_buf* b) {
    del(b->items);                   // b->items is {null, 0} afterwards
    b->count = 0;
}

fn void misuse(mut int_buf* b) {
    int_buf copy = *b;               // error: copying an owning value requires move (D17.7)
    del(*b);                         // error: del of an aggregate; del its fields instead (D17.7)
}
```

### 9.4 Cleanup with `defer`

Place a `defer` right after each acquisition; deferred statements run in reverse order, so
resources are released in the opposite order of acquisition.

```fort
import std::io;

fn bool copy_file(string src, string dst) {
    mut i32 in = io.open_read(src);
    if (in < 0) { return false; }
    defer io.close(in);
    mut i32 out = io.open_write(dst);
    if (out < 0) { return false; }   // closes in
    defer io.close(out);             // closes out, then in, at every later exit
    own mut u8[] buf = new(u8[65536]);
    defer del(buf);
    // ... copy loop ...
    return true;
}
```

### 9.5 Building a string in an `own mut u8[]`

Allocate the bytes, fill them, and `cast` the moved slice to `own string` (D3.14, D17.12). The
cast's result is `own` because its target says so, and an `own` lvalue cast to an `own` target
must be moved (D17.5), which empties the buffer so that the characters have exactly one owner
(D17.14); the caller frees the result with `del`. Allocate the exact length: a prefix such
as `buf[..n]` is a view, and a view becomes an `own string` only through the adoption `cast`,
which is meant for memory from C (D17.3).

```fort
fn own string repeat(char c, u64 n) {
    own mut u8[] buf = new(u8[n]);
    for (mut u64 i = 0; i < n; i++) { buf[i] = cast(c, u8); }
    return cast(move(buf), own string);   // the caller owns the characters
}

fn void demo() {
    own string s = repeat('-', 10);
    defer del(s);                    // del(own string) is legal (D17.12)
    println(s);
    string head = s[..5];            // a sub-string: a view
    del(head);                       // error: cannot del a string: not an own type (D17.12)
    del(cast(s, u8[]));              // error: not an own type; the cast lends a view (D17.12)
}
```

For strings whose length is not known in advance, `std::strbuf` grows an `own mut u8[]` with
the pattern of section 9.3, hands out `string` views of the bytes written so far, and returns
an `own string` from `take` (D13.2, D13.5).

## 10. Not in v1

Arena allocators as a language feature, leak detection, use-after-free detection, alignment and
packed attributes, and compile-time (linear) ownership tracking are outside v1; D15 lists the
deferred features with the idiom to use for each. `own` records who frees, and `move` and `del`
zero what they take (D17), so a leak stays a leak and a use after `move` or `del` is a `null`
dereference rather than a write into freed memory (D17.14). The `extern` boundary reaches every
C allocator and every C library in the meantime.
