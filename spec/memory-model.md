# fort memory model

This document specifies where fort values live, how heap memory is obtained and released, what
pointers, spans and strings are at run time, which checks the compiled program performs, and
which result each operation has: a fort program has no undefined behavior (D10.7). It implements
the decisions in `decisions.md`, cited as `(Dn.m)`; where it
disagrees with `decisions.md` or `grammar.md`, they win and this document has a bug. Types and
mutability are specified in `type-system.md`; build modes and the runtime in
`toolchain.md`.

**The state since 2026-11-11.** The user ruled on 2026-11-11: "I don't want anything undefined."
This document states the end state of that ruling: dead storage and frame storage (D10.9), the
zero bytes of a local, of a global, of an emptied place and of an element of `regrow` (D10.1), the
runtime errors `null dereference`, `null call` and `invalid
release` (D10.6), the allocator of the runtime (D10.3), `regrow` (D12.2), and no option that removes
a check (D10.6). The compiler writes the zero bytes of a local on the stack now (`fir.md` 7). The
compiler and the runtime implement the other rules in later changes.

## 1. Memory regions

| Region         | Holds                                                    | Lifetime            |
|----------------|----------------------------------------------------------|---------------------|
| stack          | locals, parameters, local fixed arrays and struct values | the enclosing block |
| frame storage  | each local that an `&` names (D10.1, D10.9)              | the enclosing block |
| heap           | storage from `new` or `regrow`, held by `own` references | until its release   |
| read-only data | string literals, module-level constants `Type NAME = e;` | the program         |
| writable data  | module-level globals `Type mut g = e;`                   | the program         |

Stack storage is released when the block that declares it exits (D10.1); temporaries live until the
end of the enclosing statement (D6.3). A local that an `&` names is in frame storage
and not on the stack (D10.1, D10.9). An `&` names a local when the function holds an `&e`, or a
span expression of `e` when `e` is a fixed array, for a place `e` that is the local or a part of
it (D19.4). The term reads the source and no run: a local that an `&` names in code that no run
reaches is in frame storage too.
Frame storage is storage of the same kind as an allocation. The storage of such a local is dead
after the end of the scope of the local, and each frame storage of a run is dead after the run
returns. The runtime never gives dead storage to another object (section 8). A local that no `&`
names has no address, so no pointer reaches it. When the declaration of a local runs,
each byte of its storage holds 0, padding included; then the initializer stores (D10.1). `new` has
the same rule (D10.2). A local of a loop body gets its zero bytes at each run of its declaration, on
the stack and in frame storage. Each byte of the storage of a parameter holds 0 when its run starts,
before the argument goes in, and each byte of the storage of a temporary holds 0 when its statement
makes the temporary. Each byte of the storage of a global and of a module-level constant holds 0,
padding included, before its initializer stores, and the initializer stores before the run starts.
The initializer writes no padding byte, so each padding byte holds 0 when the run starts.
A `move` or a `del` that empties a place leaves 0 in each byte of the place, padding included, and
the emptying stays no assignment for the owner rules (D17.6, D17.9). `regrow` copies each byte of
each element that it moves, padding included, and each byte of each element that it adds holds 0
(D12.2). After that, a byte changes only when a store or an emptying of the program writes it, or
when C writes it. So each byte of the storage of a local, of a global, of a module-level constant
and of an allocation holds 0, the byte that one of these wrote last, or the byte that `regrow`
copied from an old element. A load of a padding byte through a view gives that byte in each build
(section 8.4). The heap is reached only through `new`
and `regrow`, and released only through `del` and `regrow` (D10.1, D12.2); `new` yields an `own`
reference and `del` takes one, so the type of a value says whether freeing it is its holder's job
(D17.1, section 2.3). String literals and
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
`del(buf); del(buf);` frees once (D17.9). On an rvalue operand `del` only frees.

The storage of the allocation is dead after the `del` (D10.9, section 8). A use of a local owner
after its `del` is a compile error (D17.21). A use of an owner that is no local owner after its
`del` reads the zero value, so a dereference of it is the runtime error `null dereference` and an
index of it is the index error (D10.6). A `del` of a pointer that is not `null` and is no start of a
live allocation of the runtime is the runtime error `invalid release` (D10.9). Such a pointer is an
interior pointer, a pointer to dead storage, or an address that C gave. So a second `del` of one
allocation through a copy of its owner is this error.

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
| an operand, not zero, that starts no allocation    | runtime error: `invalid release` (D10.9)    |
| `T*`, `T@`, `string`, `void*` without `own`        | error: a view, not an `own` type            |
| a sub-span `s[lo..hi]` or `s[..]`, a `.ptr`        | error: taking a span and `.ptr` yield views |
| `&local`, a literal                                | error: not an `own` type                    |
| a function pointer                                 | error: not an allocation                    |
| an integer, struct or fixed array                  | error: `del` is shallow (D17.7)             |

```fort
node mut* own n = new(node);
del(n);                              // n == null afterwards
del(n);                              // no-op
n->value = 1;                        // error: use after move or del: 'n' can be vacated here
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

The runtime has its own allocator, which never gives the storage of a released allocation to another
object (D10.9). So `new`/`del` and C `malloc`/`free` are not interchangeable (D10.3). A C function
whose declaration takes the owner as `own`, for example `free`, releases an owner that C gave, and
only a trusted module calls that function (D9.8). A `del` of an owner that C gave is the runtime
error `invalid release`, because its address is no start of a live allocation of the runtime.
Storage of `new` or of `regrow` that a call gives to the C `free` breaks the contract of `free`: the
call is a case of the trusted premise (D10.7, section 8). `own` in an `extern` signature is erased
and records the C side's convention (D17.13). A cast never adds `own`, as it never adds `mut`
(D3.14), so C memory has an owner only where its `extern` says `own` (D17.3):

```fort
import extern;  // a trusted module (D9.8); rows omitted
extern fn malloc(u64 n) void mut* own;   // storage of no type the caller may write (D17.13)
extern fn free(void* own p) void;

