# fort standard library

This document specifies the v1 standard library. It implements decisions D13.1 to D13.4 and cites
the language decisions it depends on; `decisions.md` and `grammar.md` win wherever they disagree
with this file. It is written for two readers: the team implementing the library in fort on top of
`extern` declarations and the C runtime, and the author of the self-hosted compiler, which must be
writable with nothing but this library and the builtins (D12).

## 1. Principles

### 1.1 What the library is

- Every module is one fort source file under the standard library directory (D9.1, D14.1):
  `std::io` is `<std>/io.ft`. The library is ordinary fort (D13.1); the compiler knows nothing
  about it beyond resolving the `std` search root.
- Everything at module level is exported (D9.6). Names not documented here (helpers such as
  `strmap.find_slot`) are implementation details and may change; programs must not use them.
- Foreign calls happen only through the `extern` declarations collected in `std::c` (D9.8) and
  the C runtime's `fort_rt_*` entry points (section 3). The runtime is permanent; it is not a
  self-hosting goal (D13.1).
- Struct field lists and their order are part of the contract (layout per D3.8); the sizes and
  offsets stated below may be relied on.

### 1.2 Error idiom (D13.3)

- A function that can fail for a reason outside the program's control (the operating system,
  malformed input) returns `bool`, `true` on success, and delivers its result through a
  `mut T*` out-parameter. On failure `*out` is left as it was unless the entry says otherwise.
  The OS error is available from `sys.errno()`.
- Where C practice is universal the library uses a sentinel instead: `-1` for a descriptor that
  could not be opened and for a position that was not found (`i64`), `null` for a pointer.
- A violated precondition is a programming error and calls `panic` (D12.2) with a message of the
  form `"<module>.<function>: <what>"`. It is never reported through the `bool` result.
- Cleanup uses `defer` (D7.8). Runtime errors do not run deferred code (D11.4), so the library
  never depends on cleanup after a panic.

### 1.3 Ownership

Every function entry below carries an "Ownership" line. The rules behind those lines:

- A function that returns heap memory says so and names the `del` form that releases it. A
  heap string is released as `del(cast(s, u8[]))` (D10.3). `del` frees by `.ptr` (allocations
  have no header, D10.3), so a string whose `.ptr` is the start of an allocation may be released
  this way even though its trailing NUL lies past `.len`.
- A container's `free` releases only the storage the container allocated. Containers never own
  what they point to: `StrMap` keys and `PtrVec` items belong to the caller.
- A function named `view` or `bytes` returns an alias into storage owned by its argument; the
  alias is valid until the next mutating call on that argument.
- Nothing in the library allocates without saying so in its entry.

### 1.4 Talking to C

- Slices, strings and structs never cross an `extern` boundary (D13.4). A call unpacks `.ptr`
  and `.len`: `c.write(fd, cast(buf.ptr, void*), buf.len)`. A fixed array has no `.ptr` (D3.4);
  slice it first: `arr[..].ptr`.
- NUL termination. These strings carry a `0` after their last character: literals (D3.7), the
  elements of `sys.args()` (D8.6), the result of `sys.env`, and the results of `str.dup`,
  `str.concat` and `strbuf.take`. Sub-strings, `strbuf.view` results and file contents do not.
  The one way to hand a string to C is `str.to_cstr`, which always copies; `io.open_read`,
  `io.open_write` and `sys.env` use it internally (D13.4). In the other direction,
  `str.from_cstr` wraps a C string without copying.
- Out-parameters that deliver a slice. Pointer-to-slice is not a v1 type (D3.6, D15), so a
  function that produces a growable `u8[]` fills a `strbuf.StrBuf` through
  `mut strbuf.StrBuf* out`, the struct wrapper D3.6 prescribes. Scalar results use
  `mut i64* out` and similar.
- Buffer parameters of externs are `void*`; reaching it takes a `cast` (D3.11), so the caller
  decides what C may write into. Handing C a pointer into read-only memory is undefined (D10.7).

### 1.5 Naming

Module short names are the last path segment: `sys c mem io str strbuf vec strmap math`.
Functions are `snake_case`; structs are `CamelCase`; module constants are `UPPER_CASE`. When a
module has several struct types, the functions carry the type as a prefix (`vec.ptr_push`,
`vec.int_push`). Import bindings share the module namespace with locals (D7.9), so library
sources never name a parameter `c`, `str`, `mem`, `io`, `sys`, `vec`, `strbuf` or `math`; a
`char` parameter is called `ch`.

