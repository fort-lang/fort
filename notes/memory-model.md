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
| heap           | storage obtained from `new`                              | until `del`         |
| read-only data | string literals, module-level constants `Type NAME = e;` | the program         |
| writable data  | module-level globals `mut Type g = e;`                   | the program         |

Stack storage is released when the block that declares it exits (D10.1); temporaries live until
the end of the enclosing statement (D6.3). The heap is reached only through `new` and released
only through `del` (D10.1). String literals and module-level constants are placed in read-only
memory and are addressable (D3.7, D7.10). Frames larger than one page are probed on entry, so a
large local array combined with deep recursion faults on the guard page instead of skipping over
it (D10.8):

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
so no value ever starts undefined.

| Form              | Result type      | Meaning                                              |
|-------------------|------------------|------------------------------------------------------|
| `new(T)`          | `mut T*`         | one zeroed `T`                                       |
| `new(T[n])`       | `mut T[]`        | `n` zeroed elements; `n` is any integer type         |
| `new(T[n][K])`    | `mut T[][K]`     | `n` zeroed rows of `T[K]`; `K` is a constant         |
| `new(T[n][K][M])` | `mut T[][K][M]`  | `n` zeroed rows of `T[K][M]`                         |
| `new(T{...})`     | error            | allocate, then assign the fields                     |
| `new(T[])`        | error            | the count is mandatory                               |
| `new(void)`       | error            | `void` has no size                                   |
| `new(mut T)`      | error            | the result is always fully mutable (D5.8)            |

The first bracket after the element type always holds a run-time count and always produces a
slice; later brackets are fixed-array dimensions of the element type (grammar section 6).
`new(i32[4])` is therefore a `mut i32[]` of four elements, not an `i32[4]`. An untyped constant
count takes its default type (D4.5). A negative count, a total size that overflows, and
allocation failure are runtime errors (section 6); `n == 0` is allowed and yields a slice of
length 0 with a non-null `.ptr`, because the runtime allocates at least one byte (D10.2).

```fort
mut Point* p = new(Point);           // p->x == 0, p->y == 0
mut i32[] xs = new(i32[8]);          // eight zeroes
mut u8[][16] rows = new(u8[n][16]);  // n rows of sixteen bytes
mut i32[] none = new(i32[0]);        // none.len == 0, none.ptr != null; del it like any other
Point* q = new(Point{1, 2});         // error: new takes a type, not a literal
i32[] bad = new(i32[]);              // error: new needs an element count
void* v = new(void);                 // error: cannot allocate void
mut i32[] neg = new(i32[-1]);        // runtime error: negative allocation count
```

### 2.2 `del`

`del(x)` is a universe function that yields no value (D12.2). It accepts any pointer, `void*` or
slice, regardless of mutability, and frees the allocation it designates (D10.3). The argument
need not be an lvalue and is not modified: `del` does not null it.

| Argument                                       | Effect                                  |
|------------------------------------------------|-----------------------------------------|
| `T*` or `mut T*` obtained from `new(T)`        | frees the object                        |
| `void*` holding an allocation address          | frees it                                |
| `T[]` or `mut T[]` from `new(T[n])`, any `T`   | frees the elements                      |
| `T[][K]` from `new(T[n][K])`                   | frees the rows                          |
| `null`                                         | no-op                                   |
| a slice with `.ptr == null` and `.len == 0`    | no-op                                   |
| a `string`                                     | error: cast to `u8[]` first             |
| a function pointer                             | error: not an allocation                |
| an integer, struct or fixed array              | error: not a pointer or slice           |
| a sub-slice, interior pointer, stack address or literal | undefined behavior (D10.7)     |

```fort
mut Node* n = new(Node);
del(n);                              // n still holds the old address; using it is undefined
mut i32[] xs = new(i32[8]);
i32[] view = xs;
del(view);                           // fine: mutability does not matter
del(null);                           // no-op
i32[] empty = {};
del(empty);                          // no-op
string s = "abc";
del(s);                              // error: del of a string; cast to u8[] first
del(cast(s, u8[]));                  // undefined: a literal is not an allocation
i32 local = 1;
del(&local);                         // undefined: a stack address
del(xs[2..4]);                       // undefined: a sub-slice
```

Allocations carry no header: `new` is `calloc` and `del` is `free`, so memory from C `malloc`
may be released with `del` and memory from `new` may be released with `free` (D10.3):