void mut* own blob = malloc(16);     // not zeroed: C did not clear it
free(move(blob));                    // C releases what C gave; blob == null afterwards
void mut* own other = malloc(16);
del(other);                          // runtime error: invalid release (D10.9)
node mut* own q = new(node);
free(cast(move(q), void* own));      // the trusted premise: free gets storage of new (D10.7)
```

### 2.3 Ownership: `own`, `move` and lending

Whether a reference is responsible for its allocation is part of its type. `T* own`, `void* own`,
`T@ own` and `string own` designate the start of a live allocation: one of `new` or of `regrow`, or
one that a C function gave whose `extern` result says `own`. `del` releases an allocation of `new`
or of `regrow`. A `del` of an owner that C gave is the runtime error `invalid release`, and a C
function that takes the owner as `own` releases it (D10.3, section 2.2). `T*`, `T@`, `void*` and
`string`
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
The `cast` that adds `own` is reserved for memory from C (section 4.5).

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

The same shape delivers a result through an out-parameter: `del(*out); *out = move(buf);`
empties `buf`, so the deferred `del` frees only on the failure paths (section 9.2).

**Transfer into a container.** A container holds its storage as `own` fields; pushing moves the
new element in and popping moves it out, and every field it overwrites was emptied first, so
the overwrite check of section 2.5 passes (D17.11). In `push`, the entry value of `n->next` is
unknown, so `push` empties it first. `n` is a parameter and not a fresh `new`, so the store
through `n` kills the fact about `l->head`: `n` and `l` can name one object for the proof. So
`push` empties `l->head` again. A `push` that makes its node with `new` needs no second `del`
(`module-system.md` 12):

```fort
fn push(list mut* l, node mut* own n) void {
    del(n->next);                        // the entry value of n->next is unknown
    n->next = move(l->head);             // l->head is empty afterwards
    del(l->head);                        // frees nothing: the store through n killed its fact
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
    arena_drop(move(*a));                // *a is {{null, 0}, 0} afterwards
}

fn arena_drop(arena mut a) void {
    del(a.block);                        // frees every chunk at once
}

fn arena_drop_made(arena mut* own p) void {
    arena kept = move(*p);
    del(p);
    arena_drop(move(kept));
}

arena mut* own ar = new(arena);          // the calls take the arena by pointer (D17.20)
defer arena_drop_made(move(ar));
ar->block = new(u8, 4096);
u8 mut@ tmp = arena_alloc(ar, 64);
del(tmp);                                // error: cannot del 'tmp': not an own type (D17.9)
```

The arena lives in an allocation of `new`, because the calls lend it: a call kills what the
compiler knows of a local whose address the function takes (D17.11), so `arena_free(&ar);` at the
end of a local `arena mut ar` would leave an exit that the leak rule cannot prove (D17.20).
`arena_drop_made` moves the arena out, frees the allocation and then the block.

`defer` captures nothing: the statement runs with the values its variables hold at exit (D7.8,
D16), so after `defer del(p);` a later `del(p); p = move(q);` makes the exit free `q`; had the
`del(p)` been left out, the assignment would be the compile error of the overwrite check
(section 2.5).
Prefer `defer` immediately after the `new` it undoes, before the variable is rebound.

### 2.5 Ownership at run time

`own` is erased: a `T* own` is the same eight bytes as a `T*`, a `T@ own` or `string own`
the same sixteen as a view, with the same alignment, layout and calling convention, and
`extern` signatures with `own` name the same C functions (D17.1, D17.13). The compiler emits
three things for ownership, and it checks two more at compile time, with the use rule of D17.21:

- **`move(lv)`** loads the operand's value and empties the operand. It leaves the zero value
  there: eight zero bytes for a pointer or `void*`, sixteen for a span or string, the whole
  value for an owning aggregate (D17.6). After the emptying each byte of the operand holds 0, and
  the padding bytes of an owning aggregate are among them (D10.1).
- **`del(lv)`** passes the pointer (or the span's `ptr`) to the runtime's `free` and then empties
  the operand; `del(rv)` only frees (D17.9; `toolchain.md` 6). The runtime releases a pointer that
  is the start of a live allocation of its allocator, and it does nothing for `null`. Each other
  pointer is the runtime error `invalid release` (D10.9).
- **`regrow(s, n)`** gives the operand span to the runtime, which moves its elements into a new
  allocation of `n` elements and releases the old storage (D12.2). The first `s.len` elements keep
  their values, and each other element holds the zero value. `regrow` copies each byte of each
  element that it moves, padding included, and each byte of each other element holds 0, padding
  included (D10.1). Before the release, `regrow` stores
  the zero value into each owner slot in the fixed storage of each old element, and each other byte
  of the old storage keeps its value. So a `move` through a view of an old element gives the zero
  value or a memory fault, and no second owner. The old storage is dead (D10.9). The operand is the
  zero span, which gives the result of `new(T, n)`, or its `.ptr` is the start of a live allocation
  of the runtime. Each other operand is the runtime error `invalid release` (D10.9). A count `n`
  below `s.len` is a runtime error in both build modes.
- **The overwrite check.** Every store into an lvalue of `own` reference type, a declaration
  included, is legal only when the compiler proves, inside the function, that the lvalue holds the
  zero value at the store (D17.11). The compiler tests the store after the right-hand side.
  Otherwise the store is a compile error at the assignment's `=`, or at the declared name of a
  declaration: `overwriting owned value: cannot prove '<place>' is empty here`, with the note
  `del or move '<place>' before this store`. The previous allocation would otherwise leak.
  Checked and release builds give the same verdict, and no build emits a runtime check for it.
  The proof is the forward must-empty analysis of D17.11 over the FIR of the function
  (`fir.md` 11). It tracks locals, globals, their fixed field paths, field paths through a
  pointer local whose address the function does not take, parameters included, and elements
  whose index is a constant or such a local. A store of an `own` reference into any other place
  is always an error. An `own` field through a parameter needs
  `del` or `move` first, because its entry value is unknown. `del` and `move` leave zero behind,
  so for locals `v`, `a` and `b`, `del(v.data); v.data = new(...)`, `a = move(b)` after
  `move(a)`, and a store into a local that holds `{}` or `null` all pass. A test against `null`
  makes the place that it tests empty on the edge where it finds `null`, until a kill rule of
  D17.11 applies, so `if (l->head == null) { l->head = new(node); }` passes (gen rule 6). The slot
  of an owning local is zeroed once on entry, so a declaration in a loop whose body does not `del`
  or `move` the local is an error: the back edge brings a live value to the declaration.
  A store of a whole value of an owning aggregate is legal only when the compiler proves that each
  `own` reference in the fixed storage of its destination holds the zero value at the store, with
  the same facts. A declaration passes, because the storage of a local whose scope starts holds no
  allocation. The compiler first reports each such store that it cannot prove and accepts the
  program; a later change makes each such store a compile error (D17.11).
- **The leak rule (D17.20).** A named local of `own` reference type at the end of its scope, and
  such a parameter or temporary at a `return`, must hold the zero value. So must each owner in the
  fixed storage of such a local of an owning aggregate type (D17.7), and the error names the first
  owner that the compiler does not prove, such as `b.data` or `v[0]`. The compiler proves it
  with the facts of the overwrite check. Otherwise the exit is a compile error, because the
  allocation can leak: `leak: cannot prove '<place>' is empty at this exit`, with the note
  `del or move '<place>' before this exit`. The error stands at the closing brace, or at the
  `return`, `break` or `continue` that leaves the scope. `del(p)` of a `T* own`, where `T` holds
  owners in its fixed storage, needs each owner of `*p` proved empty:
  `leak: cannot prove '<place>' is empty before this del`, at the `del` keyword, with the note
  `move '<place>' into a local and del it first`. A `cast` that carries `own` from
  `T* own` to another pointee type needs the same proof, and its error ends `before this cast`.
  `<place>` is source text, such as `p->items`, and never names a FIR local. The owners of `*p`
  are tracked only through a pointer local that is not memory (D17.11), so such a `del` or `cast`
  of any other operand is an error, unless the compiler proves `p` itself empty: a `del` or such a
  `cast` of the zero value frees nothing, so it needs no proof of the owners of `*p`, as after
  `move(p)`, after `del(p)` and in the arm where a test finds `p` null (D17.20). `del` fixes a leak
  at an exit, and it does nothing on an empty owner (D17.9). For a `del(p)` whose `*p` can hold
  live owners, the fix moves each inner owner into a local first. A span whose elements hold
  owners ends through `del(s, d)` (D17.9): the compiler moves each element into the drop `d`, a
  release-only fort function, and then frees the storage, so the leak rule needs no proof for the
  owners of the elements. A `del(s)` of such a span with one operand needs no proof either, and it
  leaks what the elements hold; `--span-del-report` lists it until it is an error. A `cast` of
  such a span to another element type is an error. A span local of `own` type still needs the
  zero value at an exit. An
  exit that the compiler cannot prove is a compile error. The compiler first reports each `del` and
  each `cast` that it cannot prove and accepts the program; a later change makes each such site a
  compile error (D17.20).

```fort
u8 mut@ own mut buf = new(u8, 4);
buf = new(u8, 8);                    // error: overwriting owned value
del(buf);
buf = new(u8, 8);                    // fine: buf was {null, 0}
u8 mut@ own other = move(buf);       // buf is {null, 0} again
buf = move(other);                   // fine
list mut l = {};
l.head = new(node);                  // fine: zero-initialized field
l = list{};                          // reported, later an error: l.head can hold a node (D17.11)
```

What the compiler does not track (D10.7, D17.14): a view, or a copy of an `own` value made before a
`move` or `del`, that is used after the allocation was freed; and an `own` value that is never freed
(a leak), outside the leak rule of D17.20. Each has a defined result. The first reads or writes dead
storage (D10.9, section 8). The second is a defined leak (D10.7). A `del` of an `extern` result that
is not the start of an allocation of the runtime is the runtime error `invalid release` (D10.9). The
compile-time (linear) check that would make the other leaks and the other uses after `move` errors
is deferred (D15). `move` and `del` zero what they take. So a use of an owner that is no local owner
after its `move` or `del` reads the zero value, and a dereference of it is the runtime error `null
dereference` (D10.6). The use rule of D17.21 covers a use of a local owner after its `move` or
`del`: a use of such a local owner that a `move`, a `del` or a call can have vacated is a compile
error.

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
| `cast(e, T*)`   | from a pointer to no larger a `T`    | as written; a cast never adds `own`  |
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
| `p[lo..hi]` | unchecked span of `hi - lo` elements   | in `str.from_cstr`, `rt.args_init` only  |

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
del(o);                              // o == null; w points at dead storage (D10.9)
```

