# fort standard library

This document specifies the v1 standard library. It implements decisions D13.1 to D13.5 and cites
the language decisions it depends on, including the ownership rules of D17 wherever a signature
carries `own`; `decisions.md` and `grammar.md` win wherever they disagree with this file. It is
written for two readers: the team implementing the library in fort on top of `extern`
declarations, the runtime included, and the author of the self-hosted compiler, which must be
writable with nothing but this library and the builtins (D12).

## 1. Principles

### 1.1 What the library is

- Every module is one fort source file under the standard library directory (D9.1, D14.1):
  `std.io` is `<std>/io.ft`. The library is ordinary fort (D13.1); the compiler knows nothing
  about it beyond resolving the `std` search root, `std.rt` excepted, whose entry points a
  builtin lowers to and which is in every import closure (D12.2, D9.10, 2.10).
- Everything at module level is exported (D9.6). Names not documented here (helpers such as
  `strmap.find_slot`) are implementation details and may change; programs must not use them.
- Foreign calls go through the `extern` declarations collected in `std.libc` (D9.8); a program
  may redeclare any of those C symbols with an identical signature (D9.8). The runtime is one of
  the library's own modules, `std.rt` (D13.1, section 3), and calls libc through `std.libc` like
  every other module.
- Struct field lists and their order are part of the contract (layout per D3.8); the sizes and
  offsets stated below may be relied on. `own` is erased at run time (D17.1), so an `own` field
  has the size and offset of the same field without it.

### 1.2 Error idiom (D13.3)

- A function that can fail for a reason outside the program's control (the operating system,
  malformed input) returns `bool`, `true` on success, and delivers its result through a
  `T mut*` out-parameter, or through a pointer to an `own` slot when the result is owned (1.3).
  On failure `*out` is left as it was unless the entry says otherwise. The OS error is
  available from `sys.errno()`.
- Where C practice is universal the library uses a sentinel instead: `-1` for a descriptor that
  could not be opened and for a position that was not found (`i64`), `null` for a pointer.
- A violated precondition is a programming error and calls `panic` (D12.2) with a message of the
  form `"<module>.<function>: <what>"`. It is never reported through the `bool` result.
- Cleanup uses `defer` (D7.8). Runtime errors do not run deferred code (D11.4), so the library
  never depends on cleanup after a panic.

### 1.3 Ownership (D13.5, D17)

Ownership is part of the types (D17): every entry below carries an "Ownership" line that names
the `own` values crossing the call, and the compiler enforces what the line says. The rules the
library follows:

- **Allocating functions return `own`** (D13.5, D17.3). `str.dup`, `str.concat` and
  `strbuf.take` return `string own`; `str.to_cstr` returns `char mut@ own`. The caller binds
  the result to an `own` variable, passes it to an `own` parameter or `del`s it; using it where
  a plain `string` is expected is the "owning temporary would leak" error (D17.8). `del` of a
  `string own` frees it (D17.12); the earlier `del(cast(s, u8@))` is gone.
- **Containers own their storage, not their contents.** `str_buf`, `ptr_vec`, `int_vec` and
  `str_map` keep their storage in an `own` field, which makes each an owning aggregate (D17.7):
  it is passed by pointer (`str_buf mut*`), copying one from a variable into another needs
  `move`, and `del` of a whole container is an error. Each has a `free` that `del`s the storage
  and leaves the zero value; `free` of the zero value is a no-op (D17.9), so
  `defer free(&c);` right after the declaration is always safe. `ptr_vec` items and `str_map`
  keys are borrowed; freeing the container never touches them.
- **Out-parameters that receive ownership are pointers to `own` slots**, `u8 mut@ own mut* out`
  (D13.5, D17.2). The caller initializes the slot to `{}` or `null` and passes its address; the
  callee stores an `own` rvalue into `*out` and never frees what was there, so a live slot is
  the `overwriting owned value` runtime error in checked builds and a leak in release builds
  (D17.11). A caller that reuses a slot `del`s it first.
- **Accessors return views** (D17.3). Functions named `view` or `bytes`, `sys.args`, `sys.env`,
  `str.from_cstr` and every sub-span yield values without `own`; `del` on them does not
  compile (D17.9). A view into a container is valid until the next mutating call on that
  container; a view of an owned value is valid until the value is `del`ed or moved away, which
  nothing checks (D10.7, D17.14).
- **The `defer` idiom.** Declare the owning variable empty, defer its release on the next
  line, then fill it:

  ```fort
  u8 mut@ own mut data = {};
  defer del(data);
  if (!io.read_file_bytes(path, &data)) {
      return false;   // del of a zero span is a no-op (D17.9)
  }
  ```

  A later `return data;` is an implicit `move` (D17.5) that empties `data` before the deferred
  `del` runs (D7.8), so the allocation reaches the caller and every other path frees it. The
  same shape with `strbuf.free(&b);` covers containers.
- **`del` before storing.** Code that replaces owned storage writes `del(old)` before the store,
  `del(b->data); b->data = move(bigger);`, because storing over a live `own` lvalue is a runtime
  error in checked builds (D17.11).
- Nothing in the library allocates without saying so in its entry, and the library stays clear
  of what D17.14 leaves untracked: it never makes two `own` copies of one allocation through
  `cast`, and it applies a cast between `string` and the byte spans (D3.14) only to a view
  (`buf[..]`) or to the `own` rvalue that `move` yields (`cast(move(buf), string own)`, D17.12).