```fort
extern fn void* malloc(u64 n);
extern fn void free(void* p);

mut i32* p = cast(malloc(sizeof(i32)), mut i32*);   // uninitialized: C did not zero it
*p = 1;
del(p);                              // same allocator as free
mut u8[] b = new(u8[64]);
free(cast(b.ptr, void*));            // also fine
```

### 2.3 Ownership and cleanup

Nothing is freed automatically. The idiom is: the function that calls `new` either frees the
storage on every path or hands it to its caller, and `defer` makes "every path" mechanical
(D7.8). Deferred statements run when the enclosing block exits by falling off the end, `return`,
`break` or `continue`, innermost block first and in reverse order within a block; a `return`
operand is evaluated before the deferred code runs, and a runtime error runs nothing (D7.8,
D11.4).

```fort
fn bool checksum_ok(u8[] data) {
    mut u32 sum = 0;
    for (u8 b : data) { sum = sum +% cast(b, u32); }
    return sum % 7 == 0;
}

fn bool process(u64 n) {
    mut u8[] buf = new(u8[n]);
    defer del(buf);                      // runs at every exit below
    for (mut u64 i = 0; i < buf.len; i++) { buf[i] = cast(i, u8); }
    if (buf.len < 16) { return false; }  // buf is freed after false is produced
    if (buf[0] != 0) { return false; }   // and here
    return checksum_ok(buf);             // and here, after the call completes
}
```

`defer` captures nothing: the statement runs with the values its variables hold at exit
(D7.8, D16), so `defer del(p); p = q;` frees `q`. Prefer `defer` immediately after the `new` it
undoes, before the variable is rebound.

## 3. Pointers

A pointer `T*` holds the address of one `T` or is `null`. It is obtained with `&` on an lvalue,
from `new`, from `.ptr` of a slice or string, from a `cast`, from a function name (for function
pointers), from `null`, or from an `extern` call (D10.4, D10.5, D7.10). The operations are (D6.7,
D6.10):

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
mut Point pt = {1, 2};
mut Point* pp = &pt;
pp->x = 5;
i32 y = pp.x;                        // error: use -> on a pointer
i32 z = p[0];                        // error: pointers cannot be indexed
mut i32* q = p + 1;                  // error: no pointer arithmetic
p++;                                 // error: no pointer arithmetic
Node* n = null;
bool none = n == null;
i32[] s = {};
bool empty = s == null;              // error: compare s.len or s.ptr
bool nn = null == null;              // error: null has no type here
```

`null` is the zero pointer and function-pointer value (D10.5). Dereferencing `null` or a dangling
pointer is undefined behavior, in practice a segmentation fault (D10.5, D10.7).

### 3.1 From a raw pointer to a slice

There is no pointer arithmetic (D10.4). The only way to view memory behind a raw pointer as
elements is `p[lo..hi]`, which yields a `T[]` (or `mut T[]` from a `mut T*`) with `ptr` advanced
by `lo` elements and `len == hi - lo`, performing no check at all (D6.9). It is the explicit
unsafe escape for foreign memory: a range that extends beyond the object, or `hi < lo`, is
undefined behavior (D10.7). Only the two-bound form exists for pointers, because a pointer has no
length; `p[lo..]`, `p[..hi]` and `p[..]` are errors, as is any slicing of `void*` (D6.9).

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
```

### 3.2 `void*`

`void*` is an address with no pointee type: it cannot be dereferenced, cannot reach fields, and
cannot be indexed or sliced; every conversion to and from it is a `cast` (D3.11). It exists for
`extern` signatures and for storing an address whose type is recovered later with `cast`.

## 4. Slices and strings in memory

### 4.1 Layout

A slice `T[]` is sixteen bytes: a pointer to the first element followed by a `u64` element count
(D3.5). A `string` has the same layout with `char` elements (D3.7). Both have alignment 8 and
own nothing: the elements live on the stack (a sliced local array), on the heap (`new`), in
read-only data (a literal, a sliced module constant) or in foreign memory (`p[lo..hi]`).

```
T[] s            +----------------+----------------+
                 | ptr: T*        | len: u64       |
                 +-------+--------+----------------+
                         |
                         v
elements         | s[0] | s[1] | ... | s[len - 1] |
```