`null` is the zero pointer and function-pointer value (D10.5). A dereference of `null` is the
runtime error `null dereference`, and a call of a null function pointer is the runtime error `null
call`, each in both build modes (D10.5, D10.6). Each evaluation of a place that goes through a null
pointer is `null dereference`: a load, a store, an `&`, a span expression, a `move` and a `del` of
such a place, also when the operation reads no storage, as `&p->f`. A pointer that outlives its
storage points at dead storage. A read through it gives the value that the storage held last, a
write through it changes only dead storage, and each can stop the program with a memory fault.
Neither reads or changes a live object (D10.9, section 8).

A `cast` gives a pointer only from another pointer (D3.14). No cast takes an integer to a pointer
or a `void*`, no cast takes a `void*` to a typed pointer, and no cast makes the pointee larger.
So each pointer that fort code holds reaches the storage of an address that it derives from,
inside that object, or it is `null` or a pointer that C gave. The object is live, or its storage
is dead (D10.7, D10.9):

```fort
i32[2] mut pair = {1, 2};
u64 a = cast(&pair, u64);            // ok: a pointer gives its address as a u64
i32* next = cast(a + 8, i32*);       // error: a cast never makes a pointer from an integer
void* v = cast(&pair, void*);        // ok
i32[2]* back = cast(v, i32[2]*);     // error: a cast never gives a type to a void*
i64* wide = cast(&pair[0], i64*);    // error: the pointee grows from 4 to 8 bytes
i64* whole = cast(&pair, i64*);      // error: the alignment grows from 4 to 8 bytes
i32* first = cast(&pair, i32*);      // ok: 4 bytes from 8, with the same alignment
i32* past = pair[2..].ptr;           // error: '.ptr' can name the byte after the last element
i32* elem = &pair[1];                // ok: the index is checked
```

### 3.1 From a raw pointer to a span

There is no pointer arithmetic (D10.4), and a pointer has no length. No span expression takes a
raw pointer, but in the bodies of `str.from_cstr` and `rt.args_init`: `p[lo..hi]`, `p[lo..]`,
`p[..hi]` and `p[..]` on a `T*` or a `T mut*` are each the error `cannot take a span of T*: a
pointer has no length` (D6.9). The
bounds of such a span could reach past the object, and fort code would reach storage whose
address it does not hold. A program gets a span from `new(T, n)` (D10.2), from a fixed array,
from a span or a string, all with their checks, and a span of a C string from the one trusted
form, `str.from_cstr(char* p) string` (`stdlib.md` 2.5). That form trusts that a NUL ends `p`
inside its storage, as `strlen` does (A7 of `ownership-design.md` 5.1), and it takes no `.ptr`
(D3.5). A buffer that C wrote a C string into goes to `str.from_cbuf(u8@ b) string`, which reads
only the `b.len` bytes of `b` and panics when none of them is a NUL.

In those two bodies, `p[lo..hi]` yields a `T@` (or `T mut@` from a `T mut*`) with `ptr` advanced
by `lo` elements and `len == hi - lo`, performing no check at all. `str.from_cstr` makes
`p[0..libc.strlen(p)]`, and `rt.args_init` makes `argv[0..argc]` from the values that C passes to
`main`; only the `main` that the compiler emits calls `rt.args_init`, and each name of it in
source is an error (D6.9). Each other function of `std` takes a span or a
string, the entries of `std.rt` that the compiler calls included. Only a trusted module calls
`str.from_cstr`, with a call row (D9.8). So a range of the two functions that extends beyond the
object, or `hi < lo`, is a case of the trusted premise: the arguments break the contract of the
callee (D10.7, section 8).

```fort
import extern;                       // a trusted module (D9.8); rows omitted
import std.libc;
import std.str;

fn env_value(string name) string {   // name must be NUL-terminated, for example a literal
    return str.from_cstr(libc.getenv(&name[0]));   // null gives the empty string
}
char* p = libc.getenv(&"HOME"[0]);
char@ tail = p[3..];                 // error: cannot take a span of char*: a pointer has no length
char@ head = p[0..3];                // error: the same, outside std
void* vp = cast(p, void*);
u8@ bytes = vp[0..4];                // error: cannot take a span of a void*
u8 mut@ own raw = new(u8, 1);
u8 mut@ one = raw[0..1];             // a view of the allocation, with its check
del(one);                            // error: cannot del 'one': not an own type (D17.9)
del(raw);                            // frees the byte; one points at dead storage (D10.9)
```