### 1.4 Talking to C

- Spans, strings and structs never cross an `extern` boundary (D13.4). A call unpacks `.ptr`
  and `.len`: `libc.write(fd, cast(buf.ptr, void*), buf.len)`. A fixed array has no `.ptr`
  (D3.4); span it first: `arr[..].ptr`. `.ptr` is a view (D17.3), so C never receives
  ownership this way; `own` in an `extern` signature is erased and documents C's convention
  (D17.13), as `libc.malloc` and `libc.free` show.
- NUL termination. These strings carry a `0` after their last character: literals (D3.7), the
  elements of `sys.args()` (D8.6), the result of `sys.env`, and the C string at the `.ptr` of a
  `str.to_cstr` result. Owned strings from `str.dup`, `str.concat` and `strbuf.take` do not: a
  `string own` occupies exactly `len` bytes, so that `cast(move(buf), string own)` (D17.12)
  turns an exact-size buffer into one without a second allocation. Sub-strings, `strbuf.view`
  results and file contents are not NUL-terminated either. The one way to hand a string to C
  is `str.to_cstr`, which always copies; `io.open_read`, `io.open_write` and `sys.env` use it
  internally (D13.4). In the other direction, `str.from_cstr` wraps a C string without copying.
- Out-parameters. Scalar results use `i64 mut* out` and similar. A function that produces a
  whole file or stream fills a `strbuf.str_buf` through `strbuf.str_buf mut* out`, so that the
  caller can keep reading into the same storage and release it once; `io.read_file_bytes`
  delivers a plain heap span through `u8 mut@ own mut* out` (level 0 is `out`, level 1 the `own`
  slot the callee fills, level 2 the bytes, D5.2, D17.2) for callers that want one allocation
  and one `del`.
- Buffer parameters of externs are `void*`; reaching it takes a `cast` (D3.11), so the caller
  decides what C may write into. Handing C a pointer into read-only memory is undefined (D10.7).

### 1.5 Naming

Module short names are the last path segment: `sys libc mem io str strbuf vec strmap math`.
Functions, struct and enum types, enum members, fields and variables are `lower_case` with
underscores (`str_buf`, `ptr_vec`, `str_map_entry`); module constants are `UPPER_CASE` (D1.4).
When a module has several struct types, the functions carry the type as a prefix
(`vec.ptr_push`, `vec.int_push`). A variable never takes its type's name (`str_buf b`, never
`str_buf str_buf`; D1.4). A `char` parameter is called `ch`, and library sources do not reuse an
import binding's name for a local even though D7.9 permits it.

### 1.6 Module list

| Module          | Imports                                     | Purpose                          |
|-----------------|---------------------------------------------|----------------------------------|
| `std.libc`      | none                                        | libc `extern`s, flags, errno     |
| `std.rt`        | `libc`                                      | the runtime itself (section 3)   |
| `std.rt_float`  | `libc`                                      | the float printers (D18.1)       |
| `std.mem`       | `libc`                                      | copy, fill and compare bytes     |
| `std.str`       | `libc`, `mem`                               | compare, search, classify, parse |
| `std.sys`       | `libc`, `rt`, `str`                         | exit, args, errno, env           |
| `std.strbuf`    | `mem`                                       | growable text and byte buffer    |
| `std.io`        | `libc`, `rt`, `mem`, `sys`, `str`, `strbuf` | descriptors, whole files, streams|
| `std.vec`       | none                                        | `ptr_vec`, `int_vec`, the pattern|
| `std.strmap`    | `str`                                       | string-keyed open-addressing map |
| `std.math`      | none                                        | float bit casts, abs, min, max   |

The import graph is acyclic (D9.5). A program imports what it uses: `import std.io;` and then
`io.read_file(...)` (D9.3, D9.4).

## 2. Modules

Each module section lists its public struct definitions and constants exactly as declared,
then a block of function headers with the bodies omitted, then one entry per function with
semantics, preconditions, runtime errors and ownership, then a short example.

### 2.1 `std.sys`

Process-level services.

```fort
fn noreturn exit(i32 code)
fn string@ args()
fn i32 errno()
fn bool env(string name, string mut* out)
```

- `exit`: flushes every runtime output buffer (D11.5) and terminates the process with status
  `code & 0xFF` (D11.6). Deferred statements of the calling function do not run. Implemented as
  `rt.exit(code);`, the runtime entry point that flushes and ends the process
  (`toolchain.md` 5.1). Ownership: none.
- `args`: returns the same `string@` that `main` received (D8.6, D11.6): `args()[0]` is the
  program name and every element is NUL-terminated. Implemented as `rt.args()`. Ownership: a
  view of storage the runtime owns (D17.3, D13.5); `string@`
  carries no `own`, so `del` of the span or of an element does not compile (D17.9).
- `errno`: the value of C `errno` for the calling thread, read through
  `libc.__errno_location()`. It is meaningful only after a library call has reported failure.
  Ownership: none.
- `env`: looks up `name` in the process environment. On success `*out` aliases the environment
  string (NUL-terminated, valid for the life of the process) and the result is `true`; when the
  variable is absent `*out` is unchanged and the result is `false`. `name` is copied into a
  NUL-terminated buffer for the call (`str.to_cstr`, `del`ed before returning). Ownership:
  `*out` is a view of the environment (D17.3); it is never `del`ed.