### 1.6 Module list

| Module        | Imports                        | Purpose                                      |
|---------------|--------------------------------|----------------------------------------------|
| `std::c`      | none                           | libc and runtime `extern`s, flag constants   |
| `std::mem`    | `c`                            | copy, fill and compare bytes                 |
| `std::str`    | `c`, `mem`                     | compare, search, classify, parse, duplicate  |
| `std::sys`    | `c`, `str`                     | exit, args, errno, env                       |
| `std::strbuf` | `mem`                          | growable text and byte buffer                |
| `std::io`     | `c`, `sys`, `str`, `strbuf`    | descriptors, whole files and streams         |
| `std::vec`    | none                           | `PtrVec`, `IntVec`, the non-generic pattern  |
| `std::strmap` | `str`                          | string-keyed open-addressing table           |
| `std::math`   | none                           | float bit casts, abs, min, max, limits       |

The import graph is acyclic (D9.5). A program imports what it uses: `import std::io;` and then
`io.read_file(...)` (D9.3, D9.4).

## 2. Modules

Each module section lists its public struct definitions and constants exactly as declared,
then a block of function headers with the bodies omitted, then one entry per function with
semantics, preconditions, runtime errors and ownership, then a short example.

### 2.1 `std::sys`

Process-level services.

```fort
fn noreturn exit(i32 code)
fn string[] args()
fn i32 errno()
fn bool env(string name, mut string* out)
```

- `exit`: flushes every runtime output buffer (D11.5) and terminates the process with status
  `code & 0xFF` (D11.6). Deferred statements of the calling function do not run. Implemented as
  `c.fort_rt_flush_all(); c.exit(code);`. Ownership: none.
- `args`: returns the same `string[]` that `main` received (D8.6, D11.6): `args()[0]` is the
  program name and every element is NUL-terminated. Implemented as
  `cast(c.fort_rt_args_ptr(), string*)[0..c.fort_rt_args_len()]` (unchecked pointer slicing,
  D6.9). Ownership: the runtime owns the storage; never `del` it.
- `errno`: the value of C `errno` for the calling thread, read through
  `c.__errno_location()`. It is meaningful only after a library call has reported failure.
  Ownership: none.
- `env`: looks up `name` in the process environment. On success `*out` aliases the environment
  string (NUL-terminated, valid for the life of the process) and the result is `true`; when the
  variable is absent `*out` is unchanged and the result is `false`. `name` is copied into a
  NUL-terminated buffer for the call. Ownership: `*out` is not heap memory; never `del` it.

```fort
import std::sys;

fn string std_dir() {
    mut string dir = "../std";
    if (!sys.env("FORT_STD_DIR", &dir)) {
        eprintln("FORT_STD_DIR not set, using ", dir);
    }
    return dir;
}
```

### 2.2 `std::c`

Thin `extern` declarations for the libc calls the other modules need, plus the runtime entry
points of section 3, with the C types mapped per D9.8: `int` is `i32`, `size_t` is `u64`,
`ssize_t` and `off_t` are `i64`, `mode_t` is `u32`, `char*` is `char*`, and every `void*`
buffer is `void*`. Names are unmangled (D9.7). `open` is variadic in C; the fixed prototype is
safe because the compiler zeroes `al` before every extern call (D9.8).

```fort
// open(2) flags, Linux x86-64 values.
i32 O_RDONLY = 0;
i32 O_WRONLY = 1;
i32 O_RDWR = 2;
i32 O_CREAT = 0o100;
i32 O_TRUNC = 0o1000;
i32 O_APPEND = 0o2000;

// lseek(2) whence values.
i32 SEEK_SET = 0;
i32 SEEK_CUR = 1;
i32 SEEK_END = 2;

// errno values the library tests for.
i32 ENOENT = 2;
i32 EINTR = 4;
i32 EACCES = 13;

// <stdlib.h>, <string.h>
extern fn void* malloc(u64 size);
extern fn void free(void* p);
extern fn void* memcpy(void* dst, void* src, u64 n);
extern fn void* memmove(void* dst, void* src, u64 n);
extern fn i32 memcmp(void* a, void* b, u64 n);
extern fn void* memset(void* dst, i32 v, u64 n);
extern fn u64 strlen(char* s);
extern fn char* getenv(char* name);
extern fn noreturn exit(i32 code);
extern fn noreturn abort();

// <fcntl.h>, <unistd.h>
extern fn i32 open(char* path, i32 flags, u32 mode);
extern fn i64 read(i32 fd, void* buf, u64 n);
extern fn i64 write(i32 fd, void* buf, u64 n);
extern fn i32 close(i32 fd);
extern fn i64 lseek(i32 fd, i64 offset, i32 whence);

// <errno.h>: errno is a macro over this accessor in glibc and musl.
extern fn i32* __errno_location();

// fort runtime, section 3.
extern fn void* fort_rt_args_ptr();
extern fn u64 fort_rt_args_len();
extern fn void fort_rt_flush(i32 fd);
extern fn void fort_rt_flush_all();
```

