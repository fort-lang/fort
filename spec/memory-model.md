# fort memory model

This document specifies where fort values live, how heap memory is obtained and released, what
pointers, spans and strings are at run time, which checks the compiled program performs, and
what is undefined. It implements the decisions in `decisions.md`, cited as `(Dn.m)`; where it
disagrees with `decisions.md` or `grammar.md`, they win and this document has a bug. Types and
mutability are specified in `type-system.md`; build modes and the runtime in
`toolchain.md`.

## 1. Memory regions

| Region         | Holds                                                    | Lifetime            |
|----------------|----------------------------------------------------------|---------------------|
| stack          | locals, parameters, local fixed arrays and struct values | the enclosing block |
| heap           | storage obtained from `new`, held by `own` references    | until `del`         |
| read-only data | string literals, module-level constants `Type NAME = e;` | the program         |
| writable data  | module-level globals `Type mut g = e;`                   | the program         |

Stack storage is released when the block that declares it exits (D10.1); temporaries live until
the end of the enclosing statement (D6.3). The heap is reached only through `new` and released
only through `del` (D10.1); `new` yields an `own` reference and `del` takes one, so the type of
a value says whether freeing it is its holder's job (D17.1, section 2.3). String literals and
module-level constants are placed in read-only memory and are addressable (D3.7, D7.10). Frames
larger than one page are probed on entry. Both targets use `"probe-stack"="inline-asm"`
(D10.8). A large local array with deep recursion
then faults on the guard page instead of skipping over it:

```fort
fn deep(i32 n) i32 {
    u8[65536] mut scratch = {};          // 64 KiB frame: probed page by page on entry
    if (n == 0) { return 0; }
    return deep(n - 1) + cast(scratch[0], i32);
}
```

A `string@ args` passed to `main` is built by the runtime from `argv`; each element is
NUL-terminated and lives for the whole program (D8.6, D11.6).

## 2. Allocation and deallocation

### 2.1 `new`

`new` is a keyword form that takes a type (D12.1). Every allocation is zero-initialized (D10.2),
so no value ever starts undefined, and every result is an owning reference: the `own` in its
type says that this value is the one responsible for freeing the allocation (D17.3; section
2.3).

| Form                | Result type          | Meaning                                      |
|---------------------|----------------------|----------------------------------------------|
| `new(T)`            | `T mut* own`         | one zeroed `T`, for every `T` (D10.2)        |
| `new(T[K])`         | `T[K] mut* own`      | one zeroed array object; `K` is a constant   |
| `new(T, n)`         | `T mut@ own`         | `n` zeroed elements; `n` is any integer type |
| `new(T[K], n)`      | `T[K] mut@ own`      | `n` zeroed rows of `T[K]`                    |
| `new(T*)`           | `T* mut* own`        | one zeroed pointer slot (D10.2)              |
| `new(T mut*)`       | `T mut* mut* own`    | one zeroed slot that reaches a writable `T`  |
| `new(T*, n)`        | `T* mut@ own`        | `n` zeroed slots, each a borrowed pointer    |
| `new(T mut*, n)`    | `T mut* mut@ own`    | `n` zeroed slots, each reaching a writable T |
| `new(T* own, n)`    | `T* own mut@ own`    | `n` owned slots, all `null` (D17.3)          |
| `new(void*, n)`     | `void* mut@ own`     | `n` zeroed `void*` slots                     |
| `new(void mut*, n)` | `void mut* mut@ own` | `n` slots that reach writable storage        |
| `new(T{...})`       | error                | allocate, then assign the fields             |
| `new(T@)`           | error                | a span header is not an object to allocate   |
| `new(void)`         | error                | `void` has no size                           |
| `new(T mut)`        | error                | the outermost position is `new`'s (D10.2)    |
| `new(T* mut)`       | error                | the outermost position is `new`'s (D10.2)    |
| `new(string own)`   | error                | `own` parses only after a `*` (D10.2, D17.3) |
| `new(own T)`        | error                | nothing precedes the base type (D5.3)        |

The element count is the second argument, never part of the type (D10.2): `new(T)` allocates one
`T` and `new(T, n)` allocates `n` of them as a span, so brackets inside `new(...)` are always
fixed-array dimensions of the element type (grammar section 6). `new(i32[4])` is therefore one
`i32[4] mut* own`, and `new(i32, 4)` a four-element `i32 mut@ own`. An untyped constant count may
take any integer type, and a negative constant count is a compile error (D4.1). A negative count
at run time, a total size that overflows, and allocation failure are runtime errors (section 6);
`n == 0` is allowed and yields a span of length 0 with a non-null `.ptr`, because the runtime
allocates at least one byte (D10.2). An `own` parses only after a `*` of the element type (grammar
section 6), and a `mut` parses in every position of the element type but the outermost one, which
`new` fills: `new` marks the storage it allocates, and that storage is that one position (D5.8,
D10.2). **So the element type of the result is the element type written.** `new(node*, n)` yields
a `node* mut@ own`, a span of assignable slots that borrow nodes it cannot write, and
`new(node mut*, n)` yields a `node mut* mut@ own`, the same span of slots reaching writable nodes;
`new(node* own, n)` yields a `node* own mut@ own`, a span of owned slots that are all `null`
(D17.3). `void*` is an element type like any other, since a pointer to `void` has a size
(D3.11): `new(void*)` is one slot and `new(void*, n)` is a span of them. `void` itself names no
storage, so `new(void)` stays the error above (D10.2, D17.3).

The result is an rvalue that must land in an `own` place (D17.8): the initializer of an `own`
declaration, an `own` parameter, an `own` field or element, or `del` itself. Binding it to a
plain `T mut*` would leave nothing able to free it and is a compile error.

```fort
point mut* own p = new(point);          // p->x == 0, p->y == 0
i32 mut@ own xs = new(i32, 8);          // eight zeroes
u8[16] mut* own row = new(u8[16]);      // one zeroed row of sixteen bytes
u8[16] mut@ own rows = new(u8[16], n);  // n rows of sixteen bytes
i32 mut@ own none = new(i32, 0);        // none.len == 0, none.ptr != null; del it like any other
i32@ own ro = new(i32, 4);              // owned, read-only through this binding (D5.4, D17.4)
node mut* own mut@ own kids = new(node mut* own, 4);  // four null owned slots (D17.3)
node* own mut@ own borrowed = new(node* own, 4);  // four null owned slots, read-only nodes
node mut* own mut@ own k2 = new(own node*, 4);    // error: nothing precedes the base type (D5.3)
node mut* mut@ own bad2 = new(node mut* mut, 4);  // error: the outermost position is new's
del(new(point));                        // allocated and freed in one statement
point mut* q = new(point);              // error: owning temporary would leak (D17.8)
point mut* own q2 = new(point{1, 2});   // error: new takes a type, not a literal
i32 mut@ own bad = new(i32@, 4);        // error: a span header is not an element type
void* own v = new(void);                // error: cannot allocate void
i32 mut@ own neg = new(i32, -1);        // error: negative constant count (D4.1)
i32 mut@ own neg2 = new(i32, k);        // runtime error when k == -1: negative allocation count -1
```

### 2.2 `del`

`del(x)` is a universe function that yields no value (D12.2). Its operand must have an `own`
type, of any mutability: an `own` pointer, a `void* own`, an `own` span or a `string own`
(D17.9). `del` frees the allocation the operand designates (D10.3) and, when the operand is an
lvalue, empties it: the pointer becomes `null`, the span or string `{null, 0}` (D17.9, D17.6).
Emptying is not an assignment, so the binding need not be `mut`; an operand reached through an
indirection (`*p`, `p->f`, `s[i]`) needs that level to be mutable, because the store is visible
to everyone else who holds the pointer or span (D17.6). A zero operand is a no-op, so
`del(buf); del(buf);` frees once (D17.9). Zero-value inspections remain legal.
The proof rejects a proved empty-owner dereference and invalid use of a released borrow (D17.14).
A zero-element allocation still requires release or transfer. Zero length does not mean empty.
On an rvalue operand `del` only frees.

| Operand                                            | Effect                                      |
|----------------------------------------------------|---------------------------------------------|
| `T* own` or `T mut* own` lvalue from `new(T)`      | frees the object; the operand is `null`     |
| `void* own` holding an allocation address          | frees it; the operand is `null`             |
| `T@ own` or `T mut@ own` from `new(T, n)`          | frees the elements; operand `{null, 0}`     |
| `T[K] mut@ own` from `new(T[K], n)`                | frees the rows; operand `{null, 0}`         |
| `string own` (D17.12)                              | frees the characters; operand `{null, 0}`   |
| an `own` rvalue: a call result, `move(x)`, a cast  | frees; there is nothing to empty            |
| `null` (the literal adopts `void* own`)            | no-op (D17.9)                               |
| a zero span or string                              | no-op                                       |
| `T*`, `T@`, `string`, `void*` without `own`        | error: a view, not an `own` type            |
| a sub-span `s[lo..hi]` or `s[..]`, a `.ptr`        | error: taking a span and `.ptr` yield views |
| `&local`, a literal                                | error: not an `own` type                    |
| a function pointer                                 | error: not an allocation                    |
| an integer, struct or fixed array                  | error: `del` is shallow (D17.7)             |

```fort
node mut* own n = new(node);
del(n);                              // n == null afterwards
del(n);                              // no-op
n->value = 1;                        // proof error: n is empty
i32 mut@ own xs = new(i32, 8);
i32@ view = xs;                      // lends: view designates the same elements (D17.4)
del(view);                           // error: cannot del 'view': not an own type (D17.9)
del(xs[2..4]);                       // error: cannot del a sub-span: it is a view (D17.9)
del(xs.ptr);                         // error: cannot del a .ptr: it is a view (D17.9)
del(null);                           // no-op
i32 mut@ own empty = {};
del(empty);                          // no-op
string s = "abc";
del(s);                              // error: cannot del a string: not an own type (D17.12)
string own d = str.dup(s);           // an owned copy (D13.5)
del(d);                              // frees the copy (D17.12)
i32 local = 1;
del(&local);                         // error: a stack address is not an own value (D17.9)
del(cast(s, u8@));                   // error: not an own type; the cast lends a view (D17.12)
del(new(node));                      // frees the temporary: an own rvalue
```

Through an indirection, `del` needs the level it empties to be mutable, exactly like `move`
(D17.6, D17.9):

```fort
fn drop(node* own@ view, node* own mut@ slots, node mut* n) void {
    del(view[0]);                    // error: element level of 'view' is immutable (D17.6)
    del(slots[0]);                   // fine: the slots are mutable; slots[0] == null afterwards
    del(n->next);                    // fine: level 1 of n is mutable; n->next == null afterwards
}
```

Allocations carry no header: `new` is `calloc` and `del` is `free`, so memory from C `malloc`
may be released with `del`, and memory from `new` may be released with `free` (D10.3). `own` in
an `extern` signature is erased and records the C side's convention (D17.13). A cast never adds
`own`, as it never adds `mut` (D3.14), so C memory has an owner only where its `extern` says
`own` (D17.3):

```fort
extern fn malloc(u64 n) void mut* own;   // storage of no type the caller may write (D17.13)
extern fn free(void* own p) void;

i32 mut* own p = cast(malloc(sizeof(i32)), i32 mut* own);   // not zeroed: C did not clear it
*p = 1;
del(p);                              // same allocator as free; p == null afterwards
node mut* own q = new(node);
free(cast(move(q), void* own));      // also fine: the move empties q
```

### 2.3 Ownership: `own`, `move` and lending

Whether a reference is responsible for its allocation is part of its type. `T* own`, `void* own`,
`T@ own` and `string own` designate the start of a live allocation from `new`, or from a C function
whose `extern` result says `own`, on which `del` is meaningful; `T*`, `T@`, `void*` and `string`
without `own` are views, which may be read and written through as their mutability allows but never
freed (D17.1). `own` is part of type identity and is erased at run time (section 2.5). It marks one
level only: an `own` follows the `*` or `@` whose reference it marks as owning, the outermost one
being the reference the binding holds, and it precedes the `mut` of the same position, so
`node mut* own p` reads "owned pointer to a writable node" (D17.2; `type-system.md` has the
placement rules and the identity rules). Two rules shape every idiom in this document:

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
  `node mut* own mut@ own` lends as `node* own@` or `node*@` but never as `node mut* mut@ own`,
  which would leave the nodes owned by nobody, nor as `node mut* mut@`, through which a borrowed
  pointer could be stored into a slot the source still owns (D17.4). Operands of `==`, `!=` and
  `?:` lend; `?:` yields `own` only when both operands are `own` rvalues or `null` (D6.2). An
  `own` rvalue may not be lent: converting it to a non-`own` type is the compile error "owning
  temporary would leak" (D17.8), because nothing could ever `del` it.

```fort
struct node {
    i32 value;
    node mut* own next;              // an owning field: node is an owning aggregate (D17.7)
}

fn value_of(node* n) i32 { return n->value; }   // borrows: any node* or node* own fits
fn consume(node mut* own n) void { del(n); }    // takes ownership: the caller must not del

node mut* own a = new(node);         // an own rvalue lands in an own place
node* v = a;                         // lends: v is a view of the same node (D17.4)
node mut* own b = a;                 // error: copying an own lvalue requires move(a) (D17.5)
node mut* own c = move(a);           // a == null afterwards; c owns the node
i32 x = value_of(c);                 // lends for the call
consume(c);                          // error: passing an own lvalue requires move(c) (D17.5)
consume(move(c));                    // c == null; consume is now responsible for the node
consume(new(node));                  // an rvalue passes as it is
node mut* leak = new(node);          // error: owning temporary would leak (D17.8)
value_of(new(node));                 // error: owning temporary would leak (D17.8)
node mut* own z = move(a);           // a was already null: z == null (D17.6)
```