`.ptr` and `.len` read the two words; neither is an lvalue (D6.7). The zero value `{null, 0}` is
what `= {}` produces and what a zeroed struct field holds. There is no expression form for a
zero slice outside a declaration initializer: to reset a slice-typed field, declare a variable
with `= {}` and assign it.

### 4.2 Slicing

`e[lo..hi]`, `e[lo..]`, `e[..hi]` and `e[..]` on a fixed array lvalue, a slice or a string produce
a slice (or string) of the same elements with `ptr` advanced by `lo` elements and `len` set to
`hi - lo`; omitted bounds are `0` and `len` (D6.9). The check `0 <= lo <= hi <= len` is against
the operand's own `len`, not the original allocation, so a slice can only shrink. The result's
element mutability is that of the operand's elements (D6.9).

```fort
mut i32[] a = new(i32[6]);           // {p, 6}
mut i32[] b = a[2..5];               // {p + 2 * 4 bytes, 3}
i32[] c = b[1..];                    // {p + 3 * 4 bytes, 2}
i32[] d = a[..];                     // same header as a, elements immutable
i32[] e = b[0..4];                   // runtime error: hi 4 exceeds b.len 3
i32[] f = a[4..2];                   // runtime error: lo exceeds hi
mut i32[4] arr = {1, 2, 3, 4};
mut i32[] g = arr[1..3];             // points into arr's stack storage
i32[] h = make_array()[..];          // error: a fixed array rvalue cannot be sliced
```

### 4.3 Aliasing and lifetime

A slice and its source designate the same elements: writes through one are visible through the
other, and both are invalidated together when the storage goes away.

```fort
mut i32[] a = new(i32[4]);
mut i32[] b = a[1..3];
b[0] = 9;                            // a[1] == 9
del(a);                              // b now dangles; using it is undefined
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

Memory received from C is used through `p[lo..hi]` (section 3.1) and released with `del` or the
C library's own function, whichever the C side documents; `new`/`del` and `malloc`/`free` are
interchangeable (D10.3).

## 5. Fixed arrays and structs

A fixed array `T[N]` occupies `N * sizeof(T)` contiguous bytes with the alignment of `T`; a
struct occupies its fields in declaration order with natural alignment and padding, rounded up
to its largest field alignment, exactly like the same C struct (D3.4, D3.8). Both are values:
assignment, argument passing and `return` copy the whole object (D8.2). In the internal calling
convention they are passed by a hidden pointer to a caller-made copy and returned through a
hidden result pointer, so a callee never sees the caller's storage (D9.9).

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
u64 k = m[3].len;                    // error: constant index 3 out of range for i32[3][4]
```

## 6. Runtime checks

Every check below is compiled into the program. A failing check is a runtime error (section 7)
except where the table says otherwise. Casts never trap (D3.14) and float arithmetic never traps
(D6.12).

| Check              | Fires when                                | Message text                   |
|--------------------|-------------------------------------------|--------------------------------|
| index `e[i]`       | `i` outside `0 .. len - 1`, unsigned      | `index out of range`           |
| slice `e[lo..hi]`  | not `0 <= lo <= hi <= len`                | `slice bounds out of range`    |
| `+ - *`, unary `-` | signed or unsigned result does not fit    | `integer overflow`             |
| `++ --`            | result does not fit                       | `integer overflow`             |
| `+= -= *=`         | result does not fit                       | `integer overflow`             |
| `<< >> <<= >>=`    | count negative or at least the width      | `shift count out of range`     |
| `/ % /= %=`        | divisor is zero                           | `division by zero`             |
| `/ % /= %=`        | `MIN / -1` or `MIN % -1`                  | `integer overflow`             |
| `new(T[n])`        | `n < 0`                                   | `negative allocation count`    |
| `new`              | `n * sizeof(T)` overflows `u64`           | `allocation size overflow`     |
| `new`              | the allocator returns `null`              | `out of memory`                |
| `assert(c)`        | `c` is `false`                            | `assertion failed: <text>`     |
| `panic(m)`         | always                                    | `panic: <m>`                   |
| `noreturn` guard   | a `noreturn` function returns             | none: trap instruction         |