Semantics are those of the C functions. `malloc` and `free` are interchangeable with `new` and
`del` (D10.3) and exist for code that sizes an allocation in bytes; a `void*` from `malloc`
becomes usable through `cast(p, mut u8*)[0..n]`. `c.exit` does not flush the runtime's output
buffers; programs call `sys.exit`. `c.abort` is what the runtime calls after a runtime error
(D11.4). Ownership: as in C; the library wraps every ownership-bearing call below. Direct use
looks like `c.write(fd, cast(s.ptr, void*), s.len) == cast(s.len, i64)`, which writes a string
to a descriptor, bypassing the runtime's buffers.

### 2.3 `std::mem`

Byte-slice primitives over `memmove`, `memset` and `memcmp`.

```fort
fn void copy(mut u8[] dst, u8[] src)
fn void fill(mut u8[] dst, u8 v)
fn bool equal(u8[] a, u8[] b)
```

- `copy`: copies `src.len` bytes to the start of `dst`; the ranges may overlap (`memmove`
  semantics). Panics with `"mem.copy: destination too short"` when `dst.len < src.len`. An empty
  `src` is a no-op and makes no C call. Ownership: none.
- `fill`: sets every byte of `dst` to `v`. Ownership: none.
- `equal`: `true` when the lengths are equal and the bytes match; two empty slices are equal
  whatever their pointers. Ownership: none.

Only `u8` slices are covered: a slice cast never changes the element size (D3.14), so other
element types are copied with a loop, or through `c.memmove` on `.ptr` with a byte count of
`n * sizeof(T)` (the pattern `std::vec` uses).

```fort
import std::mem;

fn void demo() {
    mut u8[16] key = {};
    mem.copy(key[..], cast("abc", u8[]));   // key[0..3] = "abc", rest zero
    mem.fill(key[3..], cast('.', u8));
}
```

### 2.4 `std::io`

File descriptors, whole-file and stream I/O. Everything here goes straight to the descriptor;
the print family (D12.2) writes through the runtime's per-descriptor buffers (D11.5), so a
program that mixes `print` with `io.write_all` on the same descriptor calls `io.flush` between
them.

```fort
i32 STDIN = 0;
i32 STDOUT = 1;
i32 STDERR = 2;

fn i32 open_read(string path)
fn i32 open_write(string path)
fn bool close(i32 fd)
fn void flush(i32 fd)
fn i64 read(i32 fd, mut u8[] buf)
fn bool write_all(i32 fd, u8[] buf)
fn bool read_all(i32 fd, mut strbuf.StrBuf* out)
fn bool read_file(string path, mut strbuf.StrBuf* out)
fn bool write_file(string path, u8[] data)
```

- `open_read`: opens `path` with `O_RDONLY`. Returns the descriptor, or `-1` with the reason in
  `sys.errno()`. `path` is copied into a NUL-terminated buffer for the call (D13.4); a path
  containing `\0` names its prefix. Ownership: the caller releases the descriptor with `close`.
- `open_write`: opens `path` with `O_WRONLY | O_CREAT | O_TRUNC` and mode `0o644`, creating or
  truncating the file. Same result and ownership as `open_read`.
- `close`: flushes the runtime's buffer for `fd` if one exists (D11.5, via `fort_rt_flush`),
  then calls `close(2)`. Returns `true` when `close(2)` returned 0. The descriptor is invalid
  afterwards in either case. Ownership: consumes `fd`.
- `flush`: flushes the runtime's buffer for `fd` if one exists and does nothing otherwise.
  Ownership: none.
- `read`: one `read(2)` into `buf`. Returns the number of bytes read (at most `buf.len`), `0` at
  end of file, or `-1` with `sys.errno()` set. `EINTR` is retried inside the call. When
  `buf.len == 0` the result is `0` and no system call is made. Ownership: none.