Through an indirection the emptied level must be mutable, so nothing can be taken out of what
was merely lent (D17.6):

```fort
fn take(node* own@ view, node* own mut@ slots, node mut* own mut@ all) void {
    node* own p = move(view[0]);     // error: element level of 'view' is immutable (D17.6)
    node* own q = move(slots[0]);    // fine: the slot is mutable, the node is not
    node mut* own r = move(all[0]);  // fine: every level of 'all' is mutable
    del(q);
    del(r);
}
```

A range `for` lends its collection storage for the whole loop (D7.5, D17.10).
The loop reads a span or string header once at entry, including its pointer and length.
The checker refuses `del`, assignment, and `move` of the collection or containing storage inside
its body. This rule also applies through aliases. The checker permits a fort call or another
ownership operation only when it proves that the operation preserves the collection storage.
Element writes remain legal if they do not invalidate the collection storage.
A loop that changes its collection storage uses `while`.
The loan lasts for the whole loop body. Local last-use reasoning does not shorten it (D17.10).

Structs and fixed arrays that contain an `own` reference by value are owning aggregates: they
are moved like `own` references, returned from a local by an implicit move, and never `del`ed,
because `del` is shallow and a struct frees its own fields (D17.7). Functions therefore take
`list*` or `list mut*`, and a range `for` over owning elements lends its loop variable, which
is declared without `own` (D17.10):

```fort
struct list {
    node mut* own head;
    u64 size;
}

fn make() list { list l = {}; return l; }     // returning a local: implicit move (D17.7)
fn consume(list l) void { del(l.head); }      // by value: the caller writes move

list mut l = make();
list l2 = l;                         // error: copying an owning value requires move(l) (D17.7)
consume(move(l));                    // l is list{} afterwards
del(l);                              // error: del of an aggregate; del its fields instead (D17.7)
node mut* own mut@ own kids = new(node mut* own, 4);   // four null owned slots (D17.3)
for (node mut* k : kids) { }         // each k lends one element; kids is iterated in place (D17.10)
for (node mut* own k : kids) { }     // error: a loop variable cannot be own (D17.10)
node mut* own first = move(kids[0]); // taking an element out is explicit
del(first);
del(kids);                           // frees the slots; the remaining nodes must be freed first
```

`new(node mut*, 4)` yields `node mut* mut@ own`, slots that borrow; `new(node mut* own, 4)` yields
`node mut* own mut@ own`, slots that own what is later moved into them (D17.3). Each spelling says
what its slots reach, because `new` marks the storage it allocates and nothing below it (D10.2).
A cast never adds `own` (D3.14). Foreign memory gains ownership through an owning extern result
(section 4.5, D17.13). Transfer preserves the allocation identity independently of its owner place.
Heap views remain valid across transfer. Addresses of owner slots instead observe changed contents
(D17.5, D17.6).

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
fn checksum_ok(u8@ data) bool {
    u32 mut sum = 0;
    for (u8 b : data) { sum = sum +% cast(b, u32); }
    return sum % 7 == 0;
}

fn pattern(u64 n) u8 mut@ own {
    u8 mut@ own buf = new(u8, n);
    defer del(buf);                      // runs at every exit below
    for (u64 mut i = 0; i < buf.len; i++) { buf[i] = cast(i, u8); }
    if (buf.len < 16) { return new(u8, 0); }   // buf is freed after the empty result is made
    if (!checksum_ok(buf)) { return new(u8, 0); }   // and here, after the call completes
    return buf;                          // implicit move: buf is empty when the defer runs
}
```

The same shape delivers a result through an out-parameter: `*out = move(buf);` empties `buf`,
so the deferred `del` frees only on the failure paths (section 9.2).

**Transfer into a container.** A container holds its storage as `own` fields; pushing moves the
new element in and popping moves it out, and every field it overwrites was emptied first, so
the overwrite check of section 2.5 passes (D17.11):

```fort
fn push(list mut* l, node mut* own n) void {
    n->next = move(l->head);             // l->head is empty afterwards
    l->head = move(n);                   // stores into an emptied field
    l->size += 1;
}

fn pop(list mut* l) node mut* own {      // null when the list is empty
    node mut* own n = move(l->head);
    if (n != null) {
        l->head = move(n->next);
        l->size -= 1;
    }
    return n;
}

fn free_list(list mut* l) void {
    while (l->head != null) {
        del(pop(l));                     // an own rvalue: freed on the spot
    }
}

list mut l = {};
push(&l, new(node));                     // an rvalue moves in
node mut* own n = new(node);
push(&l, move(n));                       // an lvalue needs move
push(&l, n);                             // error: passing an own lvalue requires move(n) (D17.5)
free_list(&l);
```

**Freeing a tree.** A node whose children are `node mut* own mut@ own` frees each child, then the
slots, then itself; the element level is reached through a mutable path, so `move` out of the
span is allowed (D17.6):

```fort
struct tree {
    i32 value;
    tree mut* own mut@ own kids;         // owned slots holding owned subtrees (D17.2)
}

fn free_tree(tree mut* own t) void {
    if (t == null) { return; }
    for (u64 mut i = 0; i < t->kids.len; i++) {
        free_tree(move(t->kids[i]));     // the slot is null afterwards
    }
    del(t->kids);                        // the slots; t->kids is {null, 0} afterwards
    del(t);                              // the node; t is null afterwards
}
```

**An arena that hands out views.** One `own` block, many borrowed sub-spans: the callers can use the
memory but not free it, because taking a span yields a view (D17.3), and `del` of a view does not
compile (D17.9):

```fort
struct arena {
    u8 mut@ own block;
    u64 used;
}

fn arena_alloc(arena mut* a, u64 n) u8 mut@ {
    if (n > a->block.len - a->used) { panic("arena exhausted"); }
    u8 mut@ chunk = a->block[a->used .. a->used + n];   // a view into the block
    a->used += n;
    return chunk;
}

fn arena_free(arena mut* a) void {
    del(a->block);                       // frees every chunk at once; block is {null, 0}
    a->used = 0;
}