| Check                       | Checked (default) | Release (`--release`)    | `--no-bounds-check` |
|-----------------------------|-------------------|--------------------------|---------------------|
| index, slice                | runtime error     | runtime error            | check removed       |
| `+ - *`, unary `-`, `++ --` | runtime error     | wraps (two's complement) | unchanged           |
| `+= -= *=`                  | runtime error     | wraps (two's complement) | unchanged           |
| `+% -% *%`, `+%= -%= *%=`   | wraps             | wraps                    | unchanged           |
| shift count                 | runtime error     | count taken modulo width | unchanged           |
| division                    | runtime error     | runtime error            | unchanged           |
| `new` count, size, failure  | runtime error     | runtime error            | unchanged           |
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
- `<<` discards bits shifted out without a check; only the count is checked (D11.1).
- Division checks apply at every width and in both modes (D6.13, D11.3).
- `assert` is active in both modes; its message carries the source text of the argument (D12.2).
- The `noreturn` guard is a trap instruction emitted after the body of a `noreturn` function and
  after every call to one (D8.5). It is reachable only when an `extern` declared `noreturn`
  returns anyway; the process dies with SIGILL and nothing is flushed.
- `--no-bounds-check` exists for benchmarking and is unsafe (D10.6).

```fort
i32[] s = new(i32[3]);
i32 a = s[3];                        // runtime error: index out of range
i64 k = -1;
i32 b = s[k];                        // runtime error: index out of range (unsigned compare)
mut u8 c = 255;
c++;                                 // checked: runtime error; release: c == 0
u64 n = s.len - 4;                   // checked: runtime error; release: wraps
mut u32 h = 0;
h = h *% 31 +% 7;                    // wraps in both modes
i32 d = 1 << 32;                     // compile error: 1 is i32 and the constant count is its width
i32 zero = 0;
i32 e = a / zero;                    // runtime error: division by zero
assert(s.len == 3);                  // passes
assert(s.len == 4);                  // main.ft:14:1: assertion failed: s.len == 4
```

## 7. The runtime-error contract

When a check fails, or `panic` or a failed `assert` executes, the runtime (D11.4):

1. flushes every buffered output stream (D11.5);
2. writes exactly one line to standard error;
3. calls `abort()`, so the process dies with SIGABRT, which a shell reports as status 134.

Deferred statements do not run (D7.8) and no destructor-like cleanup exists. The line has one of
three forms:

```
<file>:<line>:<col>: runtime error: <message>
<file>:<line>:<col>: panic: <message>
<file>:<line>:<col>: assertion failed: <expression text>
```

`<file>:<line>:<col>` locates the failing operation, `new`, `assert` or `panic` call in the
source. Test files match these lines with `//! stderr: <substring>` and `//! abort` (D14.5).

```sh
$ fort main.ft && ./a.out
main.ft:7:13: runtime error: index out of range
$ echo $?
134
```

Output buffering (D11.5): `print` and `println` write to a runtime buffer for standard output;
`eprint` and `eprintln` are unbuffered; `fprint` and `fprintln` use one buffer per descriptor.
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
| use after `del`                                | `del(p); p->value = 1;`                    |
| double `del`                                   | `del(p); del(p);`                          |
| `del` of a non-allocation                      | `i32 x = 1; del(&x);` or `del(s[2..]);`    |
| dereferencing `null`                           | `Node* q = null; i32 v = q->value;`        |
| dereferencing a dangling pointer               | `fn i32* f() { i32 x = 1; return &x; }`    |
| writing read-only memory through a cast that added `mut` | `cast("abc", mut u8[])[0] = 'x';` |
| `p[lo..hi]` beyond the object                  | `i32 one = 0; i32[] s = (&one)[0..4];`     |
| calling a null function pointer                | `fn void() f = null; f();`                 |
| data races                                     | two threads from `extern` writing one `g`  |

Returning or storing a slice of a local array is the slice form of the dangling-pointer case
(section 4.3). Reading an object after its `del`, through any alias, is use after `del`.

Explicitly not undefined: there is no strict-aliasing rule. Reading an object through a pointer
to another type of the same size is defined and yields the bit pattern (D10.7):

```fort
f64 x = 1.0;
u64 bits = *cast(&x, u64*);          // 0x3FF0000000000000
```

## 9. Idioms

### 9.1 Ownership transfer

The function that allocates returns the `mut T*` or `mut T[]`; the caller owns it and frees it.
Document the transfer in the function's name or comment; the type system does not track it.

```fort
struct Node {
    i32 value;
    mut Node* next;
}

fn mut Node* make_node(i32 v) {
    mut Node* n = new(Node);         // zeroed: next == null
    n->value = v;
    return n;                        // ownership passes to the caller
}

fn void free_list(Node* mut head) {
    while (head != null) {
        Node* next = head->next;
        del(head);
        head = next;                 // rebinding the local copy only
    }
}
```

### 9.2 Out-parameters and error returns

Functions return `bool` or an error enum and deliver results through `mut T*` out-parameters;
`-1` and `null` sentinels are used where C convention expects them; `panic` is for programming
errors (D13.3). A slice result uses a pointer to a slice, `mut u8[]* out` (D3.6): the callee
stores the header through `*out` and the caller owns the elements.

```fort
enum ParseError { None, Empty, BadDigit, Overflow }

fn ParseError parse_u32(string s, mut u32* out) {
    if (s.len == 0) { return ParseError.Empty; }
    mut u32 acc = 0;
    for (char c : s) {
        if (c < '0' || c > '9') { return ParseError.BadDigit; }
        u32 d = cast(c, u32) - '0';
        if (acc > (4294967295 - d) / 10) { return ParseError.Overflow; }
        acc = acc * 10 + d;
    }
    *out = acc;
    return ParseError.None;
}

fn bool find(i32[] xs, i32 key, mut u64* index) {
    for (mut u64 i = 0; i < xs.len; i++) {
        if (xs[i] == key) { *index = i; return true; }
    }
    return false;
}

fn bool read_all(i32 fd, mut u8[]* out) {
    mut u8[] buf = new(u8[4096]);
    // ... fill buf, returning false after del(buf) on failure ...
    *out = buf;                      // the caller now owns buf
    return true;
}

fn void use_both() {
    mut u32 v = 0;
    switch (parse_u32("42", &v)) {
    case ParseError.None: println(v);
    case ParseError.Empty, ParseError.BadDigit, ParseError.Overflow: println("bad input");
    }
    mut u8[] data = {};
    if (read_all(0, &data)) {        // &data is mut u8[]*: data is a mut u8[] binding
        defer del(data);
        println(data.len);
    }
}
```

### 9.3 A growable buffer without generics

One struct per element type: a backing slice plus a count of the elements in use. The backing
slice is replaced by a larger one when full; `std::vec` provides `IntVec` and `PtrVec` on this
pattern (D13.2).

```fort
struct IntBuf {
    mut i32[] items;                 // backing storage; items.len is what is allocated
    u64 count;                       // elements in use
}

fn void push(mut IntBuf* b, i32 x) {
    if (b->count == b->items.len) {
        mut i32[] bigger = new(i32[b->items.len * 2 + 8]);
        for (mut u64 i = 0; i < b->count; i++) { bigger[i] = b->items[i]; }
        del(b->items);               // no-op the first time: {null, 0}
        b->items = bigger;
    }
    b->items[b->count] = x;
    b->count++;
}

fn i32[] contents(IntBuf* b) {
    return b->items[..b->count];     // a view; do not del it
}

fn void free_buf(mut IntBuf* b) {
    del(b->items);
    mut i32[] none = {};
    b->items = none;
    b->count = 0;
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
    mut u8[] buf = new(u8[65536]);
    defer del(buf);
    // ... copy loop ...
    return true;
}
```

### 9.5 Building a string in a `mut u8[]`

Allocate the bytes, fill them, and `cast` the whole slice to `string` (D3.14). The string aliases
the buffer, so the buffer must outlive it, and it is freed through the slice form. Allocate the
exact length, or keep the original slice for `del`: a prefix such as `buf[..n]` is a sub-slice,
and `del` of a sub-slice is undefined (D10.3).

```fort
fn string repeat(char c, u64 n) {
    mut u8[] buf = new(u8[n]);
    for (mut u64 i = 0; i < n; i++) { buf[i] = cast(c, u8); }
    return cast(buf, string);        // the caller owns the bytes
}

fn void demo() {
    string s = repeat('-', 10);
    println(s);
    del(cast(s, u8[]));              // release through the slice form
}
```

For strings whose length is not known in advance, `std::strbuf` grows a `mut u8[]` with the
pattern of section 9.3 and hands out a `string` view of the bytes written so far (D13.2).

## 10. Not in v1

Arena allocators, leak detection, use-after-free detection, alignment and packed attributes, and
any form of ownership tracking are outside v1; D15 lists the deferred features with the idiom to
use for each. The `extern` boundary reaches every C allocator and every C library in the
meantime.