```fort
import std.sys;

fn string std_dir() {
    string mut dir = "../std";
    if (!sys.env("FORT_STD_DIR", &dir)) {
        eprintln("FORT_STD_DIR not set, using ", dir);
    }
    return dir;
}
```

### 2.2 `std.libc`

Thin `extern` declarations for the libc calls the other modules need, with the C types mapped per
D9.8: `int` is `i32`, `size_t` is `u64`, `ssize_t` and `off_t` are `i64`, `mode_t` is `u32`,
`char*` is `char*`, and every `void*` buffer is `void*`. Names are unmangled (D9.7). `open` is
variadic in C; the fixed prototype is safe because every extern function is declared and called
through a variadic LLVM function type, so a variadic callee always learns how many vector
registers the call used (D9.8, `toolchain.md` 6 item 8).

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
extern fn void* own malloc(u64 size);
extern fn void* own calloc(u64 n, u64 size);
extern fn void free(void* own p);
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
extern fn i32 isatty(i32 fd);

// <errno.h>: errno is a macro over this accessor in glibc and musl.
extern fn i32 mut* __errno_location();
```

Semantics are those of the C functions. `malloc` returns `void* own` (no `mut`: `void*` has no
target level, D17.13) and `free` takes `void* own` (D17.13): the qualifiers are erased at the
boundary and state C's convention, so the pair is interchangeable with `new` and `del` (D10.3)
and exists for code that sizes an allocation in bytes.
`calloc` returns zeroed storage for `n` items of `size` bytes, or null, and is what `std.rt`
allocates with, since `new` promises zeroed memory (D10.2, `toolchain.md` 5.1); `isatty` answers
D11.5's question about a descriptor and is asked once per buffer (`toolchain.md` 5.3).
`__errno_location` returns `i32 mut*` because C returns `int*`, whose target is mutable: `std.rt`
writes errno back around that `isatty` (`toolchain.md` 5.3), and a caller that only reads it drops
the `mut` on the way (D5.4), which is what `sys.errno()` does. Both are
here because the runtime is a module of this library and imports `std.libc` like any other
(D13.1); no other module calls them.
`u8 mut* own p = cast(libc.malloc(n), u8 mut* own);` types the block, the cast's result being
`own` because its target says so (D3.14; `p` is `null` when C is out of memory, where `new`
would trap, D10.2), `p[0..n]` is a
`u8 mut@` view of it (D6.9), and `del(p)` releases it; so does
`libc.free(cast(move(p), void* own))`, where the `own` lvalue must be moved into the `own`
parameter (D6.11) and is left `null` (D17.6). `memcpy`, `memmove` and `memset` return their
`dst` argument, a view, so their results stay `void*`. `libc.exit` does not flush the runtime's
output buffers; programs call `sys.exit`. `libc.abort` is what the runtime calls after a
runtime error (D11.4). Ownership: `malloc`, `calloc` and `free` carry it in their types, so a
`calloc` result is released like a `malloc` one and not dropped; every other extern here takes
and returns views, and the library wraps every ownership-bearing call below.
Direct use looks like `libc.write(fd, cast(s.ptr, void*), s.len) == cast(s.len, i64)`, which
writes a string to a descriptor, bypassing the runtime's buffers.

### 2.3 `std.mem`

Byte-span primitives over `memmove`, `memset` and `memcmp`.

```fort
fn void copy(u8 mut@ dst, u8@ src)
fn void fill(u8 mut@ dst, u8 v)
fn bool equal(u8@ a, u8@ b)
```

- `copy`: copies `src.len` bytes to the start of `dst`; the ranges may overlap (`memmove`
  semantics). Panics with `"mem.copy: destination too short"` when `dst.len < src.len`. An empty
  `src` is a no-op and makes no C call. Ownership: none; an `own` span argument lends (D17.4).
- `fill`: sets every byte of `dst` to `v`. Ownership: none.
- `equal`: `true` when the lengths are equal and the bytes match; two empty spans are equal
  whatever their pointers. Ownership: none.

Only `u8` spans are covered: a span cast never changes the element size (D3.14), so other
element types are copied with a loop, as `std.vec` does, or through `libc.memmove` on `.ptr`
with a byte count of `n * sizeof(T)`.

```fort
import std.mem;

fn void demo() {
    u8[16] mut key = {};
    mem.copy(key[..], cast("abc", u8@));   // key[0..3] = "abc", rest zero
    mem.fill(key[3..], cast('.', u8));
}
```

### 2.4 `std.io`

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
fn i64 read(i32 fd, u8 mut@ buf)
fn bool write_all(i32 fd, u8@ buf)
fn bool read_all(i32 fd, strbuf.str_buf mut* out)
fn bool read_file(string path, strbuf.str_buf mut* out)
fn bool read_file_bytes(string path, u8 mut@ own mut* out)
fn bool write_file(string path, u8@ data)
```

- `open_read`: opens `path` with `O_RDONLY`. Returns the descriptor, or `-1` with the reason in
  `sys.errno()`. `path` is copied into a NUL-terminated buffer for the call (`str.to_cstr`,
  `del`ed before returning, D13.4); a path containing `\0` names its prefix. Ownership: none
  in the types; the caller releases the descriptor with `close`.