arena mut ar = {new(u8, 4096), 0};
u8 mut@ tmp = arena_alloc(&ar, 64);
del(tmp);                                // error: cannot del 'tmp': not an own type (D17.9)
arena_free(&ar);
```

`defer` captures nothing: the statement runs with the values its variables hold at exit (D7.8,
D16), so after `defer del(p);` a later `del(p); p = move(q);` makes the exit free `q`; had the
`del(p)` been left out, the assignment would trap on the overwrite check in a checked build
before any deferred code runs (section 2.5).
Prefer `defer` immediately after the `new` it undoes, before the variable is rebound.

### 2.5 Ownership at run time

`own` is erased: a `T* own` is the same eight bytes as a `T*`, a `T@ own` or `string own`
the same sixteen as a view, with the same alignment, layout and calling convention, and
`extern` signatures with `own` name the same C functions (D17.1, D17.13). The compiler emits
exactly three things for ownership:

- **`move(lv)`** loads the operand's value and stores its zero value into the operand: eight
  zero bytes for a pointer or `void*`, sixteen for a span or string, the whole value for an
  owning aggregate (D17.6).
- **`del(lv)`** passes the pointer (or the span's `ptr`) to the runtime's `free` and then
  stores the zero value into the operand; `del(rv)` only frees (D17.9; `toolchain.md` 6).
- **The overwrite check.** In a checked build (D11.1), every assignment to an lvalue of `own`
  reference type loads the current pointer word after the right-hand side has been evaluated,
  immediately before the store, and traps when it is non-null:
  `<file>:<line>:<col>: runtime error: overwriting owned value`, positioned at the assignment's
  `=` (D17.11, D11.4; section 7). The previous allocation would otherwise leak silently. `del`
  and `move` leave zero behind, so `del(v.data); v.data = new(...)`, `a = move(b)` after
  `move(a)`, and a store into a slot initialized from `{}` or `null` all pass. A release build
  stores without checking. Assignments of owning aggregates are not checked field by field. A
  declaration is a store like any other and is checked too: the slot of an owning local is zeroed
  once on entry, so the first execution passes and a second one -- a declaration in a loop whose
  body did not `del` -- traps rather than leaking one allocation per iteration (D17.11). Its check
  is reported at the declared name, a declaration having no operator token of its own.

```fort
u8 mut@ own mut buf = new(u8, 4);
buf = new(u8, 8);                    // proof error: the previous allocation would be lost
del(buf);
buf = new(u8, 8);                    // fine: buf was {null, 0}
u8 mut@ own other = move(buf);       // buf is {null, 0} again
buf = move(other);                   // fine
list mut l = {};
l.head = new(node);                  // fine: zero-initialized field
l = list{};                          // proof error: the aggregate still has an owned leaf
```

**Static proof (D17.14, D19.8).** The proof checks temporal storage and allocation ownership in
all build modes. It checks old owning leaves after right-side effects, before their replacement.
Runtime overwrite checks remain separate. Their removal supplies no proof of destination emptiness.

Record place contents separately from source validity. Record allocation identity separately from
the place that currently owns it. Release invalidates that allocation's borrows.
Refilling the owner place does not revive earlier borrows. Copying a borrow preserves its source.
Track retained sources through fields, elements, casts, globals, and available fort calls.
Mutable aliases remain legal. Substitute aliases before applying ordered ownership effects.
Taking an address or retaining a reference never removes these obligations.

Release or transfer residual owned leaves before normal storage end.
Keep `del` shallow. Release or transfer live owned descendants before their containing allocation.
Local storage ends at its FIR dead marker. Temporary storage ends at its source-defined boundary.
By-value parameter storage ends at normal return after deferred effects, even without a dead marker.
Check residual parameter leaves and escaping borrows at that boundary.
Symbolic caller storage remains separate from a borrowed parameter slot.
A whole aggregate move transfers all fields. It does not rebase addresses into inline storage.
Bind aggregate result storage to its actual caller destination, including argument and global
aliases.
Preserve the FIR order of result writes and deferred effects (`fir.md` 9.6 and 14).

Permit release after a borrow's last semantic use. An unused dangling local alone requires no error.
An escaping result or retained field preserves caller obligations beyond local last use.
Permit a store that replaces a dead borrow without reading its previous value.
Check each reaching path at the operation that needs proof. Different branch states alone are legal.
Loops retain zero-iteration paths and obligations across iterations and scope exits.
Normal return requires cleanup after deferred effects. Abort paths require no cleanup.
Unknown call outcomes preserve possible normal continuations and their cleanup obligations.

Executable owning globals must be empty at normal exit.
Libraries retain ownership between calls and provide explicit cleanup (D17.14, section 2.9).
These obligations cover memory. They add no resource type system for scalar handles.
Foreign effects remain outside proof (section 4.5). The proof does not establish total memory
safety.
Existing bounds, nullability, arithmetic, data-race, layout, and ABI rules still apply (D10.7).
The proof adds no runtime identity tracking, source exemptions, lifetime annotations, or unsafe
syntax.
`toolchain.md` 1 defines staged whole-feature selection and migration.

### 2.6 Place identity and aggregate storage

#### 2.6.1 Storage identity

The proof uses three separate identities (D17.15):

| Identity | Meaning |
|---|---|
| Slot | A storage object and a projected region that contains a value. |
| Source | The storage that a pointer, span, or string value designates. |
| Allocation | A heap object's identity and its release or transfer obligation. |

A local's storage identity includes its active scope instance.
A by-value parameter has its own slot, including a caller-made aggregate copy.
A borrowed parameter value instead designates separate symbolic caller storage.
A global designates its module storage.
An aggregate result designates the actual caller destination.
Two formal names establish no separation between their referenced storage (D17.15, D19.8).

Field projections select regions within the containing object.
Nested fields and fixed-array elements retain their containing storage identity.
Dereference and span indexing first resolve the current pointer or backing source.
The proof preserves possible overlap when it cannot resolve that source.
Different borrowed headers can designate the same backing storage.
Different slots can contain references to the same source.
The lifetime of a header or owner slot does not determine its referenced source's lifetime (D17.15).

#### 2.6.2 Dynamic elements and updates

Bind each index use to its current value, including a held index temporary (D17.15).
An assignment to the index local does not change the value of an earlier held index.
An unchanged index spelling does not establish equality across assignments.
Equality proves the same selected slot only when the backing storage and projection also agree.
Inequality proves separate slots only when their proved storage regions do not overlap.
It proves no separation between their pointed-to sources or allocations.

Use finite partitions for known index values, equality or inequality relations, and known ranges.
A partition records its membership condition and the possible contents of its element set.
Split or refine a partition only with proved facts.
An unproved relation keeps possible overlap.
A summary partition does not identify one concrete slot.
The finite-domain and convergence rules govern lost precision (`fir.md` 14; D19.8).

A strong update replaces one proved concrete destination region in each represented state (D17.15).
A source that can denote several concrete objects supplies no singleton proof.
Update all aliases of that region. Preserve disjoint regions.
A weak update keeps guarded alternatives for each possible destination.
Each alternative changes its selected region and preserves unselected regions.
Joining alternatives retains possible old contents, new contents, and invalidation.
It also retains live obligations.
A join must not discharge all candidate allocations when only one candidate is released.
It must not create multiple independent owners for one conditional transfer.

Check owning destination leaves on each feasible selected alternative after right-side effects.
Unknown selection can succeed when guarded facts prove all required obligations.
Reject an operation when any feasible alternative violates its obligation or cannot establish it.
Do not infer emptiness, release, or pointee separation from index inequality alone (D17.11, D17.15).

#### 2.6.3 Aggregate transfer and retained addresses

A copy of a reference value, alone or as a leaf of a whole aggregate copy, is a semantic use of
it, so copying a dangling reference is an invalid use (D17.14). A copy of a valid reference
preserves its source relations: a whole aggregate copy preserves each borrowed field's original
source (D17.15).
An owning aggregate copy into an owning place still requires move (D17.7).
A whole move transfers all owning leaves, borrowed fields, and scalar fields.
It empties the complete abstract source value, including its non-owning fields (D17.6).
A projected move transfers and empties only the selected subobject.
Sibling fields and elements keep their contents and obligations.
Track nested owning leaves rather than one obligation for the aggregate's type (D17.15).

A view into a transferred heap allocation remains a view into that same live allocation.
A view into inline source storage remains a view into that source storage.
Neither copy nor move changes the stored address to the corresponding destination field.
Reject its use or escape when that original storage ends.
Permit it when the original caller or local storage survives its required uses.
Keeping the destination aggregate alive does not extend the original source's lifetime (D17.15).
An output-parameter write also preserves the stored borrow's original source.
A caller destination cannot make a callee-local inline source survive return.
Transferring a heap owner through that output can preserve its live allocation view (D17.15).

An address of an owner slot refers to the slot, not its owned allocation.
Moving the slot value leaves the slot storage live and changes its contents to zero.
Reading that zero value is legal. Dereferencing it as an object is not (D17.9, D17.15).
An address into a span header likewise differs from a view into its backing allocation.

Generated code can omit clearing fixed temporary storage (`fir.md` 12.1).
The proof still transfers its ownership and empties its abstract owner state.
The remaining bytes create no additional ownership obligation.
Source-defined temporary boundaries still govern later uses.
This distinction changes no emitted instruction or ABI (D17.6, D17.15).

#### 2.6.4 Parameter and result boundaries

Each normal return ends scalar and aggregate by-value parameter slots after deferred effects.
No FIR dead marker is required for that boundary (D17.14, D17.15).
Check residual owning leaves, including unused scalar owners and nested aggregate owners.
A symbolic owning input admits a live obligation unless the returning path proves that input empty.
Current calls that pass only empty values do not remove that body obligation.
Returned and retained addresses into ending parameter storage fail at that boundary.
Returning an address into an inline parameter field also fails.
Returning a borrowed parameter value can succeed.
Its symbolic caller source remains separate.
The summary retains that source and its caller lifetime requirements (D17.15, D19.8).

Bind aggregate _0 to the caller destination before applying callee effects.
Include aliases with arguments, globals, and other referenced regions.
Scalar _0 stays private to the callee. Its stored return value carries its source relations.
An aggregate return does not end the caller destination's storage (D17.15; `fir.md` 5.3).

Check each destructive destination write against its previous overlapping owned leaves.
First apply operand reads, moves, and calls in emitted FIR order.
Then require each discarded obligation to be empty or already transferred (D17.11, D17.15).
A same-slot move can pass because the operand transfer empties that destination before the write.
A fresh return allocation does not prove that the previous caller destination is empty.
Whole-object zeroing also discards previous leaves and requires this proof.
Aggregate literals read all operands before their first destination store (`fir.md` 6).

When return lowering uses a holding temporary, transfer the return value there first.
Apply deferred effects and crossed storage boundaries. Then check the final write into actual _0.
An alias can refill the destination during defer, so recheck its current leaves at that final write.
An earlier completed result write remains visible to later deferred effects.
Do not reorder either effect to establish destination emptiness or source validity.
Check returned borrows after all deferred effects on their original sources (D17.15; `fir.md` 9.6).

#### 2.6.5 Finite case traces

These 17 traces state required proof verdicts (D17.15). They are not current compiler test results.
`A` and `B` name distinct live allocations in these input states.
The input supplies this fact. Slot separation alone does not establish it.
`zero` means an empty owner. `view(A)` designates allocation A. `slot(s)` designates slot s.
`owns(A)` includes its cleanup obligation. Each trace describes only its listed operation interval.
Accepted intervals preserve later cleanup and lifetime requirements.
Rejected intervals describe the required diagnostic reason, not a diagnostic identifier or format.

##### P01. Same index

- Input: `kids[k] = owns(A)`. Two index uses hold the same value and backing source.
- Operation: Release `kids[k]`; inspect the selected owner's zero value through the second use.
- Output: `A` is released. Both uses select the emptied slot. Other element contents stay unchanged.
- Verdict: Accept. Empty-owner inspection is legal. A later object dereference through it fails.

##### P02. Distinct indices with shared pointees

- Input: `refs[0] = view(A)`, `refs[1] = view(A)`, and `owner = owns(A)`.
- Operation: Release `owner`; dereference `refs[1]` after comparing the two indices as unequal.
- Output: The element slots stay distinct. Both stored views designate released A.
- Verdict: Reject the dereference. Diagnostic reason: the selected view refers to released storage.

##### P03. Distinct owned elements

- Input: `kids[0] = owns(A)`, `kids[1] = owns(B)`, and `v = view(B)`.
- Operation: Release `kids[0]`; use `v` while B remains live.
- Output: Element 0 is zero and A is released. Element 1 still owns B. The view still designates B.
- Verdict: Accept. Allocation separation comes from the input ownership facts, not index inequality.

##### P04. Unknown index

- Input: `kids[0] = owns(A)`, `kids[1] = owns(B)`, `v = view(A)`, and `k` is 0 or 1.
- Operation: Release `kids[k]`; dereference `v` without a condition that excludes `k == 0`.
- Output: If k is 0, A is released and B stays owned. If k is 1, B is released and A stays owned.
- Verdict: Reject the dereference. Diagnostic reason: a reaching selection releases its source.

##### P05. Nested fields

- Input: `s.left.data = owns(A)` and `s.right.data = owns(B)`; destination `taken` is zero.
- Operation: `taken = move(s.left.data)`; dereference `s.left.data`.
- Output: `taken` owns A. The selected field is zero. The sibling field still owns B.
- Verdict: Reject the dereference. Diagnostic reason: the selected owner is empty after transfer.

##### P06. Fixed arrays

- Input: `a[0] = owns(A)`, `a[1] = owns(B)`, `v = view(B)`, and destination array `b` is zero.
- Operation: Move the complete array a into b; use v while B remains live.
- Output: Both a elements are zero. The corresponding b elements own A and B. The view keeps B.
- Verdict: Accept. The move transfers every element. It does not move a's inline slot storage.

##### P07. Returned inline view

- Input: Local aggregate s has inline array `values`; `s.view` designates that inline array.
- Operation: Copy s into result storage; end the local storage; return the retained view.
- Output: The result's copied view still designates ended `s.values`, not the result's array.
- Verdict: Reject the escape. Diagnostic reason: the returned aggregate retains ended local storage.

##### P08. Returned heap view

- Input: Local owning aggregate s has `s.data = owns(A)` and `s.view = view(A)`.
- Operation: Move s into an empty result destination; end s's storage; return the result.
- Output: The result owns A and retains its live view. All source fields are zero. A remains live.
- Verdict: Accept. Caller uses and cleanup must preserve the returned allocation's obligations.

##### P09. Owner-slot address

- Input: `p = owns(A)`, `q` is zero, and `address = slot(p)`; p's storage remains live.
- Operation: `q = move(p)`; read the owner value through address.
- Output: q owns A. p is zero. address still designates the live p slot and reads zero.
- Verdict: Accept. Returning address from p's scope instead fails when that slot ends.

##### P10. Unused scalar owner parameter

- Input: By-value scalar parameter p owns A. No operation or defer consumes that obligation.
- Operation: Reach normal return without using p.
- Output: The parameter slot ends with its obligation still live. A remains allocated.
- Verdict: Reject the return.
  Diagnostic reason: ending parameter storage loses its owned allocation.

##### P11. Unused aggregate owner parameter

- Input: By-value aggregate parameter p has nested owning leaves for A and B.
- Operation: Reach normal return without transferring or releasing either leaf.
- Output: The parameter copy ends with two live obligations, even when FIR has no parameter dead.
- Verdict: Reject the return. Diagnostic reason: ending parameter storage loses its nested owners.

##### P12. Returned parameter-slot address

- Input: Borrowed pointer parameter p contains `view(A)` with a live symbolic caller source.
- Operation: Return `&p` after deferred effects.
- Output: A stays live. The returned address designates p's ending private slot, not A.
- Verdict: Reject the escape. Diagnostic reason: the result refers to ended parameter-slot storage.

##### P13. Returned inline parameter-field address

- Input: By-value parameter p contains inline array `values` in the parameter copy.
- Operation: Return `&p.values[0]` after deferred effects.
- Output: The caller's original aggregate remains separate. The result points into the ending copy.
- Verdict: Reject the escape.
  Diagnostic reason: the result refers to an ended inline parameter field.

##### P14. Returned view into caller heap storage

- Input: Borrowed parameter p contains `view(A)`; the symbolic caller owns live A.
- Operation: Return p without a deferred effect that invalidates A.
- Output: p's private slot ends. The returned value still designates caller allocation A.
- Verdict: Accept with the summary's caller lifetime requirement. Ending p does not release A.

##### P15. Same-slot aggregate result

- Input: Caller aggregate s owns A. The borrowed argument points to s. Aggregate _0 also binds to s.
- Operation: The callee returns `move(*argument)` without defer.
  Hold the value, empty s, then write _0.
- Output: The old destination is zero at the write. The result restores ownership of A to s.
- Verdict: Accept. The intermediate transfer satisfies the previous destination obligation.

##### P16. Global aggregate result

- Input: Global aggregate G owns A. Aggregate _0 binds to G. A fresh return aggregate owns B.
- Operation: Write the return aggregate into _0 without consuming G's previous owned leaf.
- Output: The proposed replacement discards A's obligation. B's freshness does not discharge A.
- Verdict: Reject the write. Diagnostic reason: replacing the actual result destination loses A.

##### P17. Result writes and deferred effects

- Input: Caller s owns A. A borrowed argument and aggregate _0 both refer to s.
- Operation: Hold `move(*argument)`; defer installs owned B in the emptied s; write the held result.
- Output: The holding temporary owns A. The final destination owns B before that result write.
- Verdict: Reject the final write. Diagnostic reason: the deferred effect creates a live overwritten
  obligation. Checking the earlier empty state would miss B.

### 2.7 Finite heap obligations and cleanup

This section defines the heap proof under D17.16. It does not change allocation or release code.
The analysis uses finite states. The program can allocate an unbounded number of objects over time.

**Concrete identities and obligations.** Each successful allocation creates one fresh allocation
identity and one obligation. Two live allocations from the same expression have different
identities.
Reusing a released address does not reuse its identity. A borrow of the released allocation stays
invalid when a later allocation uses that address or expression.

An owning reference designates one allocation, or it is empty. An owning span designates its backing
allocation, not one allocation per element. Its element slots can contain separate owned references.
A zero-element allocation still has one obligation (section 2.1).
An owned edge is an owning leaf in allocation storage. It holds the target allocation's obligation.
A borrowed edge holds a source relation. It adds no release obligation.
The graph records allocation storage, owned edges, borrowed edges, and owner places separately.
Each live allocation has one current owning reference or a transfer in progress.
The proof retains an obligation if its current owner becomes unreachable. Unreachability is not
cleanup.

**Finite representation.** A heap state uses singleton representatives and summary groups.
A singleton represents one selected allocation in each concrete state that the abstract state
permits. A summary group represents a set of allocations that can coexist in one concrete state.
Group keys can use allocation sites and bounded call context. A group key alone proves no identity
equality, disjointness, or cleanup result.
The implementation bounds representative and predicate counts. When it reaches a bound, it retains
conservative groups and loses precision. It cannot discharge an obligation to meet that bound.

Keep these facts for each group:

- Possible simultaneous cardinalities: zero, one, and many. Many means at least two allocations.
- Outstanding obligations and their owning paths or detached residual state.
- Possible borrowed sources, including subranges and released-source history.
- Proved structural relations and separation between represented allocations.

A cardinality set can contain several alternatives. For example, `{zero, one}` is not two live
allocations. Conversely, `{many}` cannot stand for one obligation merely because it has one group
key. The state need not retain an exact unbounded count. It must retain whether any obligation can
remain and the structural relation that permits complete cleanup.

A new allocation adds a fresh obligation. Moving it to an older group preserves that obligation.
If finite identity precision cannot separate it from an earlier borrow source, retain possible
overlap. Do not mark the earlier source live from the new allocation's validity.
Keep released sources separate from live-source facts. An ambiguous borrow use needs proof for
each possible source (D17.14).
Discard a released-source record only when no later use or retention can refer to it.

**Updates and conservation.** A move changes the owning path, not the allocation identity.
Moving an owned edge empties that edge and retains its target's obligation at the destination.
Before a nonempty `del`, prove valid ownership of the selected allocation and empty owned leaves
within its storage. Then discharge that allocation's obligation and invalidate its borrowed sources.
The release does not recursively discharge targets that its fields previously owned.
Empty `del` discharges nothing.

A release of one member cannot mark its entire summary group empty.
Releasing one member of `{many}` leaves `{one, many}` unless stronger relational facts apply.
A weak update retains possible residual members and possible sources. A join retains alternatives
from its reaching states. Widening retains possible residual obligations and possible overlap.
A complete-region cleanup summary can discharge a group only after the inductive proof below.
Its effect must retain obligations outside that region.

An owning destination must be empty after right-side effects and before replacement (section 2.5).
A transfer into one of the target's own descendants can create an owned cycle.
Type-correct moves alone do not prove that the resulting graph has an acyclic shape.
No exclusive borrowing rule applies. Substitute writable aliases before proving a destructive
update.
An unproved write through a possible alias destroys the affected shape fact. It does not destroy
the affected obligations.

**Inductive shapes.** A structural predicate describes a finite concrete region of any size.
Its recursive definition has a finite number of clauses and field or element roles.
The proof infers these predicates and their preconditions. Source code declares no heap predicate or
lifetime annotation.

`chain(root)` has two clauses:

1. An empty root owns no region.
2. A nonempty root owns one node, its proved payload regions, and a chain through its successor
   field. The node, payload regions, and successor region have disjoint allocation identities.

The second clause contains no path back to its node through an owned edge.
Each owned leaf belongs to the successor, a payload region, or a proved empty leaf.
The predicate cannot omit another owned field because the cleanup function does not read it.
Payload regions need their own complete cleanup proof. A pool block has one byte allocation as its
payload. A list node with only a successor has no payload region.

`tree(root)` uses the same empty clause. Its nonempty clause separates the node, payload regions,
and each child subtree. The child subtrees are pairwise disjoint in allocation identity.
An owned element collection also needs proof that its element partition covers its owned leaves.
An unproved index partition gives no complete child-cleanup fact.
Borrowed edges can cross these regions, point into them, or form cycles. They do not establish
ownership separation. Their semantic uses still need live sources.

Constructing a fresh zeroed node proves its owned leaves empty.
Attaching a transferred chain to its empty successor preserves `chain` when the node and chain are
disjoint. Attaching a transferred tree to an empty child preserves `tree` under the same separation
condition. A repeated allocation expression does not defeat freshness.
Conversely, different owner places or different indices alone do not prove different target
allocations. Summaries must preserve the construction conditions and actual argument aliases.
An arbitrary parameter is not a chain merely because its type has a successor field.
Instantiate the inferred shape precondition at each call. Reject a required fact that remains
unproved.

**Destructive cleanup.** Unfold a nonempty chain into one node and its separate successor region.
Transfer the successor into surviving owner storage before deleting the node.
Release or transfer its payloads. Prove its owned leaves empty. Delete only that node.
Continue with the saved successor, retaining its complete obligations and source relations.

```fort
struct heap_node {
    heap_node mut* own next;
    u8 mut@ own bytes;
}