### 3.2 `void*`

`void*` is an address with no pointee type: it cannot be dereferenced, cannot reach fields,
cannot be indexed and has no span expression (D3.11). A pointer or a function pointer casts to
it, and it casts to `u64`. No cast takes it back to a typed pointer or to a function pointer
type, and no cast takes an integer to it (D3.14): a `void*` holds no type and no size, so a cast
back could give a larger type than the storage has. It exists for `extern` signatures: fort code
gives an address to C as a `void*`, and an opaque handle of C, such as a directory stream, is a
`void*` that goes back to C. An address whose type fort code needs later stays in a typed slot,
or it is an index into a typed span (`stdlib.md` 2.7).
`void* own` is its owning form (D17.1): what `free` takes (D17.13) and a legal operand of `del`.
A `del` of a `void* own` that C gave is the runtime error `invalid release` (D10.3, D10.9).
`malloc` answers `void mut* own`, because the storage it gives back is storage the caller may
write, and that mark drops to `void* own` at the call to `free` (D3.11, D5.4). Fort code gets no
typed view of that storage, so a program that wants typed storage uses `new`.

```fort
import extern;  // a trusted module (D9.8); rows omitted
extern fn malloc(u64 n) void mut* own;
extern fn free(void* own p) void;
void mut* own blob = malloc(16);
point mut* own pt = cast(move(blob), point mut* own);   // error: no type for a void*
point mut* own typed = new(point);   // the typed allocation
del(typed);
free(move(blob));                    // C releases the block of malloc (D10.3)
```

## 4. Spans and strings in memory

### 4.1 Layout

A span `T@` is sixteen bytes: a pointer to the first element followed by a `u64` element count
(D3.5). A `string` has the same layout with `char` elements (D3.7). Both have alignment 8.
Whether the header owns its elements is part of the type, never of the bits: an `own` span or
`string own` is the same two words as a view (D17.1). A view's elements live in frame storage (a
span of a local array, D10.9), on the heap (`new`, seen through a lent `own` value or a span of
one), in
read-only data (a literal, a span of a module constant) or in C memory (`str.from_cstr`); an
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
i32 mut@ g = arr[1..3];              // points into arr's frame storage (D10.9)
i32@ h = make_array()[..];           // error: cannot take a span of a fixed array rvalue
i32 mut@ own i = a[..];              // error: a view cannot initialize an own place (D17.3)
del(b);                              // error: cannot del a sub-span: it is a view (D17.9)
```

### 4.3 Aliasing and lifetime

A span and its source designate the same elements: writes through one are visible through the
other. When the storage is released, the span points at dead storage (D10.9, section 8).

```fort
i32 mut@ own a = new(i32, 4);
i32 mut@ b = a[1..3];
b[0] = 9;                            // a[1] == 9
del(a);                              // b points at dead storage: a use reads or writes only that
```

A `move` changes which reference is responsible, not the allocation: views made before it stay
valid until the allocation is freed. A copy made by lending (`i32@ w = a;`) is a view like any
other, and the emptied source points at no storage, because it is `{null, 0}` (D17.6, D17.9).

```fort
i32 mut@ own a = new(i32, 4);
i32@ w = a;                          // a view
i32 mut@ own c = move(a);            // a is {null, 0}; w still designates the elements
i32 first = w[0];                    // fine
del(c);                              // w points at dead storage (D10.9); a and c are {null, 0}
i32 gone = a[0];                     // runtime error: index 0 out of range for length 0
```

Taking a span of a local array puts the array in frame storage and produces a span into it (D10.9).
The compiler does not diagnose a program that returns the span, stores it in a heap object, or keeps
it past the block (D6.7). After the end of the scope of the array, the span points at dead storage,
and the behavior is defined: a read gives the value that the storage held last, a write changes only
dead storage, and each can stop the program with a memory fault. No later local takes that storage
(D10.7, D10.9).

```fort
fn window() i32@ {
    i32[4] mut a = {1, 2, 3, 4};     // in frame storage: the function takes a span of it
    return a[1..3];                  // defined: the result points at dead storage (D10.9)
}
```

Copying a span copies the header only (D8.2); the elements are shared. A `for (T x : s)` loop
evaluates `s` once before the loop and copies each element at the start of its iteration (D7.5),
so replacing `s` inside the loop does not change what is iterated.

### 4.4 Strings

A string literal is stored in read-only memory with one NUL byte after its last character that
`len` does not count; its `.ptr` may be passed to a C function expecting a NUL-terminated
`char*` (D3.7). A sub-string shares its source's bytes and is not NUL-terminated. A string built
at run time (section 9.5) is NUL-terminated only if the program put a NUL there. `std.str`
provides duplication with a trailing NUL, and standard-library functions that hand a path to C
copy it into a NUL-terminated buffer first (D13.2, D13.4). No fort code writes the bytes of a
literal: a cast never adds `mut`, so no cast takes a `string` to a `u8 mut@` (D3.14).

```fort
import extern;  // a trusted module (D9.8); rows omitted
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
`.ptr` and `.len`, and pass a struct by address. No cast takes a `.ptr` (D3.5), so cast the span to
the element type of the C parameter first: `cast(s, u8@).ptr`.

```fort
import extern;  // a trusted module (D9.8); rows omitted
extern fn write(i32 fd, u8* buf, u64 n) i64;
extern fn memset(u8 mut* p, i32 c, u64 n) void;

fn put(string s) void {
    write(1, cast(s, u8@).ptr, s.len);
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
Memory received from C goes back to C, and fort code reads a C string of it through
`str.from_cstr` (section 3.1). The C function that the C side documents releases it: `new`/`del`
and `malloc`/`free` are not interchangeable (D10.3).
Bytes that fort code reads and writes live in a span from `new`, which a C function fills
through `.ptr`, as `read` does. Memory from C enters the `own` discipline only through an
`extern` whose result says `own` (D17.3, D17.13), because a cast never adds `own` (D3.14).
Handing an `own` value to a C function that frees it is a `move` into its `own` parameter.
Declare a C result `own` when the caller frees it, and plain when C keeps it. A C
function whose result is owned on some calls only is declared in its borrowing form, and a fort
function allocates the buffer (D17.13; `stdlib.md` 2.2 declares `realpath` so). A `del` of an
`extern` result is the runtime error `invalid release`, because its address is no start of a live
allocation of the runtime (D10.3, D10.9).

```fort
import extern;                                     // a trusted module (D9.8); rows omitted
extern fn malloc(u64 n) void mut* own;
extern fn free(void* own p) void;
extern fn read_line(u64 mut* len) char mut* own;   // C documents: the caller frees with free()