- `write_all`: writes every byte of `buf`, looping over partial writes and retrying `EINTR`.
  Returns `false` on the first error; some bytes may have been written. Ownership: none.
- `read_all`: reads `fd` to end of file, adding the bytes to `out` after its current contents,
  in chunks of at least 4096 bytes placed directly in the buffer's spare room. Returns `false` on a
  read error with `out->len` restored to its value at entry (the storage may have grown).
  Ownership: bytes land in storage owned by `*out`.
- `read_file`: `open_read`, `read_all`, `close`. Returns `false` when any step fails, with
  `out->len` restored and `sys.errno()` describing the failing call (`close(2)` leaves `errno`
  alone when it succeeds). On success the contents are `strbuf.bytes(out)` and, as text,
  `strbuf.view(out)`. Ownership: as `read_all`.
- `write_file`: `open_write`, `write_all`, `close`. Returns `true` only when all three succeed.
  Ownership: none.

```fort
import std::io;
import std::strbuf;
import std::sys;

// cat: copies one file to standard output.
fn i32 main(string[] args) {
    if (args.len != 2) {
        return 2;
    }
    mut strbuf.StrBuf buf = strbuf.create();
    defer strbuf.free(&buf);
    if (!io.read_file(args[1], &buf)) {
        eprintln("cat: ", args[1], ": errno ", sys.errno());
        return 1;
    }
    return io.write_all(io.STDOUT, strbuf.bytes(&buf)) ? 0 : 1;
}
```

### 2.5 `std::str`

Operations on `string` (D3.7). None of them allocates unless the entry says so.

```fort
fn bool equal(string a, string b)
fn i32 cmp(string a, string b)
fn u64 hash(string s)
fn bool starts_with(string s, string prefix)
fn bool ends_with(string s, string suffix)
fn i64 index_of(string s, char ch)
fn i64 last_index_of(string s, char ch)
fn i64 find(string s, string needle)
fn string dup(string s)
fn string concat(string a, string b)
fn char* to_cstr(string s)
fn string from_cstr(char* p)
fn bool parse_i64(string s, mut i64* out)
fn bool parse_u64(string s, u64 base, mut u64* out)
fn i64 digit_value(char ch)
fn bool is_digit(char ch)
fn bool is_alpha(char ch)
fn bool is_alnum(char ch)
fn bool is_space(char ch)
fn bool is_hex(char ch)
```

- `equal`: the same as `a == b` (D3.7); it exists so that equality can be a function value,
  `fn bool(string, string) eq = str.equal;`.
- `cmp`: lexicographic order over the bytes taken as `u8` values, a proper prefix sorting
  first; returns `-1`, `0` or `1`. This is independent of how `char` itself orders.
- `hash`: 64-bit FNV-1a over the bytes: start from `0xcbf29ce484222325`, and for each byte
  `h ^= cast(ch, u64); h *%= 0x100000001b3;`. The wrapping multiply (D11.2) makes the value
  identical in both build modes; the empty string hashes to the offset basis. The function is
  fixed by this specification because `strmap` and on-disk formats may depend on it.
- `starts_with`, `ends_with`: `prefix.len <= s.len && s[..prefix.len] == prefix`, and the
  mirror image for suffixes. An empty prefix or suffix always matches.
- `index_of`, `last_index_of`: position of the first or last `ch` in `s` as `i64`, or `-1`.
- `find`: position of the first occurrence of `needle` in `s`, or `-1`. An empty needle is found
  at `0`. Simple quadratic search; adequate for source-sized inputs.
- `dup`: heap copy of `s` with a trailing NUL; `result.len == s.len`. Allocates `s.len + 1`
  bytes. Ownership: the caller releases it with `del(cast(result, u8[]))`.
- `concat`: heap copy of `a` followed by `b`, NUL-terminated. Allocates `a.len + b.len + 1`
  bytes. Ownership: as `dup`.
- `to_cstr`: the one sanctioned way to hand a string to C: a NUL-terminated heap copy, returned
  as `char*` (it is `dup(s).ptr`). If `s` contains `\0`, C sees the prefix. Ownership: the
  caller releases it with `del(p)`.
- `from_cstr`: wraps the C string at `p` as `p[0..c.strlen(p)]` cast to `string`; no copy, so
  the result aliases `p` and stays valid as long as `p` does. `from_cstr(null)` is the zero
  string. Ownership: whatever owns `p`.