fn drop_chain(heap_node mut* own mut current) void {
    while (current != null) {
        heap_node mut* own next = move(current->next);
        del(current->bytes);
        del(current);
        current = move(next);
    }
}
```

At each loop head, `current` owns the remaining chain. Earlier iterations leave no detached
obligation. The empty input takes the zero-iteration path and needs no release.
The nonempty iteration saves the separate tail, discharges the payload and node obligations, and
restores the invariant with the tail. The number of remaining nodes decreases by one.
This number is a mathematical proof measure. It adds no program counter or runtime check.
Finite acyclic input establishes induction for any length.
The false loop condition proves an empty remaining chain. It does not erase an unrelated or
detached obligation.

The list `pop` in section 2.4 transfers one node and restores the remaining list head.
It empties that node's successor. Thus `del(pop(l))` releases one empty node per iteration.
The pool sequence also releases `block->bytes` before `del(block)`.
Each iteration discharges two allocation obligations when the block has a nonempty payload owner.
The inference must preserve the per-block payload relation. Counting only block allocations gives
no proof of pool cleanup.

Tree cleanup uses induction on the disjoint child subtrees.
Move each child to its cleanup function. Prove that function's normal result discharges its entire
region. Release any owned child-slot allocation and payloads before deleting the node.
Recursive calls need the same shape and strict-subregion conditions. An unsupported recursive
effect or unproved child separation supplies no successful cleanup fact.

Check `break`, `continue`, return, and deferred effects in their FIR order (D17.14).
An early normal exit must release or transfer the remaining region and detached obligations.
Saving the next node only in a borrowed reference does not transfer its obligation.
Reading a successor after deleting its node is an invalid borrow use.
An abort path needs no cleanup. It supplies no successful normal-return cleanup result.

**Cycles, sharing, and precision limits.** An owned cycle does not satisfy `chain` or `tree`.
Do not fold a cyclic graph into an acyclic summary group.
A finite identity proof can cut a cycle by moving its owned edge into surviving storage.
It must prove the cut, valid target ownership, and empty owned leaves before subsequent release.
If it cannot establish these facts, report incomplete proof at the operation that needs them.
An uncut owned cycle retains its obligations, including when it has no external owning root.

Two owning edges to the same allocation do not constitute two independent obligations.
They violate the one-owner condition. A shape proof cannot discharge each edge as a separate child.
Unknown target overlap preserves this possibility until the proof establishes separation.
Multiple borrowed edges to one allocation remain legal.
Deleting their source invalidates them even when a containing object still lives.
An unused dangling borrowed field alone does not require an error. A later read or escape does.

Pool growth preserves earlier block allocations and their view sources.
Pool cleanup invalidates the node and payload sources that it actually releases.
It preserves sources outside its discharged region, including transferred descendants.
Refilling an empty pool creates fresh identities. It does not revive earlier pool views.
Unsupported shapes, alias effects, or source distinctions retain uncertainty and obligations.
Reject the affected operation as incomplete proof. Never infer successful cleanup from lost
precision.

**Soundness condition.** The abstract state includes each concrete state that reaches the operation.
Its transfer rules include each possible concrete successor state.
An accepted release discharges one valid obligation with empty owned leaves in each reaching state.
Inductive cleanup partitions the initial region into released nodes, released payloads, live
transferred regions, and the remaining region. These parts have disjoint allocation identities.
Transferred regions retain their allocation identities and obligations at their new owning paths.
Keep their source relations, including borrowed fields and views into transferred storage.
A transfer itself releases no allocation. A source outside the transferred region can still end
independently.
The empty normal exit proves that the remaining part has no allocation obligations.
An empty remainder can coexist with live transferred obligations. Check these obligations at their
new owning boundaries. Complete release additionally requires an empty transferred part.
Discharge only allocations that the proof shows released.
A summary group that contains transferred or other live members retains those members' obligations.
An omitted child, unaccounted detached allocation, or possible owned cycle breaks this proof.
Finite merging can enlarge the possible states. It cannot remove a violating state or declare its
obligation discharged. Incomplete proof therefore rejects the affected operation (D17.14).

**Finite traces.** These ten traces state the required analysis result.
They define proof cases, not results measured from the current compiler.
`A`, `B`, and `C` name distinct concrete allocations. `empty` names an empty owner.
`many` records concurrent obligations in a summary group. A live borrow adds no obligation.

| Trace | Case | Required result |
|---|---|---|
| H01 | Empty chain | Zero obligations before and after cleanup. |
| H02 | One node | Empty payload and successor; shallow release discharges one node. |
| H03 | Unbounded chain | Separate node and tail; exit has no remaining or detached obligation. |
| H04 | Branching tree | Discharge each disjoint subtree; release its empty parent last. |
| H05 | Owned cycle | Uncut cycle retains obligations; a proved cut can permit shallow release. |
| H06 | Borrowed cycle | Keep two owner obligations; reject later use of a released source. |
| H07 | Two live allocations at one site | Releasing A preserves B; fresh C does not revive A. |
| H08 | Transferred child | Releasing A preserves B at its new owner and B's live views. |
| H09 | Leaked child | Reject release of A while its owned B leaf remains live. |
| H10 | View after pool release | Growth preserves the view; cleanup invalidates its source. |

H01: `root = empty; drop_chain(move(root));` retains zero obligations.
Repeated empty cleanup retains zero obligations.

H02: `root -> A; A.next = empty; A.bytes = empty` has one obligation.
Move the empty successor, delete the empty payload, then delete A. The state has zero obligations.
With a nonempty payload P, delete P first. The two obligations then both discharge.
Alternatively, move P into a surviving output owner before deleting A.
The remainder becomes empty. P's obligation and source stay live at the output owner.

H03: `root -> A + chain(tail)` can represent any positive number of nodes.
After `next = move(A.next)`, A and `chain(next)` remain separate obligations.
After payload cleanup and `del(A)`, only `chain(next)` remains.
Advancing preserves the invariant. The empty exit has zero chain obligations.
A partial traversal retains its remaining chain. One node release cannot clear `many`.

H04: `root -> A; A.left -> tree(B); A.right -> tree(C)` separates three regions.
Moving and cleaning both child regions empties both fields. Delete A last.
If B and C may overlap, this trace supplies incomplete proof, not two successful child cleanups.

H05: `view -> A; A.next owns A` has one allocation obligation in an owned self-cycle.
`del(view->next)` cannot release A while A contains that live owned leaf.
`out = move(view->next)` cuts the proved singleton cycle and empties A.next.
Then `del(out)` can release A if its other owned leaves are empty. Later use of view is invalid.
An unproved cycle cut cannot discharge a summary group.

H06: `a owns A; b owns B; A.back borrows B; B.back borrows A` has two obligations.
Delete A with no owned descendants. B remains live, but B.back now has a released source.
Reading through B.back is invalid. Deleting B without reading that field discharges the second
obligation.

H07: Two executions of one expression create A and B. The group records `many`.
Delete the selected A. B's obligation remains; the group cannot become zero.
When separation is proved, a view of B remains live. A view of A remains invalid.
Allocate C at that expression. Its new obligation and live state do not repair A's view.

H08: `parent owns A; A.child owns B; view borrows B` has two obligations.
`out = move(A.child)` empties the child leaf and preserves B's identity at out.
Delete A. One obligation remains at out, and view still refers to live B.
The partition contains released A, transferred B, and an empty remainder.
If A and B share a summary group, releasing A cannot mark that group empty.
Delete out. The state has zero obligations, and later use of view is invalid.

H09: `parent owns A; A.child owns B` has two obligations.
`del(parent)` fails its empty-descendant requirement. It supplies no discharge of B.
Moving B out permits A's release but requires cleanup or transfer of B at its new boundary.

H10: A pool chain owns nodes A and B, with separate byte allocations P and Q.
The state has four obligations. A view of P survives the addition of B and Q.
Cleanup extracts the tail, deletes each byte allocation, then deletes each node.
The complete empty-chain result discharges four obligations and invalidates that view of P.
Deleting only A and B cannot prove cleanup of P and Q.

### 2.8 Raw storage and reference representations

#### 2.8.1 Sources, byte windows, and typed subobjects

A raw fort reference retains these facts when the proof knows them (D17.17):

| Fact | Meaning |
|---|---|
| Source | The original storage object, with its lifetime or allocation identity. |
| Offset | The reference's byte offset within that source. |
| Access window | The permitted byte interval inherited from the original object or view. |
| Extent | The known or symbolic size of that interval. |
| Alignment | The proved address alignment, including the effect of a byte offset. |
| Layout | Typed subobjects, reference representations, owning leaves, and padding. |

Record the storage containing a reference separately from its referenced source (D17.15, D17.17).
Copying an eight-byte pointer slot does not copy its pointed-to allocation.
An address of an owning slot designates that slot, not the allocation the slot owns.

`new(T)` supplies a logical extent of `sizeof(T)` bytes.
`new(T, n)` supplies a logical extent of `n * sizeof(T)` bytes on its successful path.
Capture the count value at allocation. A later assignment to its local does not change that extent.
The existing allocation checks govern negative counts, size overflow, and allocation failure
(D10.2). Their normal continuation can supply the corresponding size facts.
An extent need not be a source literal (D17.17).

An object address supplies that object's byte window, including its layout padding.
A field or element address supplies the selected subobject's window.
A span's `.ptr` retains its selected backing window and offset.
A span header has a separate storage window from its backing elements.
A pointer cast, including a cast through `void*`, does not widen any of these windows.
It retains known fort sources and their nested owning leaves.
Recovering a different pointee type cannot hide an overlapping owner from a write or release
(D17.17).

#### 2.8.2 Ranges, access, and alignment

For a raw fort range `p[lo..hi]`, prove a live source and `0 <= lo <= hi` (D6.9, D17.17).
Measure its element size from the result's type.
Prove that both resulting byte endpoints lie within the inherited access window.
For element size k, pointer offset o, and window `[b, e)`, prove
`b <= o + lo * k <= o + hi * k <= e`.
Prove that their offset and size calculations fit the existing target address representation.
Use mathematical size relations for that proof. Wrapped arithmetic supplies no extent evidence.
If a required bound or relation remains unproved, reject the range.
The operation adds no runtime range check.
Foreign storage instead uses D17.13 trust without a static extent (D17.17).
That extent trust does not override a known allocation's release or transfer state.

An ordinary array, span, or string slice keeps its existing runtime bounds check (D6.9, D10.6).
Its successful bounds path can establish a selected byte window in live backing storage.
An ordinary span index likewise keeps its existing runtime bounds check.
These checks do not revive a released backing source.
They do not prove that forged reference bytes designate valid storage (D17.17).
The proof does not make `--no-bounds-check` safe. That option keeps its D10.6 benchmarking contract.

For a fort dereference or other typed access, prove enough accessible bytes for the accessed type.
Prove the alignment that the existing type layout requires.
A byte access requires alignment 1. A cast to a wider type does not improve that alignment.
Apply offset facts to the original alignment; do not infer alignment from the new pointer type.
A cast can remain legal even when a later access fails its extent or alignment proof.
No cast adds a runtime check (D3.14, D17.17).

These rules add no strict-aliasing restriction.
Same-size reinterpretation still yields the object's bit pattern when the access obligations hold
(D10.7). Existing mutability and ownership conversion rules still apply (D3.14).
Foreign callers and implementations remain responsible for their alignment and storage validity
(D17.13).

#### 2.8.3 Byte reads, copies, and partial writes

A fort byte operation first requires live containing storage and sufficient access extent (D17.17).
Track the byte regions it reads and writes, with their actual order and possible overlap.
Track reference representation fragments by their original value and byte position.
Preserve only facts that those effects prove.
Scalar and padding bytes do not supply a new reference source or allocation obligation.
Padding also does not relocate an inline address (D17.15, D17.17).

A complete copy of a borrowed reference preserves its original source, offset, and access window.
A copied span or string must preserve a coherent pointer and length relation.
Copying bytes from different reference versions does not establish that relation by itself.
Infer a complete representation effect from fort operations or their inferred call effects.
A function name, a copy-like loop shape, or a matching final byte count supplies no special trust.
Apply actual caller aliases before establishing the effect (D17.15, D17.17).

A partial write to a borrowed reference can leave its representation unproved.
Reject a subsequent reference read, semantic use, or escape that requires the missing facts.
A complete typed replacement can restore a valid reference without reading the previous value.
A proved unchanged fragment preserves its existing representation facts.
If writes rebuild a complete representation, prove their byte correspondence and pointer-length
relations before a reference use. Imprecision does not establish a valid representation (D17.17).

A write overlapping an owning leaf must also preserve its allocation obligation (D17.11, D17.17).
Before a destructive write, prove the old overlapping owner empty or already transferred.
This applies to partial writes, whole-object fills, and writes through an erased pointer.
A later repair cannot recover an obligation that an earlier write discarded.
A proved unchanged write preserves the existing obligation.
A complete canonical zero representation can leave an already empty owning leaf empty.
Zeroing a live owner without a transfer instead loses its obligation and fails.

A raw byte copy never supplies a semantic ownership transfer (D17.17).
Copying a live owner's representation into another owning leaf fails, even when the destination
starts empty. An identical address does not supply two independent release rights.
Copying those bytes into a scalar byte buffer creates no owner in that buffer.
The original owner retains its obligation.
Reinterpreting that buffer as an owning reference requires a transferable obligation that the raw
copy did not supply. Reject the unproved owning interpretation.
Full borrowed interpretations can retain proved source relations without acquiring ownership.
Byte effects cannot add `mut` or bypass the existing ownership conversion rules (D3.14, D17.17).

A complete representation copy that implements `move` uses the semantic owning operand's transfer.
It empties the abstract source and gives the original obligation to the destination once.
Require the old destination leaves empty after operand effects (D17.11, D17.15, D17.17).
The generated byte copy creates no extra obligation.
Generated omission of fixed temporary clearing still follows section 2.6.3.
A byte copy followed by a manual clear is not an implicit move.

For overlapping regions, preserve the actual read and write sequence (D17.17).
Assume a complete source snapshot only when the operation establishes it.
A forward copy can overwrite bytes that a later iteration reads.
Do not interpret its result as the original reference without proof.
A proved same-region no-op preserves its values and obligations.
Unknown overlap retains the alternatives of section 2.6.2.
Check each feasible alternative. Unknown overlap does not discharge an owner.

#### 2.8.4 Integer reconstruction and foreign trust

Pointer-to-`u64` conversion exposes the address bits (D3.14).
Integer-to-pointer conversion remains a permitted cast, but supplies no proved storage source.
This includes an exact unmodified round trip through `u64` (D17.17).
Arithmetic, bitwise operations, narrowing, widening, and equal final bits do not establish a source.
An identity integer copy also does not establish a source.
Reject a fort memory operation that needs that reconstructed pointer's unproved source.
The cast alone need not fail. Integer calculations and pointer comparisons are not storage accesses.
A void-pointer round trip instead preserves the original reference facts (D17.17).
Data-storage provenance does not establish a callable target.
Existing function-pointer type and call-summary rules still apply (D3.10, D19.8).

Foreign reference results and foreign entry arguments use D17.13 trust.
Preserve that classification through complete reference copies, casts, fields, and fort wrappers.
A foreign integer result supplies no trusted reference classification.
Integer reconstruction cannot acquire foreign trust from its address bits (D17.17).
Known fort sources supplied to an extern call still require their existing source proof.
The call does not reclassify its arguments as trusted foreign sources for subsequent fort uses.
The declaration supplies no inferred relation between pointer arguments and scalar size arguments.
Hidden foreign byte writes, ownership duplication, and invalidation remain outside proof (D17.13).
The caller must satisfy the foreign operation's requirements.
An own foreign result still creates a tracked allocation obligation (D17.13).
Borrows with that known allocation relation become invalid when fort releases it.
Foreign extent trust does not erase this relation (D17.17).

A fort wrapper that calls an extern byte operation retains that operation's foreign proof limit.
For example, `std.mem.copy` calls `std.libc.memmove`.
Its name does not create an intrinsic fort representation-copy effect.
An LLVM byte copy that implements a typed fort operation instead keeps that fort operation's
ownership effect. It does not become a foreign source exemption (D17.17).
No compiler contract, unsafe construct, or lifetime annotation is required (D17.13).

#### 2.8.5 Empty and interior storage

An empty ordinary span can contain `{null, 0}` without a backing allocation (D17.17).
Its length and pointer remain inspectable. It permits no element access.
An ordinary empty slice keeps the existing bounds contract.
A raw range from an unproved pointer still needs its live-source proof, even when its bounds match.

A zero-length view into live storage retains its original source and boundary offset.
An end pointer has offset at the access window's end and zero remaining accessible extent.
A zero-length raw range at that boundary can pass while the source lives.
A dereference or positive-length raw range there fails.
Non-nullness alone supplies no accessible byte (D17.17).

A zero-element allocation differs from a zero owner (D17.9, D17.17).
`new(T, 0)` retains an allocation identity and a release or transfer obligation.
Its logical element extent is zero, although the runtime allocates at least one physical byte
(D10.2). That physical byte does not permit an element access.
Moving or deleting this allocation remains legal under the ordinary ownership rules.
Its zero length does not establish an empty destination or complete cleanup.

Interior views never acquire ownership. A raw slice and `.ptr` remain borrowed (D17.3).
An owning cast preserves only its original allocation ownership (D3.14, D17.17).
Before shallow `del`, release or transfer residual owned descendants.
Erasing their containing allocation's type does not remove that requirement (D17.7, D17.17).

#### 2.8.6 Finite case traces

These 12 traces state required proof verdicts (D17.17). They are not current compiler test results.
`A` names a live allocation. `owns(A)` includes its cleanup obligation.
`view(A, b, e)` designates bytes in the half-open interval `[b, e)`.
`zero` means a canonical empty owner. Slot storage remains separate from its referenced source.
Each trace covers its listed operation interval. Accepted intervals keep later cleanup obligations.
Rejected intervals specify a diagnostic reason, not a diagnostic identifier or output format.

##### R01. Complete borrow representation
- Input: Distinct live source and destination slots contain borrowed span headers. The source is
  `view(A, 0, 4)` with length 4. The proof knows both complete slot windows and layouts.
- Operation: Fort byte effects copy the complete source header without intervening source writes.
- Output: The destination retains A, its byte window, and the coherent length. No ownership moves.
- Verdict: Accept. Complete representation correspondence preserves the original borrow.

##### R02. Duplicate owner bytes
- Input: Source slot s owns A. Destination owning slot d is zero. Both slot windows are live.
- Operation: Fort byte effects copy s's complete owner representation into d without a semantic
  move.
- Output: No valid ownership transfer exists. Source s still holds A's obligation.
- Verdict: Reject. Diagnostic reason: byte copying cannot create a second owner of A.

##### R03. Moved owner representation
- Input: Source aggregate s contains an owner of A and a borrow into A. Destination d is zero.
- Operation: `d = move(s)` uses a complete representation copy after reading its operand.
- Output: Source s is zero. Destination d owns A once and retains the borrow into A.
- Verdict: Accept. The semantic move supplies the transfer. The byte copy creates no second owner.

##### R04. Partial owner write
- Input: An owning field holds A. An erased mutable byte view overlaps that field.
- Operation: A fort store replaces one field byte with an unproved value before any transfer.
- Output: The write cannot preserve the old obligation. A later typed repair does not discharge it.
- Verdict: Reject. Diagnostic reason: the overlapping write can discard live ownership of A.

##### R05. Overlapping reference copy
- Input: Live source and destination reference windows can overlap. The proof has no source
  snapshot.
- Operation: A forward fort byte loop copies between them. A later read interprets the destination
  as a reference. Earlier writes can change bytes that later iterations read.
- Output: The proof cannot establish complete correspondence to one original reference value.
- Verdict: Reject. Diagnostic reason: the copied reference's source relation remains unproved.

##### R06. Exact integer round trip
- Input: Local x is a live i32. Pointer p designates x with sufficient extent and alignment.
- Operation: `u64 bits = cast(p, u64); i32* q = cast(bits, i32*);` then read `*q`.
- Output: The address bits remain unchanged. Integer reconstruction supplies no source proof for q.
- Verdict: Reject. Diagnostic reason: the reconstructed pointer has no proved storage source.

##### R07. Changed integer bits
- Input: Pointer p designates live fort bytes. Its address converts to u64.
- Operation: Add one with `+%`, cast the result to `u8*`, then read through it.
- Output: The computed integer supplies no source, extent, or alignment proof.
- Verdict: Reject. Diagnostic reason: arithmetic on address bits does not establish live storage.

##### R08. Arbitrary integer bits
- Input: A u64 parameter contains arbitrary bits. No reference source accompanies them.
- Operation: Cast the bits to `u8*`, then dereference the result.
- Output: The cast creates neither a source relation nor foreign trust.
- Verdict: Reject. Diagnostic reason: the pointer's storage source remains unproved.

##### R09. Empty span
- Input: Owner bytes holds A with four u8 elements. Its backing window is `[0, 4)`.
- Operation: Take `bytes[4..4]`, inspect its zero length, then delete bytes after the last borrow
  use.
- Output: The empty view has offset 4 and no accessible element. Del discharges A's obligation.
- Verdict: Accept. The empty view preserves its live source. Its later unused dangling value is
  legal.

##### R10. End-pointer access
- Input: A remains live. Pointer tail comes from `bytes[4..4].ptr` at A's access-window end.
- Operation: Read one u8 through `*tail`.
- Output: Tail retains A, but its remaining access extent is zero.
- Verdict: Reject. Diagnostic reason: the access requires one byte beyond the permitted window.

##### R11. Misaligned access
- Input: A live i32[2] object has alignment 4. A byte view selects offsets `[1, 5)`.
- Operation: Cast that view's pointer to `u32*`, then read one u32.
- Output: Four bytes fit the window. Offset 1 does not preserve alignment 4.
- Verdict: Reject. Diagnostic reason: the typed access cannot establish the required alignment.

##### R12. Zero-element allocation
- Input: Owner z holds the non-null allocation from `new(u8, 0)`. Its logical extent is zero.
- Operation: A normal return leaves z unreleased because the program treats `z.len == 0` as empty.
- Output: The zero-element allocation still has a residual obligation.
- Verdict: Reject. Diagnostic reason: the normal storage boundary loses an owned allocation.

### 2.9 Global ownership and entry boundaries

A global has persistent storage. Its declared initializer supplies its first state (D7.10, D17.19).
An ordinary function return does not end that storage.
Track its contents, owning leaves, and retained borrow sources through calls.
A local-to-global move transfers an allocation obligation. It does not lose that obligation.
Moving from a global empties its selected place and transfers the obligation to the destination.
Moving an aggregate does not rebase addresses into its inline storage (D17.7, D17.14).

Check global replacement after right-side effects, before the store (D17.11).
An address, field projection, element projection, or fort call does not exempt the destination.
An old owning leaf must be empty or have a proved transfer or release.
New contents do not revive borrows from an earlier released allocation.
Track global aggregate leaves separately. Keep del shallow (D17.9).
A live allocation with zero elements still has an obligation.
A retained global borrow alone adds no ownership. Its later use still needs a valid source.
Storing a local address in a global does not extend local storage life.
Clear or replace that retained relation before its source ends, or reject the escape (D17.14).

**Executable boundary.** Normal executable exit requires empty owning globals in the complete
checked import closure (D17.19).
Include user, library, and runtime globals. Include live owned descendants of their allocations.
Release or transfer descendants before their containing allocation, as the shallow del rule
requires.
A global is empty when its owning leaves hold zero values and retain no allocation obligations.
Borrowed fields and scalar resources need no release under this memory rule.

The final boundary occurs after applicable fort and runtime cleanup.
It precedes the generated C return or the known foreign normal-exit operation.
Check all reaching normal-termination paths at that boundary.
Apply deferred effects before a source function returns normally (D7.8).
A source main return alone is not the boundary. Generated shutdown still follows it.
An ordinary call to a function named main has ordinary function-return semantics.

Known std.sys.exit and std.rt.exit do not unwind caller scopes.
Their summaries include runtime cleanup before the final boundary.
Establish those effects from the analyzed fort bodies and generated sequence.
A runtime function name alone proves no release.
Reject or retain requirements when cleanup is absent.
Do not require an empty runtime argument owner at the exit call's entry.
Require its emptiness after its cleanup effect.
The runtime does not automatically delete user globals or suspended caller owners.
Those owners must already have a proved release or transfer before final termination.
A pending caller defer supplies no cleanup effect on an exit path that does not run it.
Compose these requirements through fort wrappers and indirect fort targets in effect order.

**Runtime argument owner.** Generated startup builds the argument span before source main runs
(D11.6).
The runtime args_store owns the allocation of string headers.
Positive argc creates that allocation. Nonpositive argc leaves args_store empty.
Final cleanup must also permit that empty state (D17.9).
Each string borrows bytes from C argv. Deleting the header allocation does not delete those bytes.
Keep argument views valid through source execution and applicable source defers.
Flush runtime buffers before final argument-storage release.
Release that allocation and empty args_store before normal executable termination.
This requirement also applies when source main has no argument parameter.
Account for the generated startup and shutdown sequence outside source-function FIR.
Checking only source-function returns does not prove this sequence.
The rule fixes no new runtime ABI helper or runtime ownership representation.
The proof must not replace an absent implementation effect with the intended runtime behavior.

**Libraries.** A library can retain owners between ordinary calls (D17.19).
Infer global preconditions, ordered effects, and postconditions from all available fort bodies.
Analyze uncalled bodies too. Library checking does not hide local leaks or invalid source uses.
Symbolic global state differs from the initializer state of the first host call.
Do not assume globals are empty at each later entry.
Do not require all library globals to become empty at each normal call return.

A library provides explicit cleanup through ordinary callable fort functions.
Cleanup releases or transfers allocations and empties its owning global leaves.
Library checking defines its global set through the complete checked library closure.
The host calls cleanup after final library use and before unload or normal host termination.
That shutdown boundary requires empty owning globals in the library closure, including its runtime.
Foreign host storage outside the closure uses D17.13 trust.
An executable caller still includes imported library globals at its final boundary.
Infer requirements for cleanup order when one library retains another library's borrowed storage.
Preserve that source relation until the retaining relation ends.
No automatic destructor, entry annotation, prescribed cleanup name, or contract syntax applies.

A library check uses symbolic caller and global states.
It does not invent an external call sequence or a generated executable entry.
The same proof applies in check and build modes under the same boundary assumptions.
Build analysis instantiates library summaries with executable startup and shutdown states.
The host's external call order and cleanup invocation remain outside standalone library proof.
The check proves no external shutdown sequence.
This ownership boundary adds no separate-compilation or shared-library output mode (D15).

**Foreign entries.** Trust valid symbolic foreign inputs under D17.13.
An own input carries a symbolic allocation obligation. An ordinary return must release or transfer
it.
A move into an owning global can satisfy that transfer while preserving the library's global state.
A borrowed input does not become owned. Its retained source relation remains a caller requirement.
Callback-local storage still ends after applicable defers at normal callback return.
Reject retained or returned borrows of that storage when they escape its boundary.
Foreign callback timing, argument validity, hidden retention, and hidden writes remain outside
proof.
An executable's known fort calls still preserve the callback's analyzed effects and requirements.

**Termination classes.** Function return, normal process termination, abort, and unresolved foreign
termination are distinct (D17.19).
The recognized foreign leaves are the selected std.libc declarations:

```fort
extern fn exit(i32 code) noreturn;
extern fn abort() noreturn;
```

Exit supplies normal process termination. Abort supplies abort termination.
A compatible redeclaration of the same linked C symbol shares its class (D9.8).
Compatibility includes return type, fixed parameter types, ownership qualifiers, and variable-tail
mark.
The existing extern-identity rule rejects incompatible redeclarations.
Match the C symbol, not a fort identifier that resembles exit or abort.
An ordinary fort function with either name requires body analysis.
These two leaves add no programmer contract or extensible contract database.
Trust their standard termination conventions under symbol interposition (D17.13).
A foreign replacement that violates that convention remains outside the proof.

Resolve function-value copies and aliases before combining possible target summaries.
Compose available fort body effects to classify wrappers.
Keep mixed outcomes separate under their reaching conditions.
A wrapper that can abort or exit normally requires cleanup only on its normal-termination paths.
An unresolved foreign target retains the unresolved foreign class.
Panic and runtime failures reach abort. Compiler traps also require no cleanup.
Abort paths carry no final global or suspended-caller cleanup requirement.
Checks on earlier operations still apply before either kind of termination.

Noreturn proves that control does not return to the caller under the declared promise (D8.5).
It proves neither abort nor normal process termination.
The defensive trap after a call catches a violated promise. It does not describe the callee's exit.
An unresolved foreign termination, including a low-level _exit call, uses D17.13 trust.
Its hidden process effects remain outside proof. Missing foreign termination metadata causes no
error.
Accepting it does not prove normal-exit cleanup or classify it as abort.
Unknown fort effects receive no foreign exemption and retain possible normal continuations.

Foreign exit handlers, signal timing, process replacement, and non-local foreign control flow remain
outside proof under D17.13.
The compiler does not prove that such handlers preserve an argument view after runtime cleanup.
A later header-element read through released argument-header storage fails its source obligation.
A copied string header still borrows argv bytes.
Header-allocation cleanup does not release those bytes.
An infinite execution has no final normal-exit boundary until it reaches known normal termination.
It still checks operations and completed local storage boundaries during that execution.

#### Six global examples

These verdicts follow D17.19. They describe the ownership proof, not current compiler coverage.
Each executable example assumes the required final runtime argument cleanup.
Diagnostic descriptions identify obligations. They add no fixed diagnostic message text.

**G1. Local-to-global transfer.** Accept the transfer and later cleanup.
The source owner becomes empty. The global retains the allocation until del.

```fort
u8 mut@ own mut pool = {};