void mut* own raw = malloc(64);                // not zeroed; fort code gives it to C only
free(move(raw));                               // C releases it; raw == null afterwards
u64 mut n = 0;
char mut* own line = read_line(&n);            // the extern says own: the caller frees
char mut@ text = line[0..n];                   // error: cannot take a span of char mut*
string seen = str.from_cstr(line);             // a view of the line up to its NUL
free(cast(move(line), void* own));             // C frees what C allocated; del(line) is
                                               // the runtime error invalid release (D10.9)
u8 mut@ own b = new(u8, 64);
free(cast(&b[0], void* own));                  // error: a cast never adds own to the view &b[0]
```

The last line would make a second owner, and it would give storage of `new` to the C `free`
(D10.3): `b` would still look live after it, and an assignment to `b` would be the compile error
of the overwrite check. Free `own` spans with `del`.

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

| Check            | Fires when                    | Message (values in decimal)                  |
|------------------|-------------------------------|----------------------------------------------|
| index `e[i]`     | `i >= len`, compared unsigned | `index 5 out of range for length 3`          |
| span `e[a..b]`   | not `0 <= a <= b <= len`      | `span bounds 2..7 out of range for length 3` |
| `*p`, `p->f`     | `p` is `null`                 | `null dereference`                           |
| a call `f(...)`  | the function value is `null`  | `null call`                                  |
| `+ - *`, `-e`    | result does not fit           | `integer overflow`                           |
| `++ --`          | result does not fit           | `integer overflow`                           |
| `+= -= *=`       | result does not fit           | `integer overflow`                           |
| `<< >> <<= >>=`  | count `< 0` or `>= width`     | `shift count 64 out of range for i64`        |
| `/ % /= %=`      | divisor is zero               | `division by zero`                           |
| `/ % /= %=`      | `MIN / -1` or `MIN % -1`      | `division overflow`                          |
| `new`, `regrow`  | `n < 0` at run time           | `negative allocation count -1`               |
| `regrow(s, n)`   | `n < s.len`                   | `regrow count 2 below length 3`              |
| `new`, `regrow`  | `n * sizeof(T)` exceeds `u64` | `allocation size overflow`                   |
| `new`, `regrow`  | the runtime has no storage    | `out of memory`                              |
| frame storage    | the runtime has no storage    | `out of memory`                              |
| `del(p)`         | `p` starts no live allocation | `invalid release`                            |
| `regrow(s, n)`   | `s` starts no live allocation | `invalid release`                            |
| `assert(c)`      | `c` is `false`                | `assertion failed: <text>`                   |
| `panic(m)`       | always                        | `panic: <m>`                                 |
| `noreturn` guard | a `noreturn` function returns | none: `llvm.trap` (D19.7)                    |

The message texts are fixed (D11.4); the numbers shown (the index and the length, the two
bounds and the length, the shift count and the shifted operand's type, the negative count, the
count and the length of a `regrow`) are
the offending values of the failing check, written as signed decimals.

| Check                                   | Checked (default) | Release (`--release`)    |
|-----------------------------------------|-------------------|--------------------------|
| index, span                             | runtime error     | runtime error            |
| `null dereference`, `null call`         | runtime error     | runtime error            |
| `+ - *`, unary `-`, `++ --`             | runtime error     | wraps (two's complement) |
| `+= -= *=`                              | runtime error     | wraps (two's complement) |
| `+% -% *%`, `+%= -%= *%=`               | wraps             | wraps                    |
| shift count                             | runtime error     | count taken modulo width |
| division                                | runtime error     | runtime error            |
| `new` and `regrow` count, size, failure | runtime error     | runtime error            |
| frame storage failure                   | runtime error     | runtime error            |
| `invalid release`                       | runtime error     | runtime error            |
| `own` overwrite (D17.11)                | compile error     | compile error            |
| `assert`, `panic`                       | runtime error     | runtime error            |
| `noreturn` guard                        | trap              | trap                     |

Notes:

- Index and span checks compare unsigned: a signed index is sign-extended and a negative value
  becomes a huge unsigned number that fails the single comparison (D6.8). A constant index out of
  range for a fixed array is a compile error instead (D6.8). `p[lo..hi]` on a pointer is never
  checked, and only `str.from_cstr` and `rt.args_init` can write it (D6.9).
- The check of `null` stands before each evaluation of a place that goes through a pointer, and
  before each call through a function value (D10.5, D10.6; `fir.md` 8). A load, a store, an `&`, a
  span expression, a `move` and a `del` of such a place take it, also when the operation reads no
  storage, as `&p->f`. An index of a span or a string takes no check of `null`: a header whose
  pointer is `null` is the zero value, whose length is 0, so its index check fails. A `del` of
  `null` has no effect and is no error (D17.9).
- The runtime makes four groups of tests inside its entries, with no branch in the caller (D19.6):
  the size and the storage of a `new`, the five tests of a `regrow` (D12.2), the storage of the
  frame storage of a run, and the `invalid release` of a `del`. The test of `invalid release` is
  exact, because the runtime knows the start of each live allocation (D10.9). It reads the pointer
  of a `del` and the `.ptr` of the operand of a `regrow`. A `del` of `null` and a `regrow` of the
  zero span release nothing, and they are no error. A `regrow` tests in this order: a negative `n`,
  an `n` below `s.len`, the size, the operand, the storage.
- Overflow checks cover signed and unsigned integers alike, so `len - 1` on an empty span traps
  in checked mode (D11.1, D16). The wrapping operators exist so hashes and counters behave
  identically in both modes (D11.2). Programs must not rely on either overflow behavior (D11.1).
- `<<` discards bits shifted out without a check, so `1 << 31` on `i32` is `-2147483648` in
  both modes; only the count is checked (D6.2, D11.1).
- Division checks apply at every width and in both modes (D6.13, D11.3).
- `assert` is active in both modes; its message carries the source text of the argument (D12.2).
- The overwrite check guards every store into an lvalue of `own` reference type, a declaration
  included, and every store of a whole value of an owning aggregate, which it reports and does not
  refuse until a later change (D17.11, section 2.5). `move` and `del` have no overwrite check. It
  is a compile-time check in every build mode, and no build emits a runtime check for it.
- The `noreturn` guard is `call void @llvm.trap()` followed by `unreachable`, emitted after the
  body of a `noreturn` function and after every call to one (D8.5, D19.7). It is reachable only
  when an `extern` declared `noreturn` returns anyway. Linux x86-64 raises SIGILL.
  Mac arm64 raises SIGTRAP. Neither target flushes buffered output before that signal.
- The compiler has no option that removes a check (D10.6). Until 2026-11-11 the option
  `--no-bounds-check` removed the index and span checks. The compiler removes the option in a later
  change. Bootstrap-0 is frozen, and it keeps its option.

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
buf = new(u8, 4);                    // error: overwriting owned value
node* none = null;
i32 v = none->value;                 // runtime error: null dereference
fn () void f = null;
f();                                 // runtime error: null call
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

`<file>` is the path the compiler opened (the search root as given plus the relative module path);
`<line>:<col>` is the position of the check's operator token, or of the builtin's name for `new`,
`regrow`, `del`, `assert` and `panic`; the `assert` text is the source text of the expression,
verbatim (D11.4). The operator token of `null dereference` is the `*` or the `->` whose operand is
`null`. The operator token of `null call` is the `(` of the call. An `out of memory` for the frame
storage of a run stands at the name of the function in its definition. Test files match these lines
with `//! stderr: <substring>` and `//! abort` (D14.5).

A memory fault is no runtime error of this contract (D10.9, D11.4). It is the signal of the system
for an access to an address that has no storage (`SIGSEGV` or `SIGBUS`). Two operations can give it:
an access to dead storage (section 8.2), and a call whose frame the stack cannot hold (D10.8,
section 1). The system ends the process. The runtime writes no line and does not flush buffered
output, as for the trap of a `noreturn` function.

```sh
$ fort main.ft && ./a.out
main.ft:7:13: runtime error: index 5 out of range for length 3
$ echo $?
134
```

Output buffering (D11.5): `print` and `println` write to a runtime buffer for standard output;
`eprint` and `eprintln` are unbuffered; `fprint` and `fprintln` use one buffer per descriptor,
and `fprint(1, ...)` shares the stdout buffer with `print`. An `extern` write to a descriptor
bypasses the buffers, so it can overtake buffered output unless the program flushes first.
Buffers flush when full, on `io.close` and `io.flush`, at exit, and before any runtime error, so
output printed before a runtime error is never lost. A buffer whose descriptor is interactive -- one
`isatty` accepts, asked once when the runtime creates that buffer and once more at the first output
after a reset of the buffer -- flushes at every newline as well, which is C's rule: the same program
shows each `println` as it runs on a terminal and holds its output until it exits when it is
redirected to a file or a pipe. The runtime exports `std.rt.flush(i32 fd)` and `std.rt.flush_all()`;
`io.flush` calls the former. `io.close` and `io.dup2` call `std.rt.reset_buffer(i32 fd)`, which
flushes the buffer and makes it ask `isatty` again at its next output, because the descriptor can
name a file of another type after the call.
Program start and exit are the `main` the compiler emits: it asks the runtime to build `args`,
calls the entry module's `main`, flushes, and returns that result masked to eight bits (D11.6).
The runtime is `std.rt`, fort like the rest of the library (D13.1).

## 8. No undefined behavior

A fort program has no undefined behavior (D10.7). The compiler refuses a program with a diagnostic,
or each operation that the program runs has a result that the decisions define: a value, a runtime
error (section 7), the trap of a `noreturn` function (section 6), one of the results of section 8.2
for an access to dead storage, or the memory fault of D10.8 when the stack cannot hold a frame. One
premise stays outside the guarantee: the trusted premise (section 8.3).

### 8.1 The defined results

An earlier text of this section listed cases of undefined behavior. Each case has a defined result
(D10.7):

| Case                                                            | Defined result      | Rule   |
|-----------------------------------------------------------------|---------------------|--------|
| a view or a copy, used after the `del` of its allocation        | dead storage        | D10.9  |
| a pointer to a local, used after its scope or after its run     | dead storage        | D10.9  |
| a dereference of `null`                                         | `null dereference`  | D10.6  |
| a call of a null function pointer                               | `null call`         | D10.6  |
| an owner that is no local owner, used after its `move` or `del` | the zero value      | D17.6  |
| a `del` or a `regrow` of no start of a live allocation          | `invalid release`   | D10.9  |
| a data race                                                     | cannot occur        | D9.8   |
| an index or a span expression out of range                      | a runtime error     | D10.6  |
| `p[lo..hi]` beyond the object                                   | the trusted premise | D6.9   |
| a store over an owner that is not empty, a whole store          | a compile error     | D17.11 |
| a leak of a local owner, a shallow `del`                        | a compile error     | D17.20 |
| a `del` of a span of owners with one operand                    | a compile error     | D17.9  |
| a local owner, used after its `move` or `del`                   | a compile error     | D17.21 |

The first row is a use of a view, or of a copy made before a `move` or `del`, after the `del` of the
allocation or after the `regrow` that released it (D12.2, D17.14). A span of a local array that a
function returns or stores is the span form of the second row (section 4.3). Each entry of the block
that declares a named local starts a new local, so a pointer to the local of an earlier entry points
at dead storage. An owner that is no local owner is an owner in a global, a place through a pointer
such as `p->next`, or an element of a span such as `kids[i]` (D17.21). After its `move` or its `del`
the place holds the zero value (D17.6), and each use reads that value. Each use has one result
(D10.7):

- a dereference of it is the runtime error `null dereference` (D10.6);
- an index of it is the index error, and a span expression of it with a bound above 0 is the span
  error, because its `.len` is 0 (D10.6);
- its `.len` is 0, and its `.ptr` is `null`;
- a `del` of it has no effect (D17.9);
- a `move` of it gives the zero value (D17.6);
- a comparison of it with `null` is true (D10.5);
- a copy of it, as a view or as an argument that lends it, has the zero value.

No owner is a function pointer (D17.1), so no call goes through an owner. A `move` of an owning
aggregate leaves the zero value in each field (D17.6), so a call through a function-pointer field of
that aggregate is the runtime error `null call` (D10.6). A second `del` through one reference has no
effect (D17.9), and a second `del` through a copy is `invalid release` (D10.9). `del` of a view, a
sub-span, a `.ptr`, the address of a local or a literal is a compile error (D17.9), and no cast adds
`own` to such a value (D3.14).

A fort program has one thread: no trusted module declares a C function that starts a thread, and the
lint of D9.8 reports a declaration of `pthread_create`, `clone`, `clone3`, `thrd_create` or `vfork`.
C calls a fort function only while a call of C that fort code made runs, so no fort function runs
between two operations of a run (D9.8, "Calls from C"). A call of C that fort code makes returns one
time to its caller, or it ends the process. No row makes a declaration of `setjmp`, `_setjmp`,
`sigsetjmp`, `__sigsetjmp`, `getcontext`, `longjmp`, `_longjmp`, `siglongjmp`, `setcontext` or
`swapcontext` legal. The lint of D9.8 reports each one (D9.8, "Calls from C"). Only `str.from_cstr`
and `rt.args_init` can write `p[lo..hi]` (D6.9), and only a trusted module calls them, with a call
row (D9.8), so a span beyond the object is a case of the trusted premise. The compile errors are the
four obligations of D17.11, which the type of a place selects: what the compiler cannot prove is an
error. The compiler has no option that removes a check (D10.6).

```fort
i32 mut@ own a = new(i32, 4);
i32@ v = a;                          // a view
del(a);
i32 x = v[0];                        // reads dead storage, or stops with a memory fault
node* q = null;
i32 y = q->value;                    // runtime error: null dereference
fn () void f = null;
f();                                 // runtime error: null call
```

### 8.2 Dead storage

The storage of an allocation is dead after its `del` (D10.3) and after its `regrow` (D12.2). The
storage of a local in frame storage is dead after the end of the scope of the local, and each frame
storage of a run is dead after the run returns (section 1). Each entry of the block that declares a
named local starts a new local, with new storage. The storage of a global and the storage of a
string literal are never dead. Storage of C is no dead storage: it is storage that a call of C gave
and that the runtime does not hold as an allocation or as frame storage. No rule of this section
gives the result of an access to storage of C after C, the system or another process released it.
A function of a trusted module gives fort code no pointer, no span and no string to storage of C
that one of the three can release or change while fort code holds the value, or its row says why
none of them does (D9.8). The rule holds at any depth: it covers each pointer, each span and each
string to storage of C that fort code loads through the value. So an access of fort code to
storage of C that one of the three released shows a row that is wrong or absent, which is case 1
of the trusted premise (section 8.3). A function of a trusted module gives its caller no function
pointer that C gave, and no value through which fort code can load one, at any depth. The rule holds
also for a function pointer that C stores, or gives to a fort function, in a call that the function
makes. No row makes such a value legal (D9.8). Six rules hold for dead storage (D10.9):

1. The runtime never gives dead storage to another object. No later allocation, frame storage,
   global or allocation of C takes an address of it: the runtime keeps the address range reserved.
   The runtime keeps no state of its own in dead storage.
2. A read through a pointer or a span to dead storage gives the value that the storage held last, or
   it stops the program with a memory fault. The value that the storage held last is the value that
   it held when it died, with the change of each later write through a pointer or a span to that
   dead storage. A write through such a value changes only dead storage, or it stops the program
   with a memory fault. Neither reads or changes a live allocation, the storage of a live local, a
   global, the frame of a run or the state of the runtime. Each access has one of its two results,
   and the rule does not say which: the runtime can keep the storage, or it can give the storage
   back to the system and keep the address range. A read gives no third result, so a value that the
   program reads from dead storage is the value that the storage held last (section 8.4). A `move`
   of an owner out of dead storage gives no second owner. Each operation that takes an owner out of
   a place leaves the zero value in that place. A `move` empties its operand, the implicit `move`
   of `return x` included, a `del` empties its operand, and a `regrow` stores the zero value into
   each owner slot of each old element (D12.2, D17.5, D17.6, D17.9). So a place in dead storage
   whose owner went to another place held the zero value last. A store of an owner into dead
   storage stores a new allocation, or it takes the owner out of its source by one of these
   operations. So the place in dead storage is then the one place that holds the owner. The
   behavior is defined.
3. A local that an `&` names is in frame storage (section 1). Frame storage is storage
   of the same kind as an allocation: rule 1 and rule 2 hold for it.
4. `del(p)` of a pointer that is not `null` and is no start of a live allocation of the runtime is
   the runtime error `invalid release`. Such a pointer is an interior pointer, a pointer to dead
   storage, or an address that C gave. For a span and a `string`, the pointer is the `.ptr`. The
   test is exact: the runtime knows each start. `regrow(s, n)` makes the same test of the `.ptr` of
   `s` (D12.2).
5. When the runtime has no storage, or no address range, for a `new`, for a `regrow` or for the
   frame storage of a run, the operation is the runtime error `out of memory`.
6. C code that a trusted module calls can get a pointer or a span to dead storage, because a
   function of a trusted module gives C an argument of its caller. Inside the bound that the call
   row gives (D9.8), an access that C code or a system call makes to dead storage has the results of
   rule 2. A system call has one more result: for an address that the system cannot access, the
   call can give the error return of its contract (`EFAULT`), or the count of the bytes that it
   moved before that address when its contract has such a count. Such a call is no case of the
   trusted premise (section 8.3). A call that changes a mapping of dead storage is no access of this
   rule: `munmap`, `mprotect`, and `mmap` with a fixed address. The runtime makes such a call only
   to give dead storage back to the system, and it keeps the address range. Rule 1 and rule 2 do
   not hold after each other such call. Its call row says why its address range holds no dead
   storage, and a call whose row cannot say so is case 3 of the trusted premise.

A memory fault is the signal of the system for an access to an address that has no storage
(`SIGSEGV` or `SIGBUS`). The system ends the process, and the runtime writes no line (section 7). A
view of an allocation, used after the `del`:

```fort
node mut* own n = new(node);
node* v = n;            // a view
del(n);
i32 x = v->value;       // reads dead storage, or stops with a memory fault.
                        // It never reads or changes a live object.
```

A pointer to a local, used after the run returns:

```fort
fn bad() i32* {
    i32 x = 5;
    return &x;          // `x` is in frame storage; the result points at dead storage
}
```

**Defined leaks.** Five cases keep an allocation until the process ends. Each is a defined result,
and no rule makes it an error (D10.7):

- an allocation that only a cycle of owners holds, or that the program reaches only through such a
  cycle;
- an allocation that an owner in a global holds, or that the program reaches only through such an
  owner;
- an allocation that an owner holds on a path that calls `exit`, or that ends in a runtime error, a
  `panic`, a failed `assert` or a memory fault;
- an allocation whose only owner is a place in dead storage, which a store through a pointer or a
  span to dead storage wrote, or that the program reaches only through such an owner;
- an allocation whose only owner is a place in storage of C, which a store through a pointer or a
  span to storage of C wrote, or that the program reaches only through such an owner.

### 8.3 The trusted premise

Fort cannot define what C code does, and it cannot define what a standard library does that the
compiler did not check. Four cases have the behavior that C gives them, and fort defines no more
(D10.7):

1. A trusted declaration or call whose row is wrong or absent (D9.8), or a function of a trusted
   module whose row for storage of C is wrong or absent (D9.8). A declaration that no row makes
   legal, and a function of a trusted module that breaks the third rule for storage of C, are this
   case too (D9.8). Examples are `memchr` declared with an `i64*` result for a search of one byte, a
   function that gives its caller a view of a block of `malloc` that a later call of `free`
   releases, a function that gives its caller a pointer to a record of C whose field holds a
   function pointer, a declaration of `signal` whose parameter gives C a fort function as the
   handler, and a declaration of `setjmp` or of `longjmp` (D9.8, "Calls from C").
2. C code that breaks A7 of `ownership-design.md` 5.1. Examples are C code that keeps an argument
   and gives it back in a later call, C code that calls a fort function when no call of C that
   fort code made runs, and C code that returns a second time from a call that fort code made.
3. A call in a trusted module whose arguments break the contract of its callee, which its call row
   states wrongly or does not state. Examples are a `char*` with no NUL inside its storage for
   `strlen`, a range of `str.from_cstr` beyond the object (section 3.1), and storage of `new` for
   `free` (D10.3). A call of C that changes a mapping of dead storage is this case too: `munmap`,
   `mprotect`, or `mmap` with a fixed address, for an address in a range that the runtime keeps
   reserved. The calls by which the runtime gives dead storage back to the system are the one
   exception (rule 6 of section 8.2).
4. A build with a standard library other than the one of the compiler: a build with `--std-dir`
   (D14.1), or with `--use std=<path>` of an artifact that `fort --lib std` built from another root
   (D9.11).

A module outside the trusted set declares no extern, calls no extern and names no boundary function
(D9.8), and the three rules for storage of C bind only a function of a trusted module (D9.8). So
cases 1 to 3 stand only in a trusted module. A module outside the trusted set has no undefined
behavior, with no condition on its code: it can keep a pointer to storage of C that a function of a
trusted module gave, copy it and load through it, because the row of that function covers each time
that fort code holds the value. The row covers also each pointer, each span and each string to
storage of C that fort code loads through the value, at any depth (D9.8). By the third rule, such a
module gets no function pointer that C gave from a function of a trusted module (D9.8). That holds
in each way that the rule names, at any depth. A pointer or a span to dead storage is no case of the
trusted premise: a module outside the trusted set can give such a value to a function of a trusted
module, which gives it to C. While each access of C stays inside the bound that the call row gives,
rule 6 of section 8.2 gives its result (D10.9). An access of C outside that bound is case 3, as for
live storage.

### 8.4 No forged pointer

Fort code reaches storage only through a value that derives from an address of that storage, inside
its object (D10.7). A pointer to dead storage is such a value: it derives from an address of the
storage that is now dead, and it reaches that dead storage only (D10.9). The compile-time rules and
the checks of section 6 give this. No cast makes a pointer from an integer, gives a type to a
`void*` or makes the pointee larger, and no view adds or changes a pointer place, an argument of an
`extern fn` included (D3.14). No span expression takes a raw pointer but in `str.from_cstr` and
`rt.args_init` (D6.9). No pointer arithmetic exists (D10.4). Each index and each span of a fixed
array, a span or a string is checked, and each place that goes through a pointer goes through a
pointer that is not `null`; no option removes these checks (D10.6). The `.ptr` of a span or a string
is legal only where no fort code loads or stores through it, so `&s[0]`, which is checked, names an
element (D3.5). Only a trusted module declares or calls an `extern fn`, or names a boundary
function, and the rows of each declaration and each call say why A7 holds (D9.8). C gives fort code
only storage that C gave, or storage that fort code gave to C, with the whole pointee inside it (A7
of `ownership-design.md` 5.1). So a read or a write of a live local goes through a value that
derives from an address of that local that its run took. A pointer to dead storage reaches no live
local, no live allocation and no global. Fort code reaches a frame of a C function, or a byte of an
allocator that is outside each allocation and outside dead storage, only through a pointer that C
gave to that storage. The guarantee holds under the trusted premise (section 8.3), and it has no
other condition.

```fort
fn next(i32* p) i32* {
    return cast(cast(p, u64) + 4, i32*);   // error: a cast never makes a pointer from an integer
}

fn f() i32 {
    i32[2] pair = {1, 2};
    i32 one = 3;
    i32 a = pair[1];                       // 2: the index is checked
    u64 bits = cast(&one, u64);            // ok: the address as an integer
    i32 b = *cast(bits, i32*);             // error: a cast never makes a pointer from an integer
    i32* p = &one;
    i32 c = *p;                            // 3: p derives from &one
    i64* wide = cast(&one, i64*);          // error: the pointee grows from 4 to 8 bytes
    return a + c;
}
```

The rules add no strict-aliasing rule: the derivation of a value decides, and the type of the
access does not.

There is no strict-aliasing rule. Reading an object through a pointer to another type of the same
size or a smaller one is defined and yields the bit pattern (D10.7):

```fort
f64 x = 1.0;
u64 bits = *cast(&x, u64*);          // 0x3FF0000000000000
```

A load of a `bool` place gives the low bit of its byte, so it gives `true` or `false` for each bit
pattern (D3.3). A view of a `u8` that holds 2 reads `false` as a `bool`, and one that holds 3
reads `true`.

`cast(p, u64)` gives the number of the address that the pointer `p` holds, and the print family
writes that number for a pointer, a `void*` and a function pointer (D10.7, D11.7). One storage has
one number in one run: each operation of the run gives the same number for the same byte, while
its storage is live and after its storage is dead. No rule fixes the number. It can differ between
two runs of one binary, between two builds of one program, and between a binary and the
interpreter of `--fir-run`. So a program whose output depends on the number of an address has a
defined result in each run, and two runs can give different results.

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

fn free_chain(node mut* own mut head) void {
    while (head != null) {
        node mut* own next = move(head->next);   // level 1 of head is mutable (D17.6)
        del(head);                   // head == null afterwards
        head = move(next);           // stores into an emptied binding (D17.11)
    }
}

fn demo() void {
    node mut* own a = make_node(1);
    node mut* own b = make_node(2);
    del(a->next);                    // a call result is unknown here: empty the field first
    a->next = move(b);               // no call stands between the del and the store
    node* second = a->next;          // lends
    free_chain(move(a));             // a == null afterwards; second points at dead storage
    free_chain(move(a));             // a null chain: nothing happens (D17.6)
    free_chain(a);                   // error: a copy of an owner needs `move` (D17.5); the read
                                     // of `a` is also a use after its `move` (D17.21)
    free_chain(second);              // error: passing a view to an own parameter (D17.3)
}
```

### 9.2 Out-parameters and error returns

Functions return `bool` or an error enum and deliver results through `T mut*` out-parameters;
`-1` and `null` sentinels are used where C convention expects them; `panic` is for programming
errors (D13.3). A span result uses a pointer to an `own` slot, `u8 mut@ own mut* out` (D3.6,
D13.5, D17.2): the callee `del`s `*out` and then moves the header through it, because the entry
value of `*out` is unknown to the callee (D17.11). The caller initialized the slot to `{}`, so
that `del` frees nothing, and the caller owns the elements.

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
    del(*out);                       // the entry value of *out is unknown here (D17.11)
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
aggregate and is passed by pointer (D17.7); `std.vec` provides `int_vec` on this pattern
(D13.2), and a vector of pointers is a typed copy of it (`stdlib.md` 2.7). A backing span whose
elements hold owners grows with `b->items = regrow(move(b->items), n);`, which moves the elements
into the new storage with no store that the compiler must prove (D12.2).

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
        b->items = move(bigger);     // the store finds an empty field (D17.11)
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

Arena allocators as a language feature, leak detection outside D17.20, a compile error for a use of
a view after the `del` of its allocation, alignment and packed attributes, and compile-time (linear)
ownership tracking are outside v1; D15 lists the deferred features with the idiom to use for each.
`own` records who frees, and `move` and `del` zero what they take (D17). So a leak outside D17.20 is
a defined leak (D10.7). A use after `move` or `del` that D17.21 does not reject reads the zero
value: a dereference of it is the runtime error `null dereference`, and an index of it is the index
error (D10.6). A use of a view after the `del` of its allocation reads or writes only dead storage
(D10.9, D17.14). The `extern` boundary reaches every C allocator and every C library in the
meantime.