- `parse_i64`: decimal with an optional leading `-`, at least one digit and nothing else: no
  `+`, no whitespace, no `_`. Returns `false` on a syntax error or when the value does not fit
  `i64`; `-9223372036854775808` is accepted. Overflow is detected arithmetically, without
  relying on either build mode's behavior (D11.1).
- `parse_u64`: digits of `base` (2 to 36) using `0-9`, `a-z` and `A-Z`, no sign, no `0x`
  prefix, no `_` (stripping prefixes and separators is the lexer's job). Returns `false` when
  `s` is empty, `base` is out of range, a character is not a digit below `base`, or the value
  overflows `u64`.
- `digit_value`: the value of `ch` as a digit in bases up to 36 (`'7'` is 7, `'b'` and `'B'`
  are 11), or `-1`.
- `is_digit` `0-9`; `is_alpha` `A-Z` and `a-z` (not `_`); `is_alnum` either; `is_space` space,
  `\t`, `\n`, `\x0b`, `\x0c`, `\r`; `is_hex` `0-9`, `a-f`, `A-F`. ASCII only: every byte at or
  above `0x80` classifies as `false`.

```fort
import std::str;

// Splits "name=123"; false on malformed input, in which case *name and *value are unchanged.
fn bool parse_binding(string line, mut string* name, mut i64* value) {
    i64 eq = str.index_of(line, '=');
    if (eq < 0) {
        return false;
    }
    u64 at = cast(eq, u64);
    if (!str.parse_i64(line[at + 1..], value)) {
        return false;
    }
    *name = line[..at];
    return true;
}
```

### 2.6 `std::strbuf`

A growable buffer for building text and byte sequences.

```fort
struct StrBuf {
    mut u8[] data;   // storage; data.len is the cap
    u64 len;         // bytes in use; len <= data.len
}
```

`StrBuf` is 24 bytes: `data` at offset 0, `len` at offset 16. The zero value `StrBuf{}` is a
valid empty buffer with no storage, so a struct containing a `StrBuf` may be initialized with
`{}`. The bytes `data[len..]` are spare room whose contents are unspecified.

```fort
fn StrBuf create()
fn StrBuf with_cap(u64 cap)
fn void free(mut StrBuf* b)
fn void reserve(mut StrBuf* b, u64 extra)
fn void push(mut StrBuf* b, char ch)
fn void push_byte(mut StrBuf* b, u8 v)
fn void append(mut StrBuf* b, string s)
fn void append_bytes(mut StrBuf* b, u8[] src)
fn void append_i64(mut StrBuf* b, i64 v)
fn void append_u64(mut StrBuf* b, u64 v)
fn void clear(mut StrBuf* b)
fn void truncate(mut StrBuf* b, u64 len)
fn string view(StrBuf* b)
fn u8[] bytes(StrBuf* b)
fn string take(mut StrBuf* b)
```

Growth policy: when an operation needs `len + extra > data.len`, the new cap is the largest of
16, `2 * data.len` and `len + extra`; the contents move to fresh `new` storage and the old
storage is `del`ed, so every earlier `view` or `bytes` alias dies at that moment (1.3).

- `create`: `StrBuf{}`; allocates nothing. `with_cap`: allocates `cap` bytes of storage up
  front (`0` is the same as `create`). Ownership: the caller calls `free`.
- `free`: `del`s the storage and resets `*b` to `StrBuf{}`; calling it twice, or on `StrBuf{}`,
  is harmless (D10.3).
- `reserve`: ensures `data.len >= len + extra`, growing per the policy. Ownership: none.
- `push`, `push_byte`: append one `char` or one `u8`.
- `append`, `append_bytes`: append the bytes of `s` or `src`. Precondition: `src` must not
  alias `b`'s own storage, because growth frees that storage before copying (not detected).
- `append_i64`, `append_u64`: append the decimal text of `v`, `-` first for negatives, with the
  same digits `print` would produce (D11.7). The `i64` minimum is handled through its `u64`
  magnitude with `-%`, so both build modes agree.
- `clear`: sets `len` to 0 and keeps the storage. `truncate`: sets `len` to the given value;
  panics with `"strbuf.truncate: beyond len"` when it exceeds `b->len`.
- `view`: `cast(b->data[..b->len], string)`, an alias, not NUL-terminated. `bytes`: the same
  span as `u8[]`. Ownership: aliases, valid until the next mutating call on `*b`.
- `take`: an exact-size NUL-terminated heap copy of the contents (`result.len == b->len`); then
  sets `b->len` to 0 and keeps the storage for reuse. Allocates `b->len + 1` bytes. Ownership:
  the caller releases the result with `del(cast(result, u8[]))`.

```fort
import std::strbuf;

// Formats "    mov <reg>, <imm>\n"; the caller releases the result with del(cast(s, u8[])).
fn string mov_imm(string reg, i64 imm) {
    mut strbuf.StrBuf b = strbuf.with_cap(32);
    defer strbuf.free(&b);
    strbuf.append(&b, "    mov ");
    strbuf.append(&b, reg);
    strbuf.append(&b, ", ");
    strbuf.append_i64(&b, imm);
    strbuf.push(&b, '\n');
    return strbuf.take(&b);
}
```

### 2.7 `std::vec`

Growable sequences of pointers and of `i64`, and the pattern for every other element type.

```fort
struct PtrVec {
    mut void*[] items;   // slots; items.len is the cap
    u64 len;             // slots in use; len <= items.len
}

struct IntVec {
    mut i64[] items;
    u64 len;
}
```

Both structs are 24 bytes with `items` at offset 0 and `len` at offset 16; `{}` is a valid
empty vector. The live elements are `v.items[..v.len]`. Indexing `v.items[i]` with
`v.len <= i < v.items.len` is not a runtime error; it reads a zero or stale slot, so code that
wants a bounds check indexes the live slice.

```fort
fn PtrVec ptr_create()
fn PtrVec ptr_with_cap(u64 cap)
fn void ptr_free(mut PtrVec* v)
fn void ptr_reserve(mut PtrVec* v, u64 extra)
fn void ptr_push(mut PtrVec* v, void* p)
fn void* ptr_pop(mut PtrVec* v)

fn IntVec int_create()
fn IntVec int_with_cap(u64 cap)
fn void int_free(mut IntVec* v)
fn void int_reserve(mut IntVec* v, u64 extra)
fn void int_push(mut IntVec* v, i64 x)
fn i64 int_pop(mut IntVec* v)
```

- `*_create`, `*_with_cap`, `*_free`, `*_reserve`: exactly as their `strbuf` counterparts,
  counting elements instead of bytes and using the same growth policy (16, then doubling). The
  copy on growth is an element loop, which is what makes the file usable as a template.
  Ownership: `*_free` releases the slots only, never the pointees of a `PtrVec`.
- `*_push`: appends one element, growing as needed. `*_pop`: removes and returns the last
  element; panics with `"vec.ptr_pop: empty"` or `"vec.int_pop: empty"` on an empty vector.

This module is the non-generic container pattern (D15). To hold `Token` values, copy `IntVec`
and its six functions, replace `i64` with `Token` and the prefix `int_` with `token_`: about
forty lines, type-checked like any other code. `PtrVec` is for elements that must not be
copied: store `cast(p, void*)` and cast back on retrieval; a pointer cast may add mutability
(D3.14), so a `mut Node*` survives the round trip.

```fort
import std::vec;

struct Node { i64 value; }

fn i64 demo() {
    mut vec.PtrVec nodes = vec.ptr_create();
    defer vec.ptr_free(&nodes);
    mut Node* n = new(Node);
    n->value = 7;
    vec.ptr_push(&nodes, cast(n, void*));
    mut Node* back = cast(vec.ptr_pop(&nodes), mut Node*);
    i64 v = back->value;
    del(back);
    return v;
}
```

### 2.8 `std::strmap`

A hash table from `string` to `i64` with open addressing, linear probing, tombstones and
`str.hash` (FNV-1a).

```fort
u8 SLOT_EMPTY = 0;
u8 SLOT_FULL = 1;
u8 SLOT_DEAD = 2;

struct StrMapEntry {
    string key;
    i64 val;
    u64 hash;
    u8 state;    // SLOT_EMPTY, SLOT_FULL or SLOT_DEAD
}

struct StrMap {
    mut StrMapEntry[] entries;   // entries.len is the cap: 0 or a power of two
    u64 live;                    // entries in state SLOT_FULL
    u64 used;                    // live plus tombstones
}
```

`StrMapEntry` is 40 bytes (`key` 0, `val` 16, `hash` 24, `state` 32, then padding); `StrMap` is
32 bytes. `StrMap{}` is a valid empty map.

Algorithm: a key probes from `hash & (cap - 1)` upward, wrapping, comparing the stored `hash`
and then the key with `==`; a lookup stops at the first `SLOT_EMPTY`. An insert of an absent
key reuses the first `SLOT_DEAD` slot met on its probe, else the `SLOT_EMPTY` that ended it.
Before an insert, if `cap == 0` or `(used + 1) * 4 > cap * 3`, the table is rebuilt: the new
cap starts at 16 and doubles until `(live + 1) * 2 <= cap`, live entries are reinserted and
tombstones disappear. `remove` marks the slot `SLOT_DEAD` and decrements `live`; `used` is
unchanged until the next rebuild. Iteration is a walk over `entries` taking the slots whose
`state == SLOT_FULL`, in table order.

```fort
fn StrMap create()
fn void free(mut StrMap* m)
fn bool put(mut StrMap* m, string key, i64 val)
fn bool get(StrMap* m, string key, mut i64* out)
fn bool has(StrMap* m, string key)
fn bool remove(mut StrMap* m, string key)
fn u64 count(StrMap* m)
```

- `create`: `StrMap{}`; allocates nothing. `free`: `del`s the entry storage and resets `*m` to
  `StrMap{}`; keys are untouched. Ownership: the caller calls `free`.
- `put`: inserts `key` with `val`, or replaces the value of an existing key. Returns `true` when
  the key was new. Keys are not copied: the caller keeps the key's bytes alive and unchanged
  for as long as the entry exists (a literal, a sub-string of a source buffer that outlives the
  map, or `str.dup`). Allocates on rebuild. Ownership: the map never owns a key.
- `get`: `true` and `*out = val` when present; `false` with `*out` unchanged otherwise. `has`:
  the presence test alone. `remove`: `true` when the key was present. `count`: `live`.

To map names to pointers, keep a `vec.PtrVec` beside the map and store the index:

```fort
import std::strmap;
import std::vec;

struct Sym { string name; i64 offset; }

struct SymTab {
    strmap.StrMap index;   // name to position in syms
    vec.PtrVec syms;       // Sym* in declaration order
}

fn bool declare(mut SymTab* t, mut Sym* s) {
    if (strmap.has(&t->index, s->name)) {
        return false;
    }
    strmap.put(&t->index, s->name, cast(t->syms.len, i64));
    vec.ptr_push(&t->syms, cast(s, void*));
    return true;
}

fn Sym* lookup(SymTab* t, string name) {
    mut i64 i = 0;
    if (!strmap.get(&t->index, name, &i)) {
        return null;
    }
    return cast(t->syms.items[cast(i, u64)], Sym*);
}
```

### 2.9 `std::math`

Integer limits, float bit casts and per-type `abs`, `min`, `max`. There is no overloading
(D8.3), so each function names its type.

```fort
i32 I32_MIN = -2147483648;
i32 I32_MAX = 2147483647;
i64 I64_MIN = -9223372036854775808;
i64 I64_MAX = 9223372036854775807;
u32 U32_MAX = 0xFFFFFFFF;
u64 U64_MAX = 0xFFFFFFFFFFFFFFFF;

fn u64 f64_bits(f64 x)
fn f64 f64_from_bits(u64 bits)
fn u32 f32_bits(f32 x)
fn f32 f32_from_bits(u32 bits)
fn f64 f64_inf()
fn f64 f64_nan()
fn bool is_nan(f64 x)
fn i32 abs_i32(i32 x)
fn i64 abs_i64(i64 x)
fn i32 min_i32(i32 a, i32 b)
fn i32 max_i32(i32 a, i32 b)
fn i64 min_i64(i64 a, i64 b)
fn i64 max_i64(i64 a, i64 b)
fn u64 min_u64(u64 a, u64 b)
fn u64 max_u64(u64 a, u64 b)
fn f64 min_f64(f64 a, f64 b)
fn f64 max_f64(f64 a, f64 b)
```

- The `*_MIN`/`*_MAX` constants are module-level constants (D7.10), usable in `case` labels and
  array lengths.
- `f64_bits` and friends reinterpret the bytes through the pointer-cast idiom,
  `*cast(&x, u64*)`, which is defined behavior: a type-punning load is not in the D10.7 list of
  undefined behavior. `f64_inf()` is `f64_from_bits(0x7FF0000000000000)` and `f64_nan()` is
  `f64_from_bits(0x7FF8000000000000)`; they are functions because a call is not a constant
  expression (D4.6) and no float literal denotes infinity (D4.4). `is_nan` is `x != x` (D6.12).
- `abs_i32`, `abs_i64`: `x < 0 ? -x : x`. For `I32_MIN` and `I64_MIN` the negation traps in
  checked mode and yields the minimum again in release mode (D11.1); callers that need a total
  function test for the minimum first.
- `min_*`, `max_*`: `a < b ? a : b` and `a > b ? a : b`. For floats, if either operand is NaN
  the comparison is false and `b` is returned; do not rely on NaN propagation.

```fort
import std::math;

fn bool is_negative_zero(f64 x) {
    return math.f64_bits(x) == 0x8000000000000000;
}
```

## 3. The runtime surface the library relies on

`toolchain.md` owns the C runtime: the full `fort_rt_*` list, the C prototypes, the print
buffers and process start. This section names only what the library calls. The library uses
the builtins `new`, `del`, `panic`, `assert` and the print family as any program does (D12);
the runtime calls behind them are emitted by the compiler and never named in library source.
Beyond that, `std::c` declares four runtime entry points:

```fort
extern fn void* fort_rt_args_ptr();
extern fn u64 fort_rt_args_len();
extern fn void fort_rt_flush(i32 fd);
extern fn void fort_rt_flush_all();
```

- `fort_rt_args_ptr`, `fort_rt_args_len`: the element pointer and length of the `string[]` the
  runtime built from `argv` at process start (D11.6). They describe the same storage `main`
  receives, so `sys.args()` and `main`'s parameter are equal slice for slice.
- `fort_rt_flush`: writes out the runtime's buffer for one descriptor, if it has one, and is a
  no-op otherwise. `io.close` and `io.flush` call it, which is how "buffers flush on `io.close`"
  in D11.5 happens.
- `fort_rt_flush_all`: writes out every runtime buffer; `sys.exit` calls it before `c.exit`.

Two properties of the runtime the library also depends on: `del` frees by the pointer alone,
with no header and no length check (D10.3, used by 1.3), and `new(T[n])` returns zeroed
storage (D10.2, which is why fresh `StrBuf` and `vec` slots read as zero).

## 4. Worked example

`wc.ft` reads the file named on the command line and prints its line and word counts, using
only this library and the builtins. Every line is v1 fort.

```fort
import std::io;
import std::str;
import std::strbuf;
import std::sys;

fn i32 main(string[] args) {
    if (args.len != 2) {
        eprintln("usage: ", args[0], " <file>");
        return 2;
    }
    mut strbuf.StrBuf buf = strbuf.create();
    defer strbuf.free(&buf);
    if (!io.read_file(args[1], &buf)) {
        eprintln("wc: cannot read ", args[1], " (errno ", sys.errno(), ")");
        return 1;
    }
    string text = strbuf.view(&buf);
    mut u64 lines = 0;
    mut u64 words = 0;
    mut bool in_word = false;
    for (char ch : text) {
        if (ch == '\n') {
            lines++;
        }
        if (str.is_space(ch)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            words++;
        }
    }
    if (text.len > 0 && text[text.len - 1] != '\n') {
        lines++;
    }
    println(lines, " ", words);
    return 0;
}
```

Notes: `buf` must be `mut` so that `&buf` has type `mut strbuf.StrBuf*` (D5.8); `defer` frees
it on both later `return` paths (D7.8); the counters are `u64` because `.len` is (D16); and the
`text.len > 0` guard keeps `text.len - 1` from trapping on an empty file (D11.1).

## 5. Not in v1

Deliberately absent, with the idiom to use instead; the language-level list is D15.

- Formatted output beyond the print family: build text in a `StrBuf` with `append`,
  `append_i64` and `append_u64`, then `io.write_all` or `print` the result.
- Generic containers: copy `std::vec` for each element type (2.7); use `StrMap` with indices
  for pointer values (2.8).
- Unicode: strings are bytes (D3.7); the classification functions are ASCII-only.
- Floating-point formatting and parsing beyond what the print family emits (D11.7): a program
  that needs a float from text writes its own conversion or calls C through `std::c`.
- Threads, signals, networking, directories, time, line-at-a-time input: call libc through your
  own `extern` declarations following the `std::c` conventions, or `read_all` and scan.