fn fill() void {
    u8 mut@ own local = new(u8, 4);
    pool = move(local);
}

fn main() i32 {
    fill();
    del(pool);
    return 0;
}
```

**G2. Global overwrite.** Reject the second store. Its previous owning leaf remains live.
Deleting or transferring pool before the replacement makes that store legal.

```fort
u8 mut@ own mut pool = {};

fn replace() void {
    pool = new(u8, 4);
    pool = new(u8, 8);
}
```

**G3. Normal main return.** Reject the executable path at its final normal-exit boundary.
The diagnostic identifies pool and its undischarged allocation. A store into pool is not a local
leak.

```fort
u8 mut@ own mut pool = {};

fn main() i32 {
    pool = new(u8, 4);
    return 0;
}
```

**G4. Abort.** Accept this ownership path. Panic aborts; it has no cleanup requirement.

```fort
u8 mut@ own mut pool = {};

fn main() i32 {
    pool = new(u8, 4);
    panic("stop");
}
```

**G5. Library return and cleanup.** Accept put with an empty pool precondition.
Its normal return retains global ownership. Accept clear; its result state has an empty pool.
The host calls clear before unload or normal host termination.
It must also discharge any other owners in the checked library closure.

```fort
u8 mut@ own mut pool = {};

fn put(u8 mut@ own value) void {
    pool = move(value);
}