- `open_write`: opens `path` with `O_WRONLY | O_CREAT | O_TRUNC` and mode `0o644`, creating or
  truncating the file. Same result and ownership as `open_read`.
- `close`: flushes the runtime's buffer for `fd` if one exists (D11.5, via `rt.flush`),
  then calls `close(2)`. Returns `true` when `close(2)` returned 0. The descriptor is invalid
  afterwards in either case. Ownership: none in the types; `close` consumes `fd`.
- `flush`: flushes the runtime's buffer for `fd` if one exists and does nothing otherwise.
  Ownership: none.
- `read`: one `read(2)` into `buf`. Returns the number of bytes read (at most `buf.len`), `0` at
  end of file, or `-1` with `sys.errno()` set. `EINTR` is retried inside the call. When
  `buf.len == 0` the result is `0` and no system call is made. Ownership: none; an `own` buffer
  lends (D17.4).
- `write_all`: writes every byte of `buf`, looping over partial writes and retrying `EINTR`.
  Returns `false` on the first error; some bytes may have been written. A `string own s` is
  written as `write_all(fd, cast(s[..], u8@))`, a cast of a view (1.3). Ownership: none.
- `read_all`: reads `fd` to end of file, adding the bytes to `out` after its current contents,
  in chunks of at least 4096 bytes placed directly in the buffer's spare room
  (`out->data[out->len..]`, a view). Returns `false` on a read error with `out->len` restored
  to its value at entry (the storage may have grown). Ownership: the bytes land in storage
  `*out` owns; growth replaces that storage, so earlier `view` and `bytes` aliases of `*out`
  die (2.6).
- `read_file`: `open_read`, `read_all`, `close`. Returns `false` when any step fails, with
  `out->len` restored and `sys.errno()` describing the failing call (`close(2)` leaves `errno`
  alone when it succeeds). On success the contents are `strbuf.bytes(out)` and, as text,
  `strbuf.view(out)`. Ownership: as `read_all`.
- `read_file_bytes`: the same as `read_file`, delivering a plain heap span. Precondition:
  `*out` is empty (`{}`). On success the callee stores a fresh, exact-size allocation holding
  the file's bytes into `*out` (`u8 mut@ own bytes = new(u8, n); ...; *out = move(bytes);`,
  D17.5), so `out->len` is the file's length and an empty file yields a zero-length, non-null
  allocation (D10.2); on failure `*out` is untouched. The store never frees what `*out` held:
  a live slot is the `overwriting owned value` runtime error in checked builds and a leak in
  release builds (D17.11), so a caller that reuses a slot `del`s it first. Ownership: `*out`
  is `u8 mut@ own`; the caller releases it with `del`, usually through `defer del(data);` on
  the `u8 mut@ own mut data` whose address it passed (1.3).
- `write_file`: `open_write`, `write_all`, `close`. Returns `true` only when all three succeed.
  Ownership: none.

```fort
import std.io;
import std.strbuf;
import std.sys;

// cat: copies one file to standard output.
fn i32 main(string@ args) {
    if (args.len != 2) {
        return 2;
    }
    strbuf.str_buf mut buf = strbuf.create();
    defer strbuf.free(&buf);
    if (!io.read_file(args[1], &buf)) {
        eprintln("cat: ", args[1], ": errno ", sys.errno());
        return 1;
    }
    return io.write_all(io.STDOUT, strbuf.bytes(&buf)) ? 0 : 1;
}
```

### 2.5 `std.str`

Operations on `string` (D3.7). None of them allocates unless the entry says so; the three that
do return `own` values (D13.5).

```fort
fn bool equal(string a, string b)
fn i32 cmp(string a, string b)
fn u64 hash(string s)
fn bool starts_with(string s, string prefix)
fn bool ends_with(string s, string suffix)
fn i64 index_of(string s, char ch)
fn i64 last_index_of(string s, char ch)
fn i64 find(string s, string needle)
fn string own dup(string s)
fn string own concat(string a, string b)
fn char mut@ own to_cstr(string s)
fn string from_cstr(char* p)
fn bool parse_i64(string s, i64 mut* out)
fn bool parse_u64(string s, u64 base, u64 mut* out)
fn i64 digit_value(char ch)
fn bool is_digit(char ch)
fn bool is_alpha(char ch)
fn bool is_alnum(char ch)
fn bool is_space(char ch)
fn bool is_hex(char ch)
```

- `equal`: the same as `a == b` (D3.7); it exists so that equality can be a function value,
  `fn bool(string, string) eq = str.equal;`. A `string own` operand lends (D17.4).
- `cmp`: lexicographic order by unsigned byte value, the order `<` gives `char` (D3.2), with a
  proper prefix sorting first; returns `-1`, `0` or `1`.
- `hash`: 64-bit FNV-1a over the bytes: start from `0xcbf29ce484222325`, and for each byte
  `h ^= cast(ch, u64); h *%= 0x100000001b3;`. The wrapping multiply (D11.2) makes the value
  identical in both build modes; the empty string hashes to the offset basis. The function is
  fixed by this specification because `strmap` and on-disk formats may depend on it.
- `starts_with`, `ends_with`: `prefix.len <= s.len && s[..prefix.len] == prefix`, and the
  mirror image for suffixes. An empty prefix or suffix always matches.