fn clear() void {
    del(pool);
}
```

**G6. Foreign callback.** Accept the owner transfer with an empty saved precondition.
Trust the incoming allocation's validity and uniqueness. Keep the global obligation after return.
Reject bad_callback: retained points into callback-local storage after that storage ends.

```fort
u8 mut* own mut saved = null;
u8 mut@ mut retained = {};

fn callback(u8 mut* own value) void {
    saved = move(value);
}

fn bad_callback() void {
    u8[4] mut local = {};
    retained = local[..];
}
```

#### Seven exit traces

These traces distinguish ordered effects from the final obligation boundary (D17.19).
An allocation label identifies storage, not an owner slot.
An empty global has no allocation obligation. Runtime cleanup is an effect, not an exemption.

**X1. Runtime cleanup.** Source state: args_store owns header allocation A; user globals are empty.
Ordered effects: source main returns after defer; runtime flushes; runtime releases A; C main
returns.
Class: known normal process termination.
Required cleanup: empty args_store and all other owning globals before the final return.
Verdict: accept after those effects. Flushing alone leaves A live and fails the final obligation.

**X2. sys.exit with a pending defer.** Source state: pool owns A; args_store owns B.
The caller has defer del(pool), then calls sys.exit(7).
Ordered effects: runtime flushes; runtime releases B; foreign exit terminates.
The caller defer does not run.
Class: known normal process termination.
Required cleanup: discharge A before the final boundary. Do not require B empty before runtime
cleanup.
Verdict: reject for pool. A pending defer cannot satisfy that obligation.

**X3. Fort exit wrapper.** Source state: pool owns A; args_store owns B; caller owners are empty.
Ordered effects: a fort wrapper deletes pool and calls sys.exit.
Runtime flushes and releases B before foreign exit terminates.
Class: known normal process termination through an inferred wrapper summary.
Required cleanup: discharge A and B before the final boundary.
Verdict: accept. A live caller owner would instead fail unless cleanup reaches it before
termination.

**X4. Abort.** Source state: pool owns A; args_store owns B; a caller local owns C.
Ordered effects: panic reports its message and aborts. Caller defers do not run.
Class: abort.
Required cleanup: none on that path. Earlier invalid operations still require diagnostics.
Verdict: accept this termination path. Do not require releases of A, B, or C.

**X5. Unresolved foreign termination.** Source state: pool owns A; args_store owns B.
Ordered effects: an ordinary extern fn finish() noreturn call uses its declared no-return promise.
Class: unresolved foreign termination. Its body could abort, exit, or run without termination.
Required cleanup: the compiler proves no foreign final boundary; the foreign convention remains
trusted.
Verdict: accept under D17.13. Record the proof limit; infer neither cleanup nor an abort exemption.

**X6. Generated startup.** Source state: runtime and user owners start empty; argc is positive.
Ordered effects: args_init allocates A; args lends it; source main runs and returns after defer.
Runtime flush and argument release follow; the final generated return ends the process.
Class: known normal process termination.
Required cleanup: discharge A and source-created owners at their applicable boundaries.
Verdict: accept only with the complete generated sequence. A source-function-only count is
insufficient.
The same sequence applies when source main takes no arguments.

**X7. Library cleanup.** Source state: a foreign host enters with empty library pool.
Ordered effects: callback transfers owner A into pool; callback returns; later clear deletes A; host
unloads.
Class: two ordinary function returns, followed by a host-controlled library boundary.
Required cleanup: callback-local owners discharge at return; pool may remain live until clear.
Verdict: accept the bodies and inferred states. Host invocation and unload order remain external
requirements.

## 3. Pointers

A pointer `T*` holds the address of one `T` or is `null`. There is no pointer arithmetic, so
the sources of a pointer are exactly the following (D10.4, D10.5), and each fixes whether the
result owns what it points to (D17.3):

| Source          | Type                                 | Owning?                              |
|-----------------|--------------------------------------|--------------------------------------|
| `null`          | whatever the context expects (D10.5) | usable in `own` and borrowed places  |
| `&e`            | `T*`, level 1 per D5.8               | no: a view of `e`                    |
| `new(T)`        | `T mut* own`                         | yes                                  |
| `s.ptr`         | `T*` with `s`'s element mutability   | no: a view of `s`                    |
| `cast(e, T*)`   | as written                           | as written; a cast never adds `own`  |
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
| `p[lo..hi]` | unchecked span of `hi - lo` elements   | a `T*` or `T mut*`                       |

```fort
i32 mut x = 10;
i32 mut* p = &x;
*p = 11;                             // x == 11
point mut pt = {1, 2};
point mut* pp = &pt;
pp->x = 5;
i32 y = pp.x;                        // error: use -> on a pointer
i32 z = p[0];                        // error: pointers cannot be indexed
i32 mut* q = p + 1;                  // error: no pointer arithmetic
p++;                                 // error: no pointer arithmetic
node* n = null;
bool none = n == null;
i32@ s = {};
bool empty = s == null;              // error: compare s.len or s.ptr
bool nn = null == null;              // error: null has no type here
node mut* own o = new(node);
node* w = o;                         // lends: the same operations, no responsibility
i32 val = o->value;                  // as on w
bool same = o == w;                  // operands of == lend (D17.4)
del(w);                              // error: cannot del 'w': not an own type (D17.9)
del(o);                              // o == null; w now dangles
```

`null` is the zero pointer and function-pointer value (D10.5).
The proof rejects invalid or unproved fort temporal storage access (D10.7, D17.14).
Null dereference and foreign dangling-pointer access remain undefined behavior (D10.7, D17.13).

### 3.1 From a raw pointer to a span

There is no pointer arithmetic (D10.4). The only way to view memory behind a raw pointer as
elements is `p[lo..hi]`, which yields a `T@` (or `T mut@` from a `T mut*`) with `ptr` advanced
by `lo` elements and `len == hi - lo`, without a runtime range check (D6.9).
The result is a view whether or not `p` is `own` (D17.3).
For fort storage, prove a live source and sufficient extent for the range (D17.14).
Foreign storage uses D17.13 trust. Foreign ranges beyond the object remain undefined behavior.
The operation supplies no proof exemption. Only the
two-bound form exists for pointers, because a pointer has no length; `p[lo..]`, `p[..hi]` and
`p[..]` are errors, as is any span expression on a `void*` (D6.9).

```fort
extern fn strlen(char* s) u64;
extern fn getenv(char* name) char*;