- `index_of`, `last_index_of`: position of the first or last `ch` in `s` as `i64`, or `-1`.
- `find`: position of the first occurrence of `needle` in `s`, or `-1`. An empty needle is found
  at `0`. Simple quadratic search; adequate for source-sized inputs.
- `dup`: heap copy of `s` with `result.len == s.len`, not NUL-terminated (1.4). Allocates
  exactly `s.len` bytes: `u8 mut@ own buf = new(u8, s.len);`, then
  `mem.copy(buf, cast(s, u8@));` (`buf` lends, D17.4), then
  `return cast(move(buf), string own);` (D17.12; the `move` empties `buf`, so no second owning
  copy survives, D17.14). `dup("")` is a zero-length, non-null allocation (D10.2). Ownership:
  returns `string own`; the caller binds it to a `string own` variable, passes it to a
  `string own` parameter or `del`s it (D17.12), and may not use it as a plain `string`
  directly (D17.8).
- `concat`: heap copy of `a` followed by `b`, not NUL-terminated; `result.len` is
  `a.len + b.len`, which is also the allocation size. Ownership: as `dup`.
- `to_cstr`: the one sanctioned way to hand a string to C: a heap copy with a trailing `'\0'`,
  returned as an owned `char` span of length `s.len + 1` whose `.ptr` is the C string and
  whose `[..s.len]` is the text (`new(char, s.len + 1)` is already zeroed, D10.2). If `s`
  contains `\0`, C sees the prefix. Ownership: returns `char mut@ own`; the caller releases it
  with `del`, usually `char mut@ own c = str.to_cstr(path); defer del(c);` and then
  `libc.open(c.ptr, ...)`, since `.ptr` lends (D17.3).
- `from_cstr`: wraps the C string at `p` as `p[0..libc.strlen(p)]` cast to `string`; no copy,
  so the result aliases `p` and stays valid as long as `p` does. `from_cstr(null)` is the zero
  string. Ownership: a view (D17.3) of whatever owns `p`; `del` on it does not compile.
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
import std.str;