fn env_value(string name) string {   // name must be NUL-terminated, for example a literal
    char* p = getenv(name.ptr);
    if (p == null) { return ""; }
    return cast(p[0..strlen(p)], string);
}
char@ tail = p[3..];                 // error: a pointer has no length
void* vp = cast(p, void*);
u8@ bytes = vp[0..4];                // error: cannot take a span of a void*
u8 mut* own raw = new(u8);
u8 mut@ one = raw[0..1];             // a view of the allocation, not a second owner
del(one);                            // error: cannot del 'one': not an own type (D17.9)
del(raw);                            // frees the byte; one now dangles
```

### 3.2 `void*`

`void*` is an address with no pointee type: it cannot be dereferenced, cannot reach fields,
cannot be indexed and has no span expression; every conversion to and from it is a `cast` (D3.11).
It exists for `extern` signatures and for storing an address whose type is recovered later with
`cast`.
`void* own` is its owning form (D17.1): what `free` takes (D17.13), a legal operand of `del`, and
the type a container such as `ptr_vec` would use if it owned what it stores. A `cast` between
`void* own` and a typed `own` pointer yields `own` because its target says so, and an `own` lvalue
operand must therefore be moved (D3.14, D17.5). `malloc` answers `void mut* own`, because the
storage it gives back is storage the caller may write, and that mark drops to `void* own` at the
call to `free` (D3.11, D5.4).

```fort
extern fn malloc(u64 n) void mut* own;
void mut* own blob = malloc(16);
point mut* own pt = cast(move(blob), point mut* own);   // blob == null afterwards
point mut* own pt2 = cast(blob, point mut* own);        // error: copying own lvalue needs move
del(pt);
del(blob);                           // no-op: blob is null
```

## 4. Spans and strings in memory

### 4.1 Layout

A span `T@` is sixteen bytes: a pointer to the first element followed by a `u64` element count
(D3.5). A `string` has the same layout with `char` elements (D3.7). Both have alignment 8.
Whether the header owns its elements is part of the type, never of the bits: an `own` span or
`string own` is the same two words as a view (D17.1). A view's elements live on the stack (a span
of a local array), on the heap (`new`, seen through a lent `own` value or a span of one), in
read-only data (a literal, a span of a module constant) or in foreign memory (`p[lo..hi]`); an
`own` span's elements always start a heap allocation (D17.1).

| Offset | Word  | Type  | Holds                                                    |
|--------|-------|-------|----------------------------------------------------------|
| 0      | `ptr` | `T*`  | the address of `s[0]`, or `null` in the zero value       |
| 8      | `len` | `u64` | the element count; `s[len - 1]` is the last element      |

`.ptr` and `.len` read the two words; neither is an lvalue (D6.7). The zero value `{null, 0}` is
what `= {}` produces and what a zeroed struct field holds. There is no expression form for a
zero span outside a declaration initializer: to reset a borrowed span-typed field, declare a
variable with `= {}` and assign it. An `own` span field is reset by `del` or `move`, which
leave `{null, 0}` behind (D17.6, D17.9).

### 4.2 Span expressions

`e[lo..hi]`, `e[lo..]`, `e[..hi]` and `e[..]` on a fixed array lvalue, a span or a string produce
a span (or string) of the same elements with `ptr` advanced by `lo` elements and `len` set to
`hi - lo`; omitted bounds are `0` and `len` (D6.9). The check `0 <= lo <= hi <= len` is against
the operand's own `len`, not the original allocation, so a span can only shrink. The result's
element mutability is that of the operand's elements (D6.9). The result is always a view, even
`a[..]` of an `own` span or `string own` (D17.3): it cannot be `del`ed (D17.9), and it never
enters an `own` place, because a cast never adds `own` (D3.14).

```fort
i32 mut@ own a = new(i32, 6);        // {p, 6}
i32 mut@ b = a[2..5];                // {p + 2 * 4 bytes, 3}
i32@ c = b[1..];                     // {p + 3 * 4 bytes, 2}
i32@ d = a[..];                      // same header as a, elements immutable
i32@ e = b[0..4];                    // runtime error: span bounds 0..4 out of range for length 3
i32@ f = a[4..2];                    // runtime error: span bounds 4..2 out of range for length 6
i32[4] mut arr = {1, 2, 3, 4};
i32 mut@ g = arr[1..3];              // points into arr's stack storage
i32@ h = make_array()[..];           // error: cannot take a span of a fixed array rvalue
i32 mut@ own i = a[..];              // error: a view cannot initialize an own place (D17.3)
del(b);                              // error: cannot del a sub-span: it is a view (D17.9)
```

### 4.3 Aliasing and lifetime

A span and its source designate the same elements: writes through one are visible through the
other, and both are invalidated together when the storage goes away.

```fort
i32 mut@ own a = new(i32, 4);
i32 mut@ b = a[1..3];
b[0] = 9;                            // a[1] == 9
del(a);                              // b now dangles; the proof rejects later semantic use
```

A `move` changes which reference is responsible, not the allocation: views made before it stay
valid until the allocation is freed. A copy made by lending (`i32@ w = a;`) is a view like any
other, and the emptied source cannot dangle, because it is `{null, 0}` (D17.6, D17.9).

```fort
i32 mut@ own a = new(i32, 4);
i32@ w = a;                          // a view
i32 mut@ own c = move(a);            // a is {null, 0}; w still designates the elements
i32 first = w[0];                    // fine
del(c);                              // w now dangles; a and c are {null, 0}
i32 gone = a[0];                     // proof error: access through a proved empty owner
```

Taking a span of a local array produces a span into that array's storage.
The proof rejects returned or retained borrows that outlive the storage (D6.7, D17.14).
The same rule covers local addresses, inline aggregate fields, and by-value parameter storage.
A borrowed parameter's slot and the symbolic caller storage it references have separate boundaries.

```fort
fn window() i32@ {
    i32[4] mut a = {1, 2, 3, 4};
    return a[1..3];                  // proof error: the local array storage ends at return
}
```

Copying a span copies the header only (D8.2); the elements are shared. A `for (T x : s)` loop
evaluates `s` once before the loop and copies each element at the start of its iteration (D7.5),
and holds its header before the first iteration. The whole-body loan prohibits collection-storage
invalidation, including through aliases and calls (D17.10).

### 4.4 Strings

A string literal is stored in read-only memory with one NUL byte after its last character that
`len` does not count; its `.ptr` may be passed to a C function expecting a NUL-terminated
`char*` (D3.7). A sub-string shares its source's bytes and is not NUL-terminated. A string built
at run time (section 9.5) is NUL-terminated only if the program put a NUL there. `std.str`
provides duplication with a trailing NUL, and standard-library functions that hand a path to C
copy it into a NUL-terminated buffer first (D13.2, D13.4). Writing through a string that was
cast to `u8 mut@` is undefined when the bytes are read-only (D10.7).

```fort
string lit = "path";                 // bytes: p a t h NUL; lit.len == 4
string sub = lit[0..2];              // "pa", no NUL after 'a'
extern fn open(char* path, i32 flags, ...) i32;
i32 fd = open(lit.ptr, 0);           // fine: a literal is NUL-terminated
i32 fd2 = open(sub.ptr, 0);          // opens "path": C reads past sub.len to the NUL
```

`string own` is the owned form (D17.12): `str.dup`, `str.concat` and `strbuf.take` return it,
while literals, sub-strings and `sys.args()` are `string` views (D13.5). It lends as `string`
in every operand position, its sub-strings are `string` views, `del(string own)` frees the
characters and `del(string)` does not compile. A cast among `string`, `char@`, `u8@` and
their `mut` forms yields `own` exactly when its target spells it (D3.14, D17.12), so a string
built in a `u8 mut@ own` becomes a `string own` with `cast(move(buf), string own)`
(section 9.5): the target says `own`, so the `own` lvalue must be moved (D17.5), which leaves
`buf` empty and the bytes with exactly one owner; `cast(buf, string)` lends a view instead.

```fort
string own d = str.dup("path");      // a heap copy with a trailing NUL (D13.5)
string t = d;                        // lends
string head = d[0..2];               // a sub-string: a view
bool eq = d == "path";               // operands of == lend (D17.4)
del(head);                           // error: cannot del a string: not an own type (D17.12)
del(t);                              // error: cannot del a string: not an own type (D17.12)
del(d);                              // frees the copy; d is {null, 0}, which equals ""
```

### 4.5 Handing memory to C

Spans, strings, structs and fixed arrays never cross an `extern` boundary (D9.8, D13.4). Unpack
`.ptr` and `.len`, cast the pointer to the declared C type, and pass a struct by address.

```fort
extern fn write(i32 fd, u8* buf, u64 n) i64;
extern fn memset(u8 mut* p, i32 c, u64 n) u8 mut*;

fn put(string s) void {
    write(1, cast(s.ptr, u8*), s.len);
}
fn clear(u8 mut@ b) void {
    memset(b.ptr, 0, b.len);
}
extern fn sum(i32@ xs) void;        // error: spans cannot cross an extern boundary
```

`own` in an `extern` signature is erased and records who frees (D17.13): a result type
`void* own` says the caller frees, a parameter type `void* own` says the callee does. The
`mut` of `void mut*` is separate and says whether C may write the storage the address names, so
`libc.malloc` is `void mut* own` and `libc.free` takes `void* own` (D3.11, D5.4); the outermost
position carries no `mut` on a result, which has no binding (D5.5).
Memory received from C is used through `p[lo..hi]` (section 3.1) and released with `del` or the
C library's own function, whichever the C side documents; `new`/`del` and `malloc`/`free` are
interchangeable (D10.3). Memory from C enters the `own` discipline only through an `extern`
whose result says `own` (D17.3, D17.13), because a cast never adds `own` (D3.14). Handing an
`own` value to a C function that frees it is a `move` into its `own` parameter. Declare a C
result `own` when the caller frees it, and plain when C keeps it. A span over an `own` result is
a view (`p[0..n]`), so the pointer stays the one `own` value of the allocation (D17.14). A C
function whose result is owned on some calls only is declared in its borrowing form, and a fort
function allocates the buffer (D17.13; `stdlib.md` 2.2 declares `realpath` so). `del` of an
`extern` result that does not start an allocation is undefined (D10.7).

An extern declaration is the implicit foreign trust boundary (D17.13).
It requires no lifetime annotation, unsafe construct, compiler contract, or effect summary.
Trust its declared ABI, types, ownership convention, storage validity, and extent.
Check known fort argument sources before the call. Apply signature ownership transfers.
An own argument transfers its obligation. A borrowed argument alone does not transfer ownership.
An own result creates a fresh obligation under the declaration's uniqueness promise.
A borrowed result has a trusted foreign source. Permit access and slicing without an inferred
source relation or static allocation extent. Missing foreign bodies or summaries cause no error.
This trust does not prove non-nullness, initialization, alignment, or foreign storage lifetime.
Foreign callers and implementations must satisfy those requirements.

Keep known fort ownership facts across the call. Missing summaries do not block later cleanup.
Hidden foreign aliases, retention, releases, writes, and callbacks remain outside the proof.
This is a proof limit, not evidence that foreign code has no effects.
A foreign result can alias fort storage. Without a source relation, the proof cannot invalidate it
when fort later releases that storage. Foreign buffer reuse and handle closure remain outside proof.
Preserve foreign trust through copies, casts, fields, returns, and fort wrappers.
Other fort sources keep their proof obligations. A raw cast cannot create foreign trust.
Trust arguments supplied at foreign entry points. Analyze each fort callback body with symbolic
caller storage and ordinary fort ownership rules (`fir.md` 14).

```fort
extern fn malloc(u64 n) void mut* own;
extern fn free(void* own p) void;
extern fn read_line(u64 mut* len) char mut* own;   // C documents: the caller frees with free()

u8 mut* own raw = cast(malloc(64), u8 mut* own);   // own rvalue to own type; not zeroed
u8 mut@ bytes = raw[0..64];                    // a view for filling
free(cast(move(raw), void* own));              // or del(raw); raw == null either way
u64 mut n = 0;
char mut* own line = read_line(&n);            // the extern says own: the caller frees
char mut@ text = line[0..n];                   // a view of the line
char mut@ own copy = cast(text, char mut@ own);   // error: a cast never adds own (D3.14)
char mut@ own mid = cast(line[1..n], char mut@ own);   // error: the same rule
del(line);                                     // frees what C allocated
u8 mut@ own b = new(u8, 64);
free(cast(b.ptr, void* own));                  // error: a cast never adds own to the view b.ptr
```

The last line would make a second owner: `b` would still look live after it, so a `del(b)` would
free twice and an assignment to `b` would trap on the overwrite check. Free `own` spans with
`del`.

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
i32[4] mut b = a;                    // 16 bytes copied
b[0] = 9;                            // a[0] is still 1
fn zero(i32[4] mut arr) void { arr[0] = 0; }
zero(a);                             // a is unchanged: arr was a copy
fn zero_in_place(i32 mut@ arr) void { arr[0] = 0; }
i32[4] mut c = {1, 2, 3, 4};
zero_in_place(c[..]);                // c[0] == 0: the span points into c
```

Multi-dimensional arrays are arrays of arrays, laid out row-major (D3.6). `i32[3][4] m` is 48
bytes; `m[i][j]` is at byte offset `(i * 4 + j) * 4`; `m[i]` is an `i32[4]` lvalue. A span of
rows, `i32[4] mut@ own`, comes from `new(i32[4], n)` (D3.6, D10.2).

```fort
i32[3][4] mut m = {};
m[2][3] = 1;                         // offset 44
i32[4] row = m[2];                   // copies 16 bytes
i32[4] mut@ own rows = new(i32[4], n);  // n rows, zeroed
rows[0][1] = 7;
rows[1] = row;                       // copies a whole row into the span's storage
u64 k = m[i].len;                    // 4: a constant; m[i] is not evaluated (D4.6)
```

## 6. Runtime checks

Every check below is compiled into the program. A failing check is a runtime error (section 7)
except where the table says otherwise. Casts never trap (D3.14) and float arithmetic never traps
(D6.12).

| Check           | Fires when                    | Message (values in decimal)                   |
|-----------------|-------------------------------|-----------------------------------------------|
| index `e[i]`    | `i >= len`, compared unsigned | `index 5 out of range for length 3`           |
| span `e[a..b]`  | not `0 <= a <= b <= len`      | `span bounds 2..7 out of range for length 3`  |
| `+ - *`, `-e`   | result does not fit           | `integer overflow`                            |
| `++ --`         | result does not fit           | `integer overflow`                            |
| `+= -= *=`      | result does not fit           | `integer overflow`                            |
| `<< >> <<= >>=` | count `< 0` or `>= width`     | `shift count 64 out of range for i64`         |
| `/ % /= %=`     | divisor is zero               | `division by zero`                            |
| `/ % /= %=`     | `MIN / -1` or `MIN % -1`      | `division overflow`                           |
| `new(T, n)`     | `n < 0` at run time           | `negative allocation count -1`                |
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
| index, span                 | runtime error     | runtime error            | check removed       |
| `+ - *`, unary `-`, `++ --` | runtime error     | wraps (two's complement) | unchanged           |
| `+= -= *=`                  | runtime error     | wraps (two's complement) | unchanged           |
| `+% -% *%`, `+%= -%= *%=`   | wraps             | wraps                    | unchanged           |
| shift count                 | runtime error     | count taken modulo width | unchanged           |
| division                    | runtime error     | runtime error            | unchanged           |
| `new` count, size, failure  | runtime error     | runtime error            | unchanged           |
| `own` overwrite (D17.11)    | runtime error     | no runtime check        | unchanged           |
| `assert`, `panic`           | runtime error     | runtime error            | unchanged           |
| `noreturn` guard            | trap              | trap                     | unchanged           |

Notes:

- Index and span checks compare unsigned: a signed index is sign-extended and a negative value
  becomes a huge unsigned number that fails the single comparison (D6.8). A constant index out of
  range for a fixed array is a compile error instead (D6.8). `p[lo..hi]` on a pointer is never
  runtime checked (D6.9). Fort raw-pointer spans still require static source and extent proof.
- Overflow checks cover signed and unsigned integers alike, so `len - 1` on an empty span traps
  in checked mode (D11.1, D16). The wrapping operators exist so hashes and counters behave
  identically in both modes (D11.2). Programs must not rely on either overflow behavior (D11.1).
- `<<` discards bits shifted out without a check, so `1 << 31` on `i32` is `-2147483648` in
  both modes; only the count is checked (D6.2, D11.1).
- Division checks apply at every width and in both modes (D6.13, D11.3).
- `assert` is active in both modes; its message carries the source text of the argument (D12.2).
- The overwrite check guards every store into an lvalue of `own` reference type, a declaration
  included, and nothing else: `move`, `del` and aggregate assignments emit no runtime overwrite
  check (D17.11, section 2.5). Release builds omit that check.
  The selected static proof rejects stores that lose old owning leaves in all build modes.
- The `noreturn` guard is `call void @llvm.trap()` followed by `unreachable`, emitted after the
  body of a `noreturn` function and after every call to one (D8.5, D19.7). It is reachable only
  when an `extern` declared `noreturn` returns anyway. Linux x86-64 raises SIGILL.
  Mac arm64 raises SIGTRAP. Neither target flushes buffered output before that signal.
- `--no-bounds-check` exists for benchmarking and is unsafe (D10.6).

```fort
i32 mut@ own s = new(i32, 3);
i32 a = s[3];                        // runtime error: index 3 out of range for length 3
i64 k = -1;
i32 b = s[k];                        // runtime error: index -1 out of range for length 3
u8 mut c = 255;
c++;                                 // checked: integer overflow; release: c == 0
u64 n = s.len - 4;                   // checked: integer overflow; release: wraps
u32 mut h = 0;
h = h *% 31 +% 7;                    // wraps in both modes
i32 sc = 32;
i32 d = a << sc;                     // checked: shift count 32 out of range for i32; release: a
i32 zero = 0;
i32 e = a / zero;                    // runtime error: division by zero
assert(s.len == 3);                  // passes
assert(s.len == 4);                  // main.ft:15:1: assertion failed: s.len == 4
u8 mut@ own mut buf = new(u8, 2);
buf = new(u8, 4);                    // proof error: the previous allocation would be lost
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
output printed before a failure is never lost. A buffer whose descriptor is interactive -- one
`isatty` accepts, asked once when the runtime creates that buffer -- flushes at every newline as
well, which is C's rule: the same program shows each `println` as it runs on a terminal and holds
its output until it exits when it is redirected to a file or a pipe. The runtime exports
`std.rt.flush(i32 fd)` and `std.rt.flush_all()`; `io.close` and `io.flush` call the former.
Program start and exit use the main that the compiler emits (D11.6).
It builds args, calls the source entry, flushes, and releases runtime argument-header storage.
The final normal-exit boundary then precedes the masked return (D17.19, section 2.9).
The runtime is `std.rt`, fort like the rest of the library (D13.1).

## 8. Undefined behavior

The ownership proof rejects invalid or unproved fort temporal storage operations (D10.7, D17.14).
It rejects local storage escapes, use of released fort borrows, and proved empty-owner dereferences.
It also checks cleanup and ownership transfer obligations. These are compile-time errors.
Staged whole-feature selection follows `toolchain.md` 1 (D19.8).

The following behavior remains undefined under D10.7. Foreign trust can hide these violations.

| Undefined behavior | Boundary |
|---|---|
| Foreign dangling result or hidden invalidation | Storage validity uses D17.13 trust. |
| Foreign ownership duplication or invalid release | An own result must satisfy its declaration. |
| Foreign own result inside an allocation | Del requires the allocation start. |
| Foreign raw-pointer range beyond the object | Foreign extent uses D17.13 trust. |
| Null dereference | Existing nullability rules still apply. |
| Calling a null function pointer | The ownership proof adds no new null check. |
| Data races | Foreign threads can race on shared storage. |

The proof does not establish total memory safety. Bounds and arithmetic keep their existing rules.
Double del through one emptied owner remains a no-op. Zero-value inspections remain legal (D17.9).
Del of a view, sub-span, .ptr, stack address, or literal remains a type error.
A cast never adds own or mut (D3.14).

There is no strict-aliasing rule. Reading through another same-size type yields the bit pattern.
The access still needs a live source and sufficient extent (D10.7, D17.14).

```fort
f64 x = 1.0;
u64 bits = *cast(&x, u64*);          // 0x3FF0000000000000
```

## 9. Idioms

### 9.1 Ownership transfer

The function that allocates returns the `T mut* own` or `T mut@ own`; the caller owns it and
frees it, and the type says so (D17.3, D17.5). A function that takes an allocation over
declares an `own` parameter, and its callers write `move` for lvalues (D17.5). Fields that own
are declared `own` too, so that a struct's `free` function can `del` them (D13.5).

```fort
struct node {
    i32 value;
    node mut* own next;
}

fn make_node(i32 v) node mut* own {
    node mut* own n = new(node);     // zeroed: next == null
    n->value = v;
    return n;                        // implicit move: responsibility passes to the caller
}

fn free_chain(node mut* own head) void {
    while (head != null) {
        node mut* own next = move(head->next);   // level 1 of head is mutable (D17.6)
        del(head);                   // head == null afterwards
        head = move(next);           // stores into an emptied binding: no trap (D17.11)
    }
}

fn demo() void {
    node mut* own a = make_node(1);
    a->next = make_node(2);          // an rvalue into a zeroed field
    node* second = a->next;          // lends
    free_chain(move(a));             // a == null afterwards; second dangles
    free_chain(a);                   // a null chain: nothing happens
    free_chain(second);              // error: passing a view to an own parameter (D17.3)
}
```

### 9.2 Out-parameters and error returns

Functions return `bool` or an error enum and deliver results through `T mut*` out-parameters;
`-1` and `null` sentinels are used where C convention expects them; `panic` is for programming
errors (D13.3). A span result uses a pointer to an `own` slot, `u8 mut@ own mut* out` (D3.6,
D13.5, D17.2): the callee moves the header through `*out`, and the caller, who initialized the
slot to `{}` so that the store passes the overwrite check (D17.11), owns the elements.

```fort
enum parse_error { none, empty, bad_digit, overflow }

fn parse_u32(string s, u32 mut* out) parse_error {
    if (s.len == 0) { return parse_error.empty; }
    u32 mut acc = 0;
    for (char c : s) {
        if (c < '0' || c > '9') { return parse_error.bad_digit; }
        u32 d = cast(c, u32) - '0';
        if (acc > (4294967295 - d) / 10) { return parse_error.overflow; }
        acc = acc * 10 + d;
    }
    *out = acc;
    return parse_error.none;
}

fn find(i32@ xs, i32 key, u64 mut* index) bool {
    for (u64 mut i = 0; i < xs.len; i++) {
        if (xs[i] == key) { *index = i; return true; }
    }
    return false;
}

fn read_all(i32 fd, u8 mut@ own mut* out) bool {
    u8 mut@ own buf = new(u8, 4096);
    defer del(buf);                  // frees buf on every failure path
    // ... fill buf, returning false on failure ...
    *out = move(buf);                // buf is {null, 0}: the deferred del is a no-op
    return true;
}

fn read_wrong(i32 fd, u8 mut@ own mut* out) bool {
    u8 mut@ own buf = new(u8, 16);
    *out = buf;                      // error: copying an own lvalue requires move(buf) (D17.5)
    return true;
}

fn use_both() void {
    u32 mut v = 0;
    switch (parse_u32("42", &v)) {
    case parse_error.none: println(v);
    case parse_error.empty, parse_error.bad_digit, parse_error.overflow: println("bad input");
    }
    u8 mut@ own mut data = {};
    if (read_all(0, &data)) {        // &data is u8 mut@ own mut*: a pointer to an own slot
        defer del(data);
        println(data.len);
    }
}
```

### 9.3 A growable buffer without generics

One struct per element type: an `own` backing span plus a count of the elements in use
(D13.5). The backing span is replaced by a larger one when full, and because `del` empties the
field first, the replacement passes the overwrite check (D17.11). The struct is an owning
aggregate and is passed by pointer (D17.7); `std.vec` provides `int_vec` and `ptr_vec` on this
pattern (D13.2).

```fort
struct int_buf {
    i32 mut@ own items;              // backing storage; items.len is what is allocated
    u64 count;                       // elements in use
}

fn push(int_buf mut* b, i32 x) void {
    if (b->count == b->items.len) {
        i32 mut@ own bigger = new(i32, b->items.len * 2 + 8);
        for (u64 mut i = 0; i < b->count; i++) { bigger[i] = b->items[i]; }
        del(b->items);               // no-op the first time; leaves {null, 0}
        b->items = move(bigger);     // the store finds an empty field: no trap
    }
    b->items[b->count] = x;
    b->count++;
}

fn contents(int_buf* b) i32@ {
    return b->items[..b->count];     // a view; del of it does not compile (D17.9)
}

fn free_buf(int_buf mut* b) void {
    del(b->items);                   // b->items is {null, 0} afterwards
    b->count = 0;
}

fn misuse(int_buf mut* b) void {
    int_buf copy = *b;               // error: copying an owning value requires move (D17.7)
    del(*b);                         // error: del of an aggregate; del its fields instead (D17.7)
}
```

### 9.4 Cleanup with `defer`

Place a `defer` right after each acquisition; deferred statements run in reverse order, so
resources are released in the opposite order of acquisition.

```fort
import std.io;

fn copy_file(string src, string dst) bool {
    i32 mut in = io.open_read(src);
    if (in < 0) { return false; }
    defer io.close(in);
    i32 mut out = io.open_write(dst);
    if (out < 0) { return false; }   // closes in
    defer io.close(out);             // closes out, then in, at every later exit
    u8 mut@ own buf = new(u8, 65536);
    defer del(buf);
    // ... copy loop ...
    return true;
}
```

### 9.5 Building a string in a `u8 mut@ own`

Allocate the bytes, fill them, and `cast` the moved span to `string own` (D3.14, D17.12). The
cast's result is `own` because its target says so, and an `own` lvalue cast to an `own` target
must be moved (D17.5), which empties the buffer so that the characters have exactly one owner
(D17.14); the caller frees the result with `del`. Allocate the exact length: a prefix such
as `buf[..n]` is a view, and a view never becomes a `string own`, because a cast never adds
`own` (D3.14).

```fort
fn repeat(char c, u64 n) string own {
    u8 mut@ own buf = new(u8, n);
    for (u64 mut i = 0; i < n; i++) { buf[i] = cast(c, u8); }
    return cast(move(buf), string own);   // the caller owns the characters
}

fn demo() void {
    string own s = repeat('-', 10);
    defer del(s);                    // del(string own) is legal (D17.12)
    println(s);
    string head = s[..5];            // a sub-string: a view
    del(head);                       // error: cannot del a string: not an own type (D17.12)
    del(cast(s, u8@));               // error: not an own type; the cast lends a view (D17.12)
}
```

For strings whose length is not known in advance, `std.strbuf` grows a `u8 mut@ own` with
the pattern of section 9.3, hands out `string` views of the bytes written so far, and returns
a `string own` from `take` (D13.2, D13.5).

## 10. Not in v1

Arena allocators as a language feature, alignment attributes, and packed attributes remain deferred
(D15). The ownership proof belongs to v1 (D17.14, D19.8).
A stricter prohibition on current zero-value inspections after move or del remains deferred.
The proof does not add a general resource type system for scalar handles.
The extern boundary keeps the foreign proof limits of section 4.5.