// Splits "name=123"; false on malformed input, in which case *name and *value are unchanged.
fn bool parse_binding(string line, string mut* name, i64 mut* value) {
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

// Keeps a copy of a name that outlives the buffer it was scanned from.
fn string own keep(string name) {
    string own copy = str.dup(name);
    return copy;   // implicit move of an own local (D17.5)
}
```

### 2.6 `std.strbuf`

A growable buffer for building text and byte sequences.

```fort
struct str_buf {
    u8 mut@ own data;   // storage the buffer owns; data.len is the cap
    u64 len;            // bytes in use; len <= data.len
}
```

`str_buf` is 24 bytes: `data` at offset 0, `len` at offset 16 (`own` is erased, D17.1). The
`own` field makes `str_buf` an owning aggregate (D17.7): functions take `str_buf mut*` or
`str_buf*`, copying a buffer from one variable into another is written `move(b)`, `del` of a
whole `str_buf` is an error, and `free` releases it. The zero value `str_buf{}` is a valid empty
buffer with no storage, so a struct containing a `str_buf` may be initialized with `{}`. The
bytes `data[len..]` are spare room whose contents are unspecified.

```fort
fn str_buf create()
fn str_buf with_cap(u64 cap)
fn void free(str_buf mut* b)
fn void reserve(str_buf mut* b, u64 extra)
fn void push(str_buf mut* b, char ch)
fn void push_byte(str_buf mut* b, u8 v)
fn void append(str_buf mut* b, string s)
fn void append_bytes(str_buf mut* b, u8@ src)
fn void append_i64(str_buf mut* b, i64 v)
fn void append_u64(str_buf mut* b, u64 v)
fn void clear(str_buf mut* b)
fn void truncate(str_buf mut* b, u64 len)
fn string view(str_buf* b)
fn u8@ bytes(str_buf* b)
fn string own take(str_buf mut* b)
```

Growth policy: when an operation needs `len + extra > data.len`, the new cap is the largest of
16, `2 * data.len` and `len + extra`; the contents move to fresh `new` storage and the old
storage is `del`ed, so every earlier `view` or `bytes` alias dies at that moment (1.3). The
replacement is:

```fort
u8 mut@ own bigger = new(u8, cap);      // u8 mut@ own, D17.3
mem.copy(bigger, b->data[..b->len]);    // bigger lends (D17.4); the source is a view
del(b->data);                           // frees and empties the field (D17.9)
b->data = move(bigger);                 // the slot is zero, so the store passes (D17.11)
```

- `create`: `str_buf{}`; allocates nothing. `with_cap`: allocates `cap` bytes of storage up
  front as `str_buf{new(u8, cap), 0}` (an `own` rvalue lands in the `own` field, D17.5); `0`
  is the same as `create`. Ownership: the result is an owning value that the caller keeps in
  a variable (`strbuf.str_buf mut b = strbuf.create();`, a call result needs no `move`, D17.5)
  and releases with `free`.
- `free`: `del(b->data)`, which frees the storage and empties the field (D17.9), then sets
  `b->len` to 0, leaving `str_buf{}`; calling it twice, or on `str_buf{}`, is harmless because
  `del` of a zero span is a no-op.
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
  span as `u8@`. Ownership: views (D17.3), valid until the next mutating call on `*b`; `del`
  on them does not compile (D17.9).
- `take`: an exact-size heap copy of the contents (`result.len == b->len`, not NUL-terminated,
  1.4); then sets `b->len` to 0 and keeps the storage for reuse, so the buffer is empty
  afterwards. Allocates `b->len` bytes: `u8 mut@ own copy = new(u8, b->len);`, `mem.copy`,
  then `return cast(move(copy), string own);` (D17.12). An empty buffer yields a zero-length,
  non-null allocation (D10.2). Ownership: returns `string own`; the caller releases it with
  `del` (D17.12).

```fort
import std.io;
import std.strbuf;

// Formats "    mov <reg>, <imm>\n". Ownership: the caller dels the result.
fn string own mov_imm(string reg, i64 imm) {
    strbuf.str_buf mut b = strbuf.with_cap(32);
    defer strbuf.free(&b);
    strbuf.append(&b, "    mov ");
    strbuf.append(&b, reg);
    strbuf.append(&b, ", ");
    strbuf.append_i64(&b, imm);
    strbuf.push(&b, '\n');
    return strbuf.take(&b);   // an own rvalue flows into the own return type (D17.5)
}

fn bool emit_mov(i32 fd) {
    string own line = mov_imm("rax", 42);
    defer del(line);
    return io.write_all(fd, cast(line[..], u8@));   // a view of line is written (1.3)
}
```

### 2.7 `std.vec`

Growable sequences of pointers and of `i64`, and the pattern for every other element type.

```fort
struct ptr_vec {
    void* mut@ own items;   // slots the vector owns; items.len is the cap
    u64 len;                // slots in use; len <= items.len
}

struct int_vec {
    i64 mut@ own items;
    u64 len;
}
```

Both structs are 24 bytes with `items` at offset 0 and `len` at offset 16 (`own` is erased,
D17.1); `{}` is a valid empty vector. Both are owning aggregates (D17.7): functions take them by
pointer and `*_free` releases them. Each `own` marks one reference (D17.2), so a `ptr_vec` owns its
slots and borrows every pointer in them: `ptr_push` lends its argument (D17.4), `ptr_pop`
returns a view, and `ptr_free` never touches a pointee. The live elements are
`v.items[..v.len]`. Indexing `v.items[i]` with `v.len <= i < v.items.len` is not a runtime
error; it reads a zero or stale slot, so code that wants a bounds check indexes the live span.

```fort
fn ptr_vec ptr_create()
fn ptr_vec ptr_with_cap(u64 cap)
fn void ptr_free(ptr_vec mut* v)
fn void ptr_reserve(ptr_vec mut* v, u64 extra)
fn void ptr_push(ptr_vec mut* v, void* p)
fn void* ptr_pop(ptr_vec mut* v)

fn int_vec int_create()
fn int_vec int_with_cap(u64 cap)
fn void int_free(int_vec mut* v)
fn void int_reserve(int_vec mut* v, u64 extra)
fn void int_push(int_vec mut* v, i64 x)
fn i64 int_pop(int_vec mut* v)
```

- `*_create`, `*_with_cap`, `*_free`, `*_reserve`: exactly as their `strbuf` counterparts,
  counting elements instead of bytes and using the same growth policy (16, then doubling).
  Growth copies the elements to fresh storage with an element loop, then `del`s the old span
  and stores the new one (`del(v->items); v->items = move(bigger);`, D17.11), which is what
  makes the file usable as a template. Ownership: `*_free` `del`s the slots and leaves `{}`; it
  never frees the pointees of a `ptr_vec`.
- `*_push`: appends one element, growing as needed. `*_pop`: removes and returns the last
  element; panics with `"vec.ptr_pop: empty"` or `"vec.int_pop: empty"` on an empty vector.

This module is the non-generic container pattern (D15). To hold `token` values, copy `int_vec`
and its six functions, replace `i64` with `token` and the prefix `int_` with `token_`: about
forty lines, type-checked like any other code. `ptr_vec` is for elements that must not be
copied and belong to someone else (an arena, a fixed array, an owner that outlives the vector):
store `cast(p, void*)` and cast back on retrieval; a pointer cast may add mutability (D3.14),
so a `node mut*` survives the round trip. When the vector is to own its elements, copy the file
with the slot type `node mut* own mut@ own items;` instead, an owned span of owned nodes (D17.2):
`node_push(node_vec mut* v, node mut* own n)` stores `v->items[v->len] = move(n);` (a parameter
is an `own` lvalue, D17.5, and a fresh slot is zero, D10.2, so the store passes D17.11);
`node_pop` returns `node mut* own` with `return move(v->items[v->len]);` (the slot is reached
through `mut` storage, D17.6, and is left `null`); the growth loop moves each element,
`bigger[i] = move(v->items[i]);`; `node_free` `del`s every live element before the span; and
a range loop over the live span lends, `for (node mut* n : v->items[..v->len])` (D17.10).

```fort
import std.vec;

struct node { i64 value; }

fn i64 demo() {
    vec.ptr_vec mut stack = vec.ptr_create();
    defer vec.ptr_free(&stack);
    node mut* own n = new(node);              // new lands in an own place (D17.3, D17.8)
    defer del(n);
    n->value = 7;
    vec.ptr_push(&stack, cast(n, void*));     // lends n; the vector never frees it
    node* top = cast(vec.ptr_pop(&stack), node*);
    return top->value;
}
```

### 2.8 `std.strmap`

A hash table from `string` to `i64` with open addressing, linear probing, tombstones and
`str.hash` (FNV-1a).

```fort
u8 SLOT_EMPTY = 0;
u8 SLOT_FULL = 1;
u8 SLOT_DEAD = 2;

struct str_map_entry {
    string key;   // a borrowed view; the map never owns a key
    i64 val;
    u64 hash;
    u8 state;     // SLOT_EMPTY, SLOT_FULL or SLOT_DEAD
}

struct str_map {
    str_map_entry mut@ own entries;   // entries.len is the cap: 0 or a power of two
    u64 live;                         // entries in state SLOT_FULL
    u64 used;                         // live plus tombstones
}
```

`str_map_entry` is 40 bytes (`key` 0, `val` 16, `hash` 24, `state` 32, then padding); `str_map` is
32 bytes (`own` is erased, D17.1). `str_map{}` is a valid empty map. `str_map` is an owning
aggregate (D17.7) and is passed by pointer; `str_map_entry` has no `own` field, so entries are
copied freely during a rebuild.

Algorithm: a key probes from `hash & (cap - 1)` upward, wrapping, comparing the stored `hash`
and then the key with `==`; a lookup stops at the first `SLOT_EMPTY`. An insert of an absent
key reuses the first `SLOT_DEAD` slot met on its probe, else the `SLOT_EMPTY` that ended it.
Before an insert, if `cap == 0` or `(used + 1) * 4 > cap * 3`, the table is rebuilt: the new
cap starts at 16 and doubles until `(live + 1) * 2 <= cap`, a fresh
`str_map_entry mut@ own table = new(str_map_entry, cap);` receives the live entries, tombstones
disappear, and `del(m->entries); m->entries = move(table);` installs it (D17.11). `remove`
marks the slot `SLOT_DEAD` and decrements `live`; `used` is unchanged until the next rebuild.
Iteration is a walk over `entries` taking the slots whose `state == SLOT_FULL`, in table order.

```fort
fn str_map create()
fn void free(str_map mut* m)
fn bool put(str_map mut* m, string key, i64 val)
fn bool get(str_map* m, string key, i64 mut* out)
fn bool has(str_map* m, string key)
fn bool remove(str_map mut* m, string key)
fn u64 count(str_map* m)
```

- `create`: `str_map{}`; allocates nothing. `free`: `del(m->entries)`, which frees the table and
  empties the field (D17.9), then zeroes `live` and `used`, leaving `str_map{}`; keys are
  untouched, and a second call is harmless. Ownership: the caller calls `free`.
- `put`: inserts `key` with `val`, or replaces the value of an existing key. Returns `true` when
  the key was new. Keys are borrowed views (D13.5): the caller keeps the key's bytes alive and
  unchanged for as long as the entry exists (a literal, a sub-string of a source buffer that
  outlives the map, or a `string own` from `str.dup` held in a variable that outlives the
  entry and is `del`ed afterwards). `put(&m, str.dup(s), v)` does not compile: the copy would
  be an owning temporary that leaks (D17.8). Replacing the value of a key that is there stores
  the value alone: the entry keeps the key view of the insert that created it, and the `key` of
  the later `put`, equal in bytes but possibly other storage, is not stored. It is therefore the
  first key's bytes that must stay alive for as long as the entry does, and a caller replacing a
  value from a buffer it is about to release `remove`s the entry and `put`s it again. Allocates
  on rebuild. Ownership: the map never owns a key.
- `get`: `true` and `*out = val` when present; `false` with `*out` unchanged otherwise. `has`:
  the presence test alone. `remove`: `true` when the key was present. `count`: `live`.

To map names to pointers, keep a `vec.ptr_vec` beside the map and store the index. A struct that
holds two containers is itself an owning aggregate (D17.7), so it is passed by pointer and gets
a `free` of its own:

```fort
import std.strmap;
import std.vec;

struct sym { string name; i64 offset; }

struct sym_tab {
    strmap.str_map index;   // name to position in syms
    vec.ptr_vec syms;       // sym* in declaration order, owned by whoever declared them
}

fn bool declare(sym_tab mut* t, sym mut* s) {
    if (strmap.has(&t->index, s->name)) {
        return false;
    }
    strmap.put(&t->index, s->name, cast(t->syms.len, i64));
    vec.ptr_push(&t->syms, cast(s, void*));
    return true;
}

fn sym* lookup(sym_tab* t, string name) {
    i64 mut i = 0;
    if (!strmap.get(&t->index, name, &i)) {
        return null;
    }
    return cast(t->syms.items[cast(i, u64)], sym*);
}

fn void symtab_free(sym_tab mut* t) {
    strmap.free(&t->index);
    vec.ptr_free(&t->syms);   // the sym values are borrowed and stay alive
}
```

### 2.9 `std.math`

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
  `*cast(&x, u64*)`, which is defined because fort has no strict-aliasing rule (D10.7).
  `f64_inf()` is `f64_from_bits(0x7FF0000000000000)` and `f64_nan()` is
  `f64_from_bits(0x7FF8000000000000)`; they are functions because a call is not a constant
  expression (D4.6) and no float literal denotes infinity (D6.12). `is_nan` is `x != x` (D6.12).
- `abs_i32`, `abs_i64`: `x < 0 ? -x : x`. For `I32_MIN` and `I64_MIN` the negation traps in
  checked mode and yields the minimum again in release mode (D11.1); callers that need a total
  function test for the minimum first.
- `min_*`, `max_*`: `a < b ? a : b` and `a > b ? a : b`. For floats, if either operand is NaN
  the comparison is false and `b` is returned; do not rely on NaN propagation.

```fort
import std.math;

fn bool is_negative_zero(f64 x) {
    return math.f64_bits(x) == 0x8000000000000000;
}
```

### 2.10 `std.rt`

The runtime (D13.1, D13.2): process start and exit, allocation, the failure paths and the print
buffers, written in fort over `std.libc`. `toolchain.md` 5 fixes what each entry point does and
section 3 below names the four the rest of the library calls; `sys` and `io` are its only
importers inside the library, and everything else in it is an implementation detail (1.1).
Its names are mangled like any module's (`std.rt.flush`, D9.7); the compiler knows them, because
a builtin lowers to a call of one (D12.2), and calls them like any other fort function.

## 3. The runtime surface the library relies on

`toolchain.md` owns the runtime: the full entry-point list, the signatures, the print buffers and
process start. `std.rt` is a library module like any other (D13.1, 2.10) and this section names
only what the rest of the library calls of it. The library uses the builtins `new`, `del`,
`move`, `panic`, `assert` and the print family as any program does (D12); the runtime calls
behind them, including the overwrite check of D17.11, are emitted by the compiler and never named
in library source. Beyond that, `sys` and `io` import `std.rt` and call four of its functions,
whose signatures `toolchain.md` 5.1 fixes:

```fort
fn string@ args();
fn void flush(i32 fd);
fn void flush_all();
fn noreturn exit(i32 status);
```

- `args`: the `string@` the runtime built from `argv` at process start (D11.6). It is the same
  span `main` receives, so `sys.args()` and `main`'s parameter are equal span for span. The
  result is a view (D17.3): the runtime keeps the storage, and the span carries no `own`.
- `flush`: writes out the runtime's buffer for one descriptor, if it has one, and is a
  no-op otherwise. `io.close` and `io.flush` call it, as D11.5 specifies.
- `flush_all`: writes out every runtime buffer. The library does not call it; it is
  there for programs that write through `libc.write` after printing (2.2), and the `main` the
  compiler emits calls it at exit (D11.6).
- `exit`: flushes every runtime buffer and exits with `status & 0xFF`; `sys.exit` is a
  call to it.

Three properties of the runtime the library also depends on: `del` frees by the pointer alone,
with no header and no length check (D10.3, D17.9, used by 1.3); `own` changes no bits, so a
value produced by `new` and one adopted from `malloc` are released the same way (D17.1, D17.3);
and `new(T, n)` returns zeroed storage (D10.2), which is why fresh `str_buf`, `vec` and `strmap`
slots read as zero and why a fresh `own` slot passes the overwrite check (D17.11).

## 4. Worked example

`wc.ft` reads the file named on the command line and prints its line and word counts, using
only this library and the builtins. Every line is v1 fort.

```fort
import std.io;
import std.str;
import std.sys;

fn i32 main(string@ args) {
    if (args.len != 2) {
        eprintln("usage: ", args[0], " <file>");
        return 2;
    }
    u8 mut@ own mut data = {};
    defer del(data);
    if (!io.read_file_bytes(args[1], &data)) {
        eprintln("wc: cannot read ", args[1], " (errno ", sys.errno(), ")");
        return 1;
    }
    string text = cast(data[..], string);
    u64 mut lines = 0;
    u64 mut words = 0;
    bool mut in_word = false;
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

Notes: `data` is declared `u8 mut@ own mut` so that `&data` has type `u8 mut@ own mut*` (D5.8,
D17.2), the slot type `read_file_bytes` fills, and it starts as `{}` because the callee stores
over it (D17.11); `defer del(data)` is registered before the call, so both later `return` paths
free the bytes (D7.8), and on the failure path `del` of the still-zero span is a no-op (D17.9);
`text` is a view of the bytes (`data[..]` and a cast between the span families, D17.3, D3.14),
so no second owner exists; the counters are `u64` because `.len` is (D16); and the
`text.len > 0` guard keeps `text.len - 1` from trapping on an empty file (D11.1).

## 5. Not in v1

Deliberately absent, with the idiom to use instead; the language-level list is D15.

- Formatted output beyond the print family: build text in a `str_buf` with `append`,
  `append_i64` and `append_u64`, then `io.write_all` or `print` the result.
- Generic containers: copy `std.vec` for each element type (2.7); use `str_map` with indices
  for pointer values (2.8).
- Containers that own their elements: copy `std.vec` with a `node mut* own mut@ own` slot type
  and a `free` that `del`s every element (2.7).
- Compile-time leak and use-after-`move` detection (D15, D17.14): the idiom is `defer del`
  right after the declaration, and the zero value that `move` and `del` leave behind (1.3).
- Unicode: strings are bytes (D3.7); the classification functions are ASCII-only.
- Floating-point formatting and parsing beyond what the print family emits (D11.7): a program
  that needs a float from text writes its own conversion or calls C through `std.libc`.
- Threads, signals, networking, directories, time, line-at-a-time input: call libc through your
  own `extern` declarations following the `std.libc` conventions, or `read_all` and scan.
