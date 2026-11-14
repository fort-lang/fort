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
  builtin lowers to and which is in every import closure (D12.2, D9.10, 2.11).
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
- **Containers own their storage, not their contents.** `str_buf`, `int_vec` and
  `str_map` keep their storage in an `own` field, which makes each an owning aggregate (D17.7):
  it is passed by pointer (`str_buf mut*`), copying one from a variable into another needs
  `move`, and `del` of a whole container is an error. Each has a `drop` that takes the container
  by `move` and `del`s its storage (`strbuf.drop`, `vec.int_drop`, `strmap.drop`), and a `free`
  that moves `*c` into the `drop` and leaves the zero value; `free` of the zero value is a no-op
  (D17.9). `str_map` keys are borrowed; freeing the container never touches them.
- **The end of a container.** Each owner of a local container must hold the zero value at each
  exit of the local, or the exit is a compile error (D17.20). The compiler reads no callee, and
  a call kills what it knows of each local whose address the function takes (D17.11, kill rule
  5). So a container that no call takes by address ends with a deferred `strbuf.drop(move(c));`,
  and a deferred `strbuf.free(&c);` or `strbuf.drop(move(c));` of a container that the function
  lends to calls is a compile error. A container that the function lends to calls lives in an
  allocation of `new` and ends through a `drop_made` of the program: the value moves out, the
  allocation goes, and the `drop` of the container frees the storage. The samples of 2.4, 2.6
  and 2.7 take this form:

  ```fort
  // Ends a buffer in an allocation of `new` that the caller moves in.
  fn buf_drop_made(strbuf.str_buf mut* own p) void {
      strbuf.str_buf kept = move(*p);
      del(p);
      strbuf.drop(move(kept));
  }

  strbuf.str_buf mut* own b = new(strbuf.str_buf);
  defer buf_drop_made(move(b));
  strbuf.append(b, "text");   // the calls take the pointer, and no call takes the address of b
  ```
- **Out-parameters that receive ownership are pointers to `own` slots**, `u8 mut@ own mut* out`
  (D13.5, D17.2). The caller initializes the slot to `{}` or `null` and passes its address; the
  callee `del`s `*out` and then stores an `own` rvalue into it, because the entry value of `*out`
  is unknown to the callee and the overwrite check of D17.11 needs it empty. The `del` frees
  nothing when the caller initialized the slot.
- **Accessors return views** (D17.3). Functions named `view` or `bytes`, `sys.args`,
  `str.from_cstr` and every sub-span yield values without `own`; `del` on them does not
  compile (D17.9). A view into a container is valid until the next mutating call on that
  container; a view of an owned value is valid until the value is `del`ed or moved away. No
  compile-time rule checks it (D17.14): after the `del`, the view points at dead storage, and
  each access through it has a result of D10.9.
- **The `defer` idiom.** Declare the owning variable empty, defer its release on the next
  line, then fill it. Put the declaration before the first call of the function, so that no
  call comes before it on any path. The function takes the address of the slot, and a call
  kills the fact that such a slot is empty (D17.11, kill rule 5). So a declaration after a
  call stores into a slot that the compiler cannot prove empty, and that is a compile error.
  This block shows the correct idiom, with the declaration first:

  ```fort
  u8 mut@ own mut data = {};
  defer del(data);
  if (!io.read_file_bytes(path, &data)) {
      return false;   // del of a zero span is a no-op (D17.9)
  }
  ```

  A later `return data;` is an implicit `move` (D17.5) that empties `data` before the deferred
  `del` runs (D7.8), so the allocation reaches the caller and every other path frees it. A
  container takes the shape of the bullet above.
- **`del` before storing.** Code that replaces owned storage writes `del(old)` before the store,
  `del(b->data); b->data = move(bigger);`, because a store over an `own` lvalue that the compiler
  cannot prove empty is a compile error (D17.11).
- Nothing in the library allocates without saying so in its entry. The library applies a cast
  between `string` and the byte spans (D3.14) only to a view (`buf[..]`) or to the `own` rvalue
  that `move` yields (`cast(move(buf), string own)`, D17.12). A cast never adds `own` (D3.14).

### 1.4 Talking to C

- Spans, strings and structs never cross an `extern` boundary (D13.4). A call unpacks `.ptr`
  and `.len`: `libc.write(fd, buf.ptr, buf.len)` for a `u8@ buf`. A fixed array has no `.ptr`
  (D3.4); span it first: `arr[..].ptr`. `.ptr` is a view (D17.3), so C never receives
  ownership this way; `own` in an `extern` signature is erased and documents C's convention
  (D17.13), as `libc.malloc` and `libc.free` show. A C function whose result is owned on some
  calls only has its borrowing declaration and a fort function that allocates the buffer:
  `libc.realpath` and `os.real_path` (2.2, 2.14, D17.13).
- NUL termination. These strings carry a `0` after their last character: literals (D3.7), the
  elements of `sys.args()` (D8.6), and the C string at the `.ptr` of a
  `str.to_cstr` result. Owned strings from `str.dup`, `str.concat` and `strbuf.take` do not: a
  `string own` occupies exactly `len` bytes, so that `cast(move(buf), string own)` (D17.12)
  turns an exact-size buffer into one without a second allocation. Sub-strings, `strbuf.view`
  results and file contents are not NUL-terminated either. The one way to hand a string to C
  is `str.to_cstr`, which always copies; `io.open_read`, `io.open_write` and `sys.env` use it
  internally (D13.4). In the other direction, `str.from_cstr` wraps a C string without copying,
  and only a trusted module names it (D9.8).
- Out-parameters. Scalar results use `i64 mut* out` and similar. A function that produces a
  whole file or stream fills a `strbuf.str_buf` through `strbuf.str_buf mut* out`, so that the
  caller can keep reading into the same storage and release it once; `io.read_file_bytes`
  delivers a plain heap span through `u8 mut@ own mut* out` (level 0 is `out`, level 1 the `own`
  slot the callee fills, level 2 the bytes, D5.2, D17.2) for callers that want one allocation
  and one `del`.
- Buffer parameters of externs say what C means by them. A buffer C fixes as bytes is `u8*`,
  or `u8 mut*` where C would drop the `const`, so a `u8` span's `.ptr` crosses with no cast and
  the type system refuses a buffer the caller may not write (2.2). A buffer of no fixed type is
  `void*`, or `void mut*` where C writes it (D3.11); reaching either takes a `cast`, so the
  caller decides what C may write into. An `extern` that hides a C write behind a buffer
  without `mut` lies about C, and no compile rule reaches it: its audit row is wrong (D9.8).
- Each module that the compiler loads from its standard library directory, each module of `<root>`
  in `fort --lib std <root>`, and each section of the interface file of a used library `std` is
  trusted (D9.8): it may declare and call an `extern fn`, and each declaration and each call has
  an audit row (A7 of `ownership-design.md` 5.1). A module that does not write `import extern;`
  calls no extern and names no boundary function: a function of a trusted module with a parameter
  whose type reaches a pointer to `char`, `u8` or `void`, also through a function pointer type,
  such as `str.from_cstr`, `libc.snprintf_float`, `rt.print_enum`, `rt.args_init` and the entries
  of `std.rt` that take a C string. It calls the functions of the library that take spans and
  strings, whose bounds the checker checks.

### 1.5 Naming

Module short names are the last path segment:
`sys libc mem io str strbuf vec strmap math sort net sha256`.
Functions, struct and enum types, enum members, fields and variables are `lower_case` with
underscores (`str_buf`, `int_vec`, `str_map_entry`); module constants are `UPPER_CASE` (D1.4).
When a module has several struct types, the functions carry the type as a prefix
(`vec.int_push`, `strbuf.append`). A variable never takes its type's name (`str_buf b`, never
`str_buf str_buf`; D1.4). A `char` parameter is called `ch`, and library sources do not reuse an
import binding's name for a local even though D7.9 permits it.

### 1.6 Module list

| Module          | Imports                                     | Purpose                          |
|-----------------|---------------------------------------------|----------------------------------|
| `std.libc`      | none                                        | libc `extern`s, flags, errno     |
| `std.heap`      | `libc`                                      | the allocator of the runtime     |
| `std.rt`        | `heap`, `libc`, `strbuf`                    | the runtime itself (section 3)   |
| `std.mem`       | `heap`, `libc`                              | copy, fill and compare bytes     |
| `std.str`       | `libc`, `mem`                               | compare, search, classify, parse |
| `std.sys`       | `libc`, `rt`, `str`, `strbuf`               | exit, args, errno, env, run      |
| `std.strbuf`    | `mem`                                       | growable text and byte buffer    |
| `std.io`        | `libc`, `rt`, `mem`, `sys`, `str`, `strbuf` | descriptors, whole files, streams|
| `std.vec`       | none                                        | `int_vec`, the pattern           |
| `std.strmap`    | `str`                                       | string-keyed open-addressing map |
| `std.math`      | none                                        | float bit casts, abs, min, max   |
| `std.sort`      | none                                        | a typed span sorted in place     |
| `std.net`       | `libc`, `str`, `sys`                        | a TCP listener and a connection  |
| `std.os`        | `libc`, `str`, `strbuf`                     | triple, files, directories      |

`std.digest.sha256` imports nothing and gives the SHA-256 digest of bytes (2.15).

`std.libc`, `std.net` and `std.os` have one source for each target: `std/linux/` and
`std/darwin/`. The other modules are the same on both targets and stand in `std/`.
`tools/assemble_std.sh <linux|darwin> std <dir>` writes the standard root of one target. A module
in a subdirectory keeps its path below `std/` or `std/<target>/`: `std/digest/sha256.ft` is the
module `std.digest.sha256` (D9.2). The directory of the other target gives nothing.

The import graph is acyclic (D9.5). A program imports what it uses: `import std.io;` and then
`io.read_file(...)` (D9.3, D9.4).

## 2. Modules

Each module section lists its public struct definitions and constants exactly as declared,
then a block of function headers with the bodies omitted, then one entry per function with
semantics, preconditions, runtime errors and ownership, then a short example.

### 2.1 `std.sys`

Process-level services.

```fort
fn exit(i32 code) noreturn
fn args() string@
fn errno() i32
fn env(string name, strbuf.str_buf mut* out) bool
fn set_errno(i32 code) void
fn pid() i32
fn abort() noreturn
fn raise_signal(i32 sig) bool
fn set_env(string name, string value) bool
fn unset_env(string name) bool

enum run_result { rr_done, rr_not_started, rr_not_waited }
fn run(string@ argv, string@ env, i32 mut* status) run_result
```

The compiler and its test suites call C only through functions of the library that take
spans, strings and plain values (D9.8). This section, 2.3, 2.4, 2.5 and 2.14 give them. Each one
takes each length from a span or a string, and each failure gives a result that the caller
tests, with the reason in `sys.errno()`. None is a boundary function (D9.8), so each module can
call it.

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
- `env`: looks up `name` in the process environment. On success it appends a copy of the value
  to `*out` after what it holds, and the result is `true`; when the variable is absent `*out` is
  unchanged and the result is `false`. A `name` that holds `=` before its first NUL gives `false`
  with `*out` unchanged on each target: no variable name holds `=`, and the C libraries differ
  there (glibc `getenv` finds no variable, and the Darwin one looks up the name before the `=`).
  `setenv(3)` can free the storage of a value that it set before, so a view
  of the environment can end at the next `set_env`; the copy stays. `name` is copied into a
  NUL-terminated buffer for the call (`str.to_cstr`, `del`ed before returning), and a NUL in
  `name` limits the lookup to its prefix. Ownership: `*out` belongs to the caller.
- `set_errno`: sets C `errno` of the calling thread to `code`. Ownership: none.
- `pid`: the process id, from `getpid(2)`, which does not fail. Ownership: none.
- `abort`: ends the process with `SIGABRT`, as `abort(3)` does. It flushes no runtime output
  buffer, and deferred statements do not run. Ownership: none.
- `raise_signal`: sends the signal `sig` to the calling thread with `raise(3)`. Returns `true`
  when `raise` accepts the signal, and a signal that ends the process does not return; returns
  `false` with `EINVAL` for a signal number that the system does not have. Ownership: none.
- `set_env`: sets the variable `name` of the process environment to `value` with `setenv(3)`,
  and replaces a value that is there. Returns `false` with the errno of `setenv`: `EINVAL` for an
  empty name or a name that contains `=`. A NUL in `name` or `value` limits it to its prefix.
  Ownership: none; `setenv` copies both.
- `unset_env`: removes the variable `name` from the process environment with `unsetenv(3)`. A
  name that is not there is no failure. Returns `false` with the errno of `unsetenv`: `EINVAL`
  for an empty name or a name that contains `=`. A NUL in `name` limits it to its prefix.
  Ownership: none.
- `run`: runs the command `argv` in a child process and waits for it. `argv[0]` names the
  program, which `execvp(3)` looks for in `PATH`. `env` holds pairs of a name and a value, which
  the child sets with `setenv(3)` before the exec; a pair that `setenv` refuses leaves the
  environment of the child as it was, and the environment of the caller does not change. The
  child gets the descriptors of the caller. `run` makes each C string before `fork(2)`, so the
  child calls only `close(2)`, `setenv`, `execvp`, the errno accessor of the C library through
  `errno` (`__errno_location` on Linux, `__error` on Darwin), `write(2)` and `_exit(2)`. A pipe
  whose write end closes at the exec tells the parent whether the exec succeeded: after a failed
  exec the child writes its errno to the pipe and ends with status 127.
  Returns `rr_done` with the wait status of
  `waitpid(2)` in `*status`: the exit status in its second byte, and a signal that ended the
  child in its low seven bits. Returns `rr_not_started` when `pipe(2)`, `fcntl(2)`, `fork(2)` or
  `execvp` fails, with the errno of that call, and with `EINVAL` for an empty `argv` or an odd
  count of strings in `env`. Returns `rr_not_waited` when `waitpid` fails with an errno other
  than `EINTR`, which it retries. `run` waits for a child that it started in each case but the
  last, so no zombie stays. `*status` changes only for `rr_done`. A NUL in a string of `argv` or
  `env` ends that string. `run` flushes no runtime output buffer, and the child flushes none: a
  caller whose output must come before the output of the command calls `io.flush` first.
  Ownership: none; `run` frees the C strings that it makes.

```fort
import std.strbuf;
import std.sys;

fn std_dir(strbuf.str_buf mut* out) void {
    if (!sys.env("FORT_STD_DIR", out)) {
        strbuf.append(out, "../std");
        eprintln("FORT_STD_DIR not set, using ", strbuf.view(out));
    }
}
```

### 2.2 `std.libc`

The target-specific `std.libc` module supplies C extern declarations and fixed fort wrappers.
It maps C types per D9.8: `int` is `i32`, `size_t` is `u64`, `ssize_t` and `off_t` are `i64`.
`mode_t` is `u32` on Linux and `u16` on Mac. The parameters of `mkdir` and `chmod` cross as `u32`
on both targets, and `umask` and `mkfifo` give each target its own type. `char*` is `char*`.
A buffer C fixes as bytes is `u8*` or `u8 mut*`. A buffer of no fixed
type is `void*` or `void mut*` (1.4). C extern names are unmangled (D9.7). `open` is
variadic in C, so both targets declare `open` with `...` (D9.8). Apple arm64 then places its
mode tail on the stack, and a Linux call sets the vector-register count.

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
i32 EINVAL = 22;

// <stdlib.h>, <string.h>
extern fn malloc(u64 size) void mut* own;
extern fn calloc(u64 n, u64 size) void mut* own;
extern fn free(void* own p) void;
extern fn memcpy(u8 mut* dst, u8* src, u64 n) void;
extern fn memmove(u8 mut* dst, u8* src, u64 n) void;
extern fn memcmp(u8* a, u8* b, u64 n) i32;
extern fn memset(u8 mut* dst, i32 v, u64 n) void;
extern fn strlen(char* s) u64;
extern fn getenv(char* name) char*;
extern fn exit(i32 code) noreturn;
extern fn abort() noreturn;

// <sys/mman.h>: what the allocator of the runtime uses (2.16). MAP_ANON is 0x1000 and
// MADV_DISCARD is 5 (MADV_FREE) in std/darwin/libc.ft. `map_range` is the first 2^32 bytes of a
// mapping: no caller of mmap maps fewer bytes.
i32 PROT_NONE = 0;
i32 PROT_READ = 1;
i32 PROT_WRITE = 2;
i32 MAP_PRIVATE = 2;
i32 MAP_ANON = 0x20;
i32 MADV_DISCARD = 4;                        // MADV_DONTNEED
u64 MAP_RANGE_WORDS = 536870912;
struct map_range {
    u64[MAP_RANGE_WORDS] words;
}
extern fn mmap(void* addr, u64 len, i32 prot, i32 flags, i32 fd, i64 offset) map_range mut*;
extern fn munmap(void* addr, u64 len) i32;
extern fn mprotect(void* addr, u64 len, i32 prot) i32;
extern fn madvise(void* addr, u64 len, i32 advice) i32;

// <fcntl.h>, <unistd.h>
extern fn open(char* path, i32 flags, ...) i32;
extern fn read(i32 fd, u8 mut* buf, u64 n) i64;
extern fn write(i32 fd, u8* buf, u64 n) i64;
extern fn close(i32 fd) i32;
extern fn lseek(i32 fd, i64 offset, i32 whence) i64;
extern fn isatty(i32 fd) i32;

// <sys/socket.h>, <netinet/in.h>
extern fn socket(i32 domain, i32 kind, i32 protocol) i32;
extern fn setsockopt(i32 fd, i32 level, i32 name, void* value, u32 len) i32;
extern fn bind(i32 fd, void* addr, u32 len) i32;
extern fn listen(i32 fd, i32 backlog) i32;
extern fn accept(i32 fd, void mut* addr, u32 mut* len) i32;
extern fn connect(i32 fd, void* addr, u32 len) i32;
extern fn getsockname(i32 fd, void mut* addr, u32 mut* len) i32;

// <errno.h>: errno is a macro over this accessor in glibc and musl.
extern fn __errno_location() i32 mut*;

// fcntl(2), readlink(2) and realpath(3). fcntl takes a variable tail in C, and this declaration
// keeps it on both targets (D9.8).
extern fn fcntl(i32 fd, i32 cmd, ...) i32;
extern fn readlink(char* path, char mut* buf, u64 size) i64;
// Pass a buffer of PATH_MAX bytes as `resolved`. The result is that buffer, or null with errno set.
// A null buffer makes C allocate a result that fort cannot free, so use `os.real_path` instead.
extern fn realpath(char* path, char mut* resolved) char*;

// <dirent.h>: a directory stream is an allocation of C that closedir frees. `dirent` is the
// x86-64 glibc layout of `struct dirent` (std/darwin/libc.ft holds the Mac layout, with a
// `u16 namlen` before `kind` and a `char[1024] name`), and `dirent_head` holds its fields before
// `name`. readdir gives a `dirent_head*`, or null at the end of the stream and on an error.
struct dirent {
    u64 ino;
    i64 off;
    u16 reclen;
    u8 kind;
    char[256] name;
}
struct dirent_head {
    u64 ino;
    i64 off;
    u16 reclen;
    u8 kind;
}
extern fn opendir(char* path) void mut* own;
extern fn fdopendir(i32 fd) void mut* own;
extern fn readdir(void mut* dir) dirent_head*;
extern fn closedir(void mut* own dir) i32;

// The size of the buffer that realpath(3) writes, the terminating zero included.
u64 PATH_MAX = 4096;                         // 1024 in std/darwin/libc.ft

// <string.h>, <stdlib.h>, <unistd.h>, <sys/stat.h>, <sys/wait.h>: what the functions of this
// library that the compiler and its tests call use. The `mode_t` of `mkdir` and `chmod` crosses
// as u32 on both targets: one signature serves both roots. ERANGE, F_SETFD and FD_CLOEXEC have
// one value on both targets.
i32 EEXIST = 17;
i32 R_OK = 4;
i32 ERANGE = 34;
i32 F_SETFD = 2;
i32 FD_CLOEXEC = 1;
extern fn strerror(i32 code) char*;
extern fn strtod(char* s, char* mut* end) f64;
extern fn strtof(char* s, char* mut* end) f32;
extern fn mkdtemp(char mut* tmpl) char*;
extern fn setenv(char* name, char* value, i32 overwrite) i32;
extern fn unsetenv(char* name) i32;
extern fn access(char* path, i32 mode) i32;
extern fn getcwd(char mut* buf, u64 size) char mut*;
extern fn unlink(char* path) i32;
extern fn rename(char* old_path, char* new_path) i32;
extern fn rmdir(char* path) i32;
extern fn symlink(char* target, char* link_path) i32;
extern fn mkdir(char* path, u32 mode) i32;
extern fn chmod(char* path, u32 mode) i32;
extern fn dup(i32 fd) i32;
extern fn dup2(i32 old_fd, i32 new_fd) i32;
extern fn pipe(i32 mut* fds) i32;
extern fn fork() i32;
extern fn execvp(char* file, void* argv) i32;
extern fn waitpid(i32 pid, i32 mut* status, i32 options) i32;
extern fn _exit(i32 status) noreturn;
extern fn raise(i32 sig) i32;
extern fn umask(u32 mask) u32;               // u16 in std/darwin/libc.ft
extern fn mkfifo(char* path, u32 mode) i32;  // u16 mode in std/darwin/libc.ft
```

A C library function is declared here and nowhere else (since 2026-09-25). The compiler and the
`test/fort` suites call none of them (D9.8): they call the functions of `std.os`, `std.io`,
`std.sys`, `std.str` and `std.mem` that take spans and strings (2.1, 2.3, 2.4, 2.5, 2.14), and
those functions call `realpath`, `access`, `getcwd`, `strerror`, `mkdtemp`, `mkdir`, `chmod`,
`symlink`, `mkfifo`, `umask`, `unlink`, `unlinkat`, `rmdir`, `rename`, `readlink`, `opendir`,
`fdopendir`, `readdir`, `closedir`, `dup`, `dup2`, `getpid`, `raise`, `setenv`, `unsetenv`,
`fork`, `execvp`, `waitpid`, `pipe`, `fcntl`, `_exit`, `strtod`, `strtof` and `snprintf`. No suite
module of `test/fort` declares a C function. `test/fort/heap_full_test.ft` and
`test/fort/heap_segments_test.ft` are the two suite modules that write `import extern;`: each
calls `std.heap.drop`, a boundary function (2.16, D9.8).
`_NSGetExecutablePath` stays in the Mac `std.os`: it is dyld's, not the C library's.

Semantics are those of the C functions. Each buffer parameter says what C means by it (1.4):
`read`, `write` and the four `<string.h>` entries count bytes, so they take `u8*` and `u8 mut*`
and a `u8` span's `.ptr` crosses with no cast; `malloc`, `calloc` and the socket addresses take
storage of no fixed type, so they take `void*` and `void mut*`. The `mut` is
there exactly where C's own prototype has no `const`, so the type system refuses a buffer the
caller may not write: `libc.read(fd, view.ptr, view.len)` on a `u8@` is
"the argument expects u8 mut*, not u8*".
`malloc` and `calloc` return `void mut* own`, storage of no type that the caller may write, and
`free` takes `void* own`, which every result reaches by dropping marks (D3.11, D5.4, D17.13):
the `own` is erased at the boundary and states C's convention. The pair is not interchangeable
with `new` and `del` (D10.3): `free` releases what `malloc` and `calloc` gave, and a `del` of that
storage is the runtime error `invalid release` (D10.9). The pair exists for storage that fort code
gives to C.
`calloc` returns zeroed storage for `n` items of `size` bytes, or null. No module of the library
calls `malloc`, `calloc` or `free`: the runtime has its own allocator, `std.heap` (2.16, D10.9).
`mmap`, `munmap`, `mprotect` and `madvise` are here for that allocator, and no other module calls
them. A call that changes a mapping can take the bytes of dead storage away or give its range to
another object, so the audit row of each of the four asks for a call row that says why the range
holds no dead storage, or that the runtime gives dead storage back (D10.9 rule 6). `mmap` gives a
`map_range mut*`, a struct of 2^32 bytes, because the pointee of the result of an extern lies
inside its storage (A7 of `ownership-design.md` 5.1): each caller maps 2^32 bytes or more and
tests the result against the address -1 before another use. `isatty` answers
D11.5's question about a descriptor and is asked once per buffer, and once more at the first
output after `io.close` or `io.dup2` resets the buffer (`toolchain.md` 5.3).
`__errno_location` returns `i32 mut*` because C returns `int*`, whose target is mutable: `std.rt`
writes errno back around that `isatty` (`toolchain.md` 5.3), and a caller that only reads it drops
the `mut` on the way (D5.4), which is what `sys.errno()` does. Both are
here because the runtime is a module of this library and imports `std.libc` like any other
(D13.1); no other module calls them.
No cast gives a type to the `void*` of `malloc` or `calloc` (D3.14), so fort code gives that
storage to C and releases it with `libc.free(move(p))`, where the `own` lvalue must be
moved into the `own` parameter (D6.11) and is left `null` (D17.6). A `del` of it is the runtime
error `invalid release`, because C gave the storage (D10.3). A program that wants typed
storage uses `new`. C's `memcpy`, `memmove` and `memset` return their `dst` argument, and the
declarations here give no result: a result would give back the `.ptr` of an empty span as a
`u8 mut*` to the byte after the span (D3.5), and no code of the six trees used it. A program that
declares one of them must write the same `void` result (D9.8). `strtod` and `strtof` take
`char* mut* end`, because C writes a `char*` into `*end` (A7 of `ownership-design.md` 5.1);
`std.rt` passes `null`.
`libc.exit` does not flush the runtime's output buffers; programs call `sys.exit`. `libc.abort`
is what the runtime calls after a runtime error (D11.4). `std.libc` declares no `qsort`: its
comparator reads each element through a cast from a `void*`, which no cast allows (D3.14), and
`std.sort` sorts typed spans in fort (2.10). A directory stream of `opendir` and `fdopendir` is an
allocation of C, so each result says `own`, and `closedir` takes it with `move`; a `del` of it
is the runtime error `invalid release` (D10.3). A record of `readdir` holds `reclen` bytes, and
its name ends inside them, so a short record does not hold the whole `name` array of a `dirent`.
`readdir` therefore gives a `dirent_head*`, a view of the fields before the name in storage of C
that the next `readdir` of the stream can change, or null at the end of the stream and on an
error (D9.8). `os.list_dir` checks `reclen` against the size of a `dirent`, copies `reclen` bytes
into a zero local `dirent`, and reads the name there with `str.from_cbuf` (2.14). The
`i32 mut*` result of `__errno_location` names storage of C that C lets the caller write, and its
audit row says so (D9.8, D17.13).
Ownership: `malloc`, `calloc` and `free` carry it in their types, so a
`calloc` result is released like a `malloc` one and not dropped; every other extern here takes
and returns views, and the library wraps every ownership-bearing call below.
`rename` gives the file at `old_path` the name `new_path` and returns 0, or -1 with errno set.
It replaces a file at `new_path` in one atomic step, so a reader of `new_path` sees the old
file or the new file and never a partial file. Both paths must be on one file system. A program
writes a file atomically with a temporary file beside the target and a `rename` onto the target.
`readlink` and `realpath` write into the caller's buffer, so the buffer is `char mut*`.
`realpath` returns that buffer, a view. With a null buffer C allocates the result and the caller
must free it, but one symbol has one signature (D9.8) and a cast never adds `own` (D3.14), so
fort code cannot free that result. `std.os.real_path` is the owning form: it allocates a buffer
of `PATH_MAX` bytes with `new` and passes it (2.14, D17.13).
Only a trusted module calls these declarations (D9.8). In a trusted module, direct use looks
like `libc.write(fd, cast(s, u8@).ptr, s.len) == cast(s.len, i64)`, which
writes a string to a descriptor, bypassing the runtime's buffers; the `cast` is there because a
`string` carries `char*` and not `u8*`, and it casts the string because no cast takes a `.ptr`
(D3.5). `libc.read(fd, buf.ptr, buf.len)` on a `u8 mut@` needs none.

The seven socket calls come from `<sys/socket.h>` and `<netinet/in.h>`. `socklen_t` is
`unsigned int`, so it is `u32`; an address is a struct of no fixed type here, so it crosses as
`void*`, or as `void mut*` where C writes it, which is `accept` and `getsockname` (1.4); those
two take the address length through a pointer because they write it back as well, and a scalar
out-parameter keeps its type rather than becoming a `void*` (1.4). `htons` and `htonl` are
absent: glibc defines each as a macro as well as a function, and a macro has no symbol an `extern`
declaration can name (D9.8), so `std.net` writes the two swaps in fort. `std.net` wraps all seven
calls and is what a program uses (2.13). A program that declares one of these symbols itself must
write the same signature, because `std.libc` is in every import closure (D9.8, D9.10).

### 2.3 `std.mem`

Byte-span primitives over `memmove`, `memset` and `memcmp`.

```fort
fn copy(u8 mut@ dst, u8@ src) void
fn fill(u8 mut@ dst, u8 v) void
fn equal(u8@ a, u8@ b) bool
fn can_allocate(u64 size) bool
```

- `copy`: copies `src.len` bytes to the start of `dst`; the ranges may overlap (`memmove`
  semantics). Panics with `"mem.copy: destination too short"` when `dst.len < src.len`. An empty
  `src` is a no-op and makes no C call. Ownership: none; an `own` span argument lends (D17.4).
- `fill`: sets every byte of `dst` to `v`. Ownership: none.
- `equal`: `true` when the lengths are equal and the bytes match; two empty spans are equal
  whatever their pointers. Ownership: none.
- `can_allocate`: reports whether `new` gives `size` bytes now. It is a call of `heap.can_take`
  (2.16): the allocator of the runtime has the address range, and the system gives the storage.
  It gives no storage to an allocation, but it can map a new segment, as `heap.can_take` does.
  `new` stops the process with `out of memory` where this function returns `false` (D10.2,
  D10.9), so a caller that must see the refusal of a large size asks here first, as the
  interpreter of `--fir-run` does for a region of 1 MiB or more. Ownership: none.

Only `u8` spans are covered: a span cast never changes the element size (D3.14), so other
element types are copied with a loop, as `std.vec` does, or through `libc.memmove` on `.ptr`
with a byte count of `n * sizeof(T)`.

```fort
import std.mem;

fn demo() void {
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

fn open_read(string path) i32
fn open_write(string path) i32
fn open(string path, i32 flags, i32 mode) i32
fn close(i32 fd) bool
fn flush(i32 fd) void
fn read(i32 fd, u8 mut@ buf) i64
fn write(i32 fd, u8@ buf) i64
fn write_all(i32 fd, u8@ buf) bool
fn read_all(i32 fd, strbuf.str_buf mut* out) bool
fn read_file(string path, strbuf.str_buf mut* out) bool
fn read_file_bytes(string path, u8 mut@ own mut* out) bool
fn write_file(string path, u8@ data) bool
fn is_terminal(i32 fd) bool
fn pipe(i32 mut* read_end, i32 mut* write_end) bool
fn dup(i32 fd) i32
fn dup2(i32 old_fd, i32 new_fd) i32
```

- `open_read`: opens `path` with `O_RDONLY`. Returns the descriptor, or `-1` with the reason in
  `sys.errno()`. `path` is copied into a NUL-terminated buffer for the call (`str.to_cstr`,
  `del`ed before returning, D13.4); a path containing `\0` names its prefix. Ownership: none
  in the types; the caller releases the descriptor with `close`.
- `open_write`: opens `path` with `O_WRONLY | O_CREAT | O_TRUNC` and mode `0o644`, creating or
  truncating the file. Same result and ownership as `open_read`.
- `open`: opens `path` with the `open(2)` flags `flags`, for example
  `libc.O_RDONLY | libc.O_DIRECTORY`. `mode` gives the permission bits, less the umask, of a file
  that `O_CREAT` makes, and the other flags ignore it. Same result and ownership as `open_read`.
- `close`: flushes the runtime's buffer for `fd` if one exists and resets it (D11.5, via
  `rt.reset_buffer`), then calls `close(2)`. A file that a later open gives the same number gets
  the buffer state of its own type. Returns `true` when `close(2)` returned 0. The descriptor is
  invalid afterwards in either case. Ownership: none in the types; `close` consumes `fd`.
- `flush`: flushes the runtime's buffer for `fd` if one exists and does nothing otherwise.
  Ownership: none.
- `read`: one `read(2)` into `buf`. Returns the number of bytes read (at most `buf.len`), `0` at
  end of file, or `-1` with `sys.errno()` set. `EINTR` is retried inside the call. When
  `buf.len == 0` the result is `0` and no system call is made. Ownership: none; an `own` buffer
  lends (D17.4).
- `write`: one `write(2)` from `buf`. Returns the number of bytes written (at most `buf.len`), or
  `-1` with `sys.errno()` set. `EINTR` is retried inside the call. When `buf.len == 0` the result
  is `0` and no system call is made. Ownership: none.
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
- `read_file_bytes`: the same as `read_file`, delivering a plain heap span. On success the callee
  stores a fresh, exact-size allocation holding the file's bytes into `*out`
  (`u8 mut@ own bytes = new(u8, n); ...; del(*out); *out = move(bytes);`, D17.5), so `out->len` is
  the file's length and an empty file yields a zero-length, non-null allocation (D10.2); on failure
  `*out` is untouched. The callee `del`s `*out` just before the store, because the entry value of
  `*out` is unknown to it (D17.11). So a live slot is freed before the store, and an empty slot
  (`{}`) frees nothing. Ownership: `*out` is `u8 mut@ own`; the caller releases it with `del`, usually through
  `defer del(data);` on the `u8 mut@ own mut data` whose address it passed (1.3).
- `write_file`: `open_write`, `write_all`, `close`. Returns `true` only when all three succeed.
  Ownership: none.
- `is_terminal`: `true` when `isatty(3)` says that `fd` names a terminal. Returns `false` with
  `ENOTTY` for a descriptor of another file and `EBADF` for a descriptor that is not open.
  Ownership: none.
- `pipe`: makes a pipe with `pipe(2)`, writes the descriptor of its read end to `*read_end` and
  of its write end to `*write_end`, and returns `true`. Returns `false` with the errno of `pipe`,
  for example `EMFILE` when the process has no free descriptor, and changes neither. Ownership:
  none in the types; the caller closes both descriptors.
- `dup`: gives a new descriptor of the file that `fd` names with `dup(2)`, the lowest descriptor
  that is free. The two descriptors share the position and the status flags of the file. Returns
  `-1` with the errno of `dup`: `EBADF` when `fd` is not open, `EMFILE` when no descriptor is
  free. Ownership: none in the types; the caller closes the new descriptor.
- `dup2`: makes `new_fd` name the file that `old_fd` names with `dup2(2)` and returns `new_fd`.
  It first flushes the runtime buffers of `old_fd` and of `new_fd`: the bytes that the print
  family wrote to `old_fd` before come before the bytes written to `new_fd` after, and the bytes
  of `new_fd` go to the file that it named before, which `dup2` closes. Then the buffer of
  `new_fd` asks `isatty` again with `rt.reset_buffer` (3), so it is line-buffered for a terminal
  and not for another file (D11.5). `dup2` flushes `old_fd` before `new_fd`, so when both name
  one file, the bytes of `old_fd` come first, whatever order the program wrote them in: `dup2`
  keeps no order between two buffers of one file, and a caller that needs one calls `io.flush`
  between the writes. When `new_fd` is `old_fd` and is open, nothing changes.
  Returns `-1` with the errno of `dup2`: `EBADF` when `old_fd` is not open or `new_fd` is
  negative. Ownership: none in the types.

```fort
import std.io;
import std.strbuf;
import std.sys;

// Ends a buffer in an allocation of `new` that the caller moves in (1.3).
fn buf_drop_made(strbuf.str_buf mut* own p) void {
    strbuf.str_buf kept = move(*p);
    del(p);
    strbuf.drop(move(kept));
}

// cat: copies one file to standard output.
fn main(string@ args) i32 {
    if (args.len != 2) {
        return 2;
    }
    strbuf.str_buf mut* own buf = new(strbuf.str_buf);
    defer buf_drop_made(move(buf));
    if (!io.read_file(args[1], buf)) {
        eprintln("cat: ", args[1], ": errno ", sys.errno());
        return 1;
    }
    return io.write_all(io.STDOUT, strbuf.bytes(buf)) ? 0 : 1;
}
```

### 2.5 `std.str`

Operations on `string` (D3.7). None of them allocates unless the entry says so; the three that
do return `own` values (D13.5).

```fort
fn equal(string a, string b) bool
fn cmp(string a, string b) i32
fn hash(string s) u64
fn starts_with(string s, string prefix) bool
fn ends_with(string s, string suffix) bool
fn index_of(string s, char ch) i64
fn last_index_of(string s, char ch) i64
fn find(string s, string needle) i64
fn dup(string s) string own
fn concat(string a, string b) string own
fn to_cstr(string s) char mut@ own
fn from_cstr(char* p) string
fn from_cbuf(u8@ b) string
fn parse_i64(string s, i64 mut* out) bool
fn parse_u64(string s, u64 base, u64 mut* out) bool
fn parse_f64(string s, f64 mut* out) bool
fn parse_f32(string s, f32 mut* out) bool
fn format_f64(u8 mut@ buf, i32 precision, f64 v) i32
fn digit_value(char ch) i64
fn is_digit(char ch) bool
fn is_alpha(char ch) bool
fn is_alnum(char ch) bool
fn is_space(char ch) bool
fn is_hex(char ch) bool
```

- `equal`: the same as `a == b` (D3.7); it exists so that equality can be a function value,
  `fn (string, string) bool eq = str.equal;`. A `string own` operand lends (D17.4).
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
  string. It is the one trusted form by which a program gets a span from a raw pointer: no other
  function takes a span of a raw pointer but `rt.args_init`, which no source calls (D6.9). Only a
  trusted module names `from_cstr`, because it takes a `char*` (D9.8).
  It trusts that a NUL ends `p` inside its storage, as `strlen` does; a `p` with no such NUL is
  a case of the trusted premise of D10.7 (A7 of `ownership-design.md` 5.1). It takes no `.ptr`
  (D3.5): the
  `.ptr` of an empty span names the byte after its object, and strlen would read from there. Pass
  `&buf[0]`, whose index is checked, or give the buffer to `from_cbuf`. Ownership: a view (D17.3)
  of whatever owns `p`; `del` on it does not compile.
- `from_cbuf`: wraps the bytes of `b` before its first NUL as a `string`, with no copy; the scan
  reads only the `b.len` bytes of `b`. A `b` with no NUL panics with
  `str.from_cbuf: no NUL in the buffer` (1.2, D13.3), and so does an empty `b`. It is the form for
  a buffer that C wrote a C string into: `str.from_cbuf(cast(buf[..], u8@))` after `getcwd` or
  `realpath`. Ownership: a view (D17.3) of whatever owns `b`.
- `parse_i64`: decimal with an optional leading `-`, at least one digit and nothing else: no
  `+`, no whitespace, no `_`. Returns `false` on a syntax error or when the value does not fit
  `i64`; `-9223372036854775808` is accepted. Overflow is detected arithmetically, without
  relying on either build mode's behavior (D11.1).
- `parse_u64`: digits of `base` (2 to 36) using `0-9`, `a-z` and `A-Z`, no sign, no `0x`
  prefix, no `_` (stripping prefixes and separators is the lexer's job). Returns `false` when
  `s` is empty, `base` is out of range, a character is not a digit below `base`, or the value
  overflows `u64`.
- `parse_f64`: reads the longest prefix of `s` that `strtod(3)` reads, as the C library reads
  it: white space first, then a decimal or a hexadecimal form, `inf` or `nan`; the text after the
  prefix stays unread, and a text with no number gives 0. A NUL in `s` ends the text. Writes the
  value to `*out` and returns `true` with `sys.errno()` unchanged when the value is in range.
  Returns `false` with `ERANGE` when it is not; `*out` then holds the value that `strtod` gives,
  an infinity or a value near 0. Ownership: none; the copy with a NUL for C is freed before the
  return.
- `parse_f32`: `parse_f64` for an `f32`, with `strtof(3)`.
- `format_f64`: writes `v` in the exponent form `%.*e` of `snprintf(3)`, with `precision` digits
  after the point, as the C library writes it: at most `buf.len - 1` bytes of the text into
  `buf`, then a NUL. An empty `buf` gets no byte. Returns the length of the whole text, which is
  `buf.len` or more when the text did not fit, or a value below 0 when `snprintf` fails. A
  `precision` below 0 gives 6 digits. Ownership: none.
- `digit_value`: the value of `ch` as a digit in bases up to 36 (`'7'` is 7, `'b'` and `'B'`
  are 11), or `-1`.
- `is_digit` `0-9`; `is_alpha` `A-Z` and `a-z` (not `_`); `is_alnum` either; `is_space` space,
  `\t`, `\n`, `\x0b`, `\x0c`, `\r`; `is_hex` `0-9`, `a-f`, `A-F`. ASCII only: every byte at or
  above `0x80` classifies as `false`.

```fort
import std.str;

// Splits "name=123"; false on malformed input, in which case *name and *value are unchanged.
fn parse_binding(string line, string mut* name, i64 mut* value) bool {
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
fn keep(string name) string own {
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
fn create() str_buf
fn with_cap(u64 cap) str_buf
fn free(str_buf mut* b) void
fn drop(str_buf mut b) void
fn reserve(str_buf mut* b, u64 extra) void
fn cap(str_buf* b) u64
fn push(str_buf mut* b, char ch) void
fn push_byte(str_buf mut* b, u8 v) void
fn append(str_buf mut* b, string s) void
fn append_bytes(str_buf mut* b, u8@ src) void
fn append_i64(str_buf mut* b, i64 v) void
fn append_u64(str_buf mut* b, u64 v) void
fn clear(str_buf mut* b) void
fn truncate(str_buf mut* b, u64 len) void
fn view(str_buf* b) string
fn bytes(str_buf* b) u8@
fn take(str_buf mut* b) string own
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
- `free`: `drop(move(*b))`. The `move` leaves `str_buf{}` in `*b` (D17.6), and `drop` frees the
  storage; calling it twice, or on `str_buf{}`, is harmless because `del` of a zero span is a
  no-op.
- `drop`: `del(b.data)` of a buffer that the caller moves in (`strbuf.drop(move(b))`). It holds
  the one release of the type, and `free` reaches it. Ownership: takes the buffer.
- `reserve`: ensures `data.len >= len + extra`, growing per the policy. Ownership: none.
- `cap`: `b->data.len`, the bytes the storage holds, so `cap(b) - b->len` is the room a further
  `append` uses without growing. It never shrinks except at `free`, and `create` answers 0. This
  is the call a program that measures its own allocation uses, so that it reads no field of the
  struct. Ownership: none.
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

// Ends a buffer in an allocation of `new` that the caller moves in (1.3).
fn buf_drop_made(strbuf.str_buf mut* own p) void {
    strbuf.str_buf kept = move(*p);
    del(p);
    strbuf.drop(move(kept));
}

// Formats "    mov <reg>, <imm>\n". Ownership: the caller dels the result.
fn mov_imm(string reg, i64 imm) string own {
    strbuf.str_buf mut* own b = new(strbuf.str_buf);
    defer buf_drop_made(move(b));
    strbuf.reserve(b, 32);
    strbuf.append(b, "    mov ");
    strbuf.append(b, reg);
    strbuf.append(b, ", ");
    strbuf.append_i64(b, imm);
    strbuf.push(b, '\n');
    return strbuf.take(b);   // an own rvalue flows into the own return type (D17.5)
}

fn emit_mov(i32 fd) bool {
    string own line = mov_imm("rax", 42);
    defer del(line);
    return io.write_all(fd, cast(line[..], u8@));   // a view of line is written (1.3)
}
```

### 2.7 `std.vec`

A growable sequence of `i64`, and the pattern for every other element type.

```fort
struct int_vec {
    i64 mut@ own items;   // slots the vector owns; items.len is the cap
    u64 len;              // slots in use; len <= items.len
}
```

The struct is 24 bytes with `items` at offset 0 and `len` at offset 16 (`own` is erased, D17.1);
`{}` is a valid empty vector. It is an owning aggregate (D17.7): functions take it by pointer and
`int_free` releases it. The live elements are `v.items[..v.len]`. Indexing `v.items[i]` with
`v.len <= i < v.items.len` is not a runtime error; it reads a zero or stale slot, so code that
wants a bounds check indexes the live span.

```fort
fn int_create() int_vec
fn int_with_cap(u64 cap) int_vec
fn int_free(int_vec mut* v) void
fn int_drop(int_vec mut v) void
fn int_reserve(int_vec mut* v, u64 extra) void
fn int_push(int_vec mut* v, i64 x) void
fn int_pop(int_vec mut* v) i64
```

- `int_create`, `int_with_cap`, `int_free`, `int_drop`, `int_reserve`: exactly as their `strbuf`
  counterparts, counting elements instead of bytes and using the same growth policy (16, then
  doubling). Growth copies the elements to fresh storage with an element loop, then `del`s the
  old span and stores the new one (`del(v->items); v->items = move(bigger);`, D17.11), which is
  what makes the file usable as a template. Ownership: `int_drop` `del`s the slots of a vector
  that the caller moves in; `int_free` moves the vector into `int_drop` and leaves `{}`.
- `int_push`: appends one element, growing as needed. `int_pop`: removes and returns the last
  element; panics with `"vec.int_pop: empty"` on an empty vector.

This module is the non-generic container pattern (D15). To hold `token` values, copy `int_vec`
and its seven functions, replace `i64` with `token` and the prefix `int_` with `token_`: about
forty lines, type-checked like any other code. The module has no vector of `void*`: no cast gives
a type to a `void*` (D3.14), so a pointer that went in as a `void*` could not come back as a
typed pointer. A vector of pointers is a typed copy of the pattern. For pointers to elements that
belong to someone else (an arena, a fixed array, an owner that outlives the vector), copy the file
with the slot type `node mut* mut@ own items;`: `node_push(node_vec mut* v, node mut* n)` stores
the view, `node_pop` returns `node mut*`, and `node_drop` never touches a pointee. A typed handle
is the other form: an `int_vec` of indices into a typed span that the caller owns. When the
vector is to own its elements, copy the file with the slot type `node mut* own mut@ own items;`
instead, an owned span of owned nodes (D17.2):
`node_push(node_vec mut* v, node mut* own n)` copies the index into a local and empties the slot
first, `u64 k = v->len; del(v->items[k]); v->items[k] = move(n);` (a parameter is an `own`
lvalue, D17.5). The `del` frees nothing, because a fresh slot is zero at run time (D10.2), but
the compiler cannot know that inside `node_push`; the local `k` gives both elements one key
(D17.11). `node_pop` returns `node mut* own` with `return move(v->items[v->len]);` (the slot is
reached through `mut` storage, D17.6, and is left `null`); the growth loop moves each element
with `del(bigger[i]); bigger[i] = move(v->items[i]);`; `node_drop` `del`s every live element
before the span, and `node_free` moves the vector into it; and a range loop over the live span
lends,
`for (node mut* n : v->items[..v->len])` (D17.10).

```fort
struct node { i64 value; }

// A vector of views of nodes: a typed copy of int_vec with the slot type node mut*.
struct node_vec {
    node mut* mut@ own items;
    u64 len;
}

// A read through a pointer parameter: the owner is no local owner here (D17.21).
fn value_of(node* p) i64 {
    return p->value;
}

// Ends a vector in an allocation of `new` that the caller moves in (1.3): node_drop, the typed
// copy of int_drop, frees the slots.
fn node_drop_made(node_vec mut* own p) void {
    node_vec kept = move(*p);
    del(p);
    node_drop(move(kept));
}

fn demo() i64 {
    node_vec mut* own stack = new(node_vec);  // the calls take the vector by pointer (1.3)
    defer node_drop_made(move(stack));
    node mut* own n = new(node);              // new lands in an own place (D17.3, D17.8)
    defer del(n);
    n->value = 7;
    node_push(stack, n);                      // lends n; the vector never frees it
    node mut* top = node_pop(stack);          // the type that went in comes back
    return value_of(top);
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
fn create() str_map
fn free(str_map mut* m) void
fn drop(str_map mut m) void
fn put(str_map mut* m, string key, i64 val) bool
fn get(str_map* m, string key, i64 mut* out) bool
fn has(str_map* m, string key) bool
fn remove(str_map mut* m, string key) bool
fn count(str_map* m) u64
```

- `create`: `str_map{}`; allocates nothing. `free`: `drop(move(*m))`. The `move` leaves
  `str_map{}` in `*m` (D17.6), and `drop` frees the table; keys are untouched, and a second call
  is harmless. `drop`: `del(m.entries)` of a map that the caller moves in
  (`strmap.drop(move(m))`). Ownership: the caller calls `free`, or passes the map to `drop`.
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

To map names to pointers, keep the pointers in a span beside the map and store the index. The
table copies the vector pattern with the slot type `sym mut*` (2.7), because no container holds
a `void*` that a cast could type again (D3.14). A struct that holds two containers is itself an
owning aggregate (D17.7), so it is passed by pointer and gets a `free` of its own:

```fort
import std.strmap;

struct sym { string name; i64 offset; }

struct sym_tab {
    strmap.str_map index;     // name to position in syms
    sym mut* mut@ own syms;   // sym* in declaration order, owned by whoever declared them
    u64 len;
}

fn declare(sym_tab mut* t, sym mut* s) bool {
    if (strmap.has(&t->index, s->name)) {
        return false;
    }
    if (t->len == t->syms.len) {
        sym mut* mut@ own bigger = new(sym mut*, 2 * t->len + 4);
        for (u64 mut i = 0; i < t->len; i += 1) {
            bigger[i] = t->syms[i];
        }
        del(t->syms);
        t->syms = move(bigger);
    }
    strmap.put(&t->index, s->name, cast(t->len, i64));
    t->syms[t->len] = s;
    t->len += 1;
    return true;
}

fn lookup(sym_tab* t, string name) sym* {
    i64 mut i = 0;
    if (!strmap.get(&t->index, name, &i)) {
        return null;
    }
    return t->syms[cast(i, u64)];
}

fn symtab_free(sym_tab mut* t) void {
    strmap.free(&t->index);
    del(t->syms);   // the sym values are borrowed and stay alive
    t->len = 0;
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

fn f64_bits(f64 x) u64
fn f64_from_bits(u64 bits) f64
fn f32_bits(f32 x) u32
fn f32_from_bits(u32 bits) f32
fn f64_inf() f64
fn f64_nan() f64
fn is_nan(f64 x) bool
fn abs_i32(i32 x) i32
fn abs_i64(i64 x) i64
fn min_i32(i32 a, i32 b) i32
fn max_i32(i32 a, i32 b) i32
fn min_i64(i64 a, i64 b) i64
fn max_i64(i64 a, i64 b) i64
fn min_u64(u64 a, u64 b) u64
fn max_u64(u64 a, u64 b) u64
fn min_f64(f64 a, f64 b) f64
fn max_f64(f64 a, f64 b) f64
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

fn is_negative_zero(f64 x) bool {
    return math.f64_bits(x) == 0x8000000000000000;
}
```

### 2.10 `std.sort`

A typed span sorted in place, in fort, and the pattern for each other element type (D13.2).

```fort
fn sort_i64(i64 mut@ items, fn (i64*, i64*) i32 cmp) void
fn sort_u64(u64 mut@ items, fn (u64*, u64*) i32 cmp) void
fn cmp_i64(i64* a, i64* b) i32
fn cmp_u64(u64* a, u64* b) i32
```

- `sort_i64`, `sort_u64`: order the elements of `items` in place, so that `cmp` answers zero or
  less for every adjacent pair. The sort is written in fort and makes no C call. The order is
  **not stable**: two elements that the comparator calls equal come out in an unspecified order.
  Each panics with `"sort.sort_i64: null comparator"` or `"sort.sort_u64: null comparator"` when
  `cmp` is `null`. A call of a null function pointer is the runtime error `null call` (D3.10), and
  the panic comes before each call: the test
  comes before the count, so a span of 0 or 1 elements with a `null` comparator panics too. A
  span carries its length (D3.5), so the caller cannot pass a count that disagrees with the
  storage, and the parameter `T mut@` says that the elements must be mutable. Ownership: none;
  an `own` span argument lends (D17.4).
- `cmp_i64`, `cmp_u64`: ready-made ascending comparators, `-1`, `0` or `1`. `cmp_u64` compares
  unsigned, so it orders `0xFFFFFFFFFFFFFFFF` last where `cmp_i64` on the same bytes orders it
  first. A qualified function name is a value (D3.10), so a call reads
  `sort.sort_i64(xs, sort.cmp_i64)`. A descending order is a comparator of the caller's own with
  the two arguments exchanged.

**The comparator.** Its type is `fn (T*, T*) i32` for the element type `T`: it reads two
elements through typed pointers, with no cast. It answers a negative `i32` when the element at
`a` sorts before the element at `b`, `0` when the two tie, and a positive one when it sorts
after. It must be a total order and it must be consistent between calls; it must not change the
elements, and it must not call the sort on the same span. When it breaks this, the order of the
result is unspecified, and each element of the input is still in the span once. A comparator may
`panic` (D12.2): the span is left half sorted, and the failure path runs as everywhere else.

**Each other element type.** fort has neither generics nor overloading (D8.3, D15), so this
module is the pattern of `std.vec`: to sort `point` values, copy `sort_i64`, replace `i64` with
`point` and the prefix with `point_`. The comparator then takes `point*` and reads the fields.
The module has no sort of `void*` elements and no sort over `libc.qsort`: the comparator of
`qsort` reads each element through a cast from a `void*`, and no cast gives a type to a `void*`
(D3.14). To order data that a pointer or an owner holds, sort a typed span of pointers with a
typed copy, or sort an `int_vec` of indices with a comparator that reads the data of the caller.
The elements of `sort_i64` and `sort_u64` own nothing. A typed copy for an element type that
holds an owner moves each element with `move` (D17.5), as the growth loop of `std.vec` does; the
simpler way orders views of owned data, pointers or indices, and leaves the owner where it is.

```fort
import std.sort;

struct point {
    i64 x;
    i64 y;
}

// The comparator of point_sort, a copy of sort.sort_i64 in the module of the caller with i64
// replaced by point; the copy is not shown.
fn by_x(point* p, point* q) i32 {
    if (p->x < q->x) {
        return -1;
    }
    if (p->x > q->x) {
        return 1;
    }
    return 0;
}

fn order(point mut@ ps, i64 mut@ xs) void {
    point_sort(ps, by_x);                 // the copy of sort_i64 for point
    sort.sort_i64(xs, sort.cmp_i64);
}
```

### 2.11 `std.rt`

The runtime (D13.1, D13.2): process start and exit, allocation, the failure paths and the print
buffers, the float text of 2.12 among them, written in fort over `std.libc` and `std.strbuf`.
`toolchain.md` 5 fixes what each entry point does and
section 3 below names the four the rest of the library calls; `sys` and `io` are its only
importers inside the library, and everything else in it is an implementation detail (1.1).
Its names are mangled like any module's (`std.rt.flush`, D9.7); the compiler knows them, because
a builtin lowers to a call of one (D12.2), and calls them like any other fort function.

Its allocation entry point answers `void mut*`, storage of `std.heap.take` (2.16), with no `own`:
`take` gives a plain pointer, and no cast adds the mark (D3.14). `toolchain.md` 5.1 is the list of
entry points the compiler holds (D12.2) and it declares the signature. No cast gives a type to a
`void*` (D3.14), so a caller that calls `alloc` directly gives the result only to C, to `release`
or to `free`, whose parameter is `void*`; the compiler types the result of a `new` itself.

### 2.12 The float text of `std.rt`

The float text of D11.7, to a descriptor or to a `str_buf`. These four functions stood in a
module of their own, `std.rt_float`, until T-132 folded that module into `std.rt`; they are
declarations of `std.rt` and this section is a part of 2.11. Deleting `std/rt_float.ft` took a
pin of its own, because the pinned compiler that built HEAD read that file name for any closure
holding a float (D18.1, `notes/compiler.md` 8).

```fort
fn print_f32(i32 fd, f32 v) void
fn print_f64(i32 fd, f64 v) void
fn append_f32(strbuf.str_buf mut* b, f32 v) void
fn append_f64(strbuf.str_buf mut* b, f64 v) void
```

- `print_f32`, `print_f64`: append the text of `v` to the buffer of `fd` (D11.5, D11.7). They are
  the two float entry points of D18.1: the print family calls one of them per float argument
  (D12.2), and a program may call them itself.
- `append_f32`, `append_f64`: append the same bytes to `b`. One formatter produces the text for
  both forms, so the bytes `append_f64(&b, v)` adds are the bytes `print_f64(fd, v)` writes, for
  every `v`, and the same holds of the `f32` pair. The text is D18.2's shortest decimal that
  reads back in the argument's own type, laid out by D18.3. An `f32` is never widened to an `f64`
  first, because the digits depend on the argument's own type (D18.1). Ownership: none; `b` keeps
  its storage and grows by the policy of 2.6.
- The two `append` functions are library functions and not entry points: no call the compiler
  emits names them, so `toolchain.md` 5.1's list stays the two printers (D18.4).
- `std.rt` holds the two `append` functions, rather than `std.strbuf` holding them, so that one
  formatter serves both forms and the two cannot disagree over a value (D18.1). `std.rt`
  imports `std.strbuf` for them, and `std.rt` is in every import closure (D9.10), so every
  program now compiles `std.strbuf` and `std.mem` as well, and every program carries the float
  text whether it prints a float or not.
- Everything else these declarations need -- the decimal, the search of D18.2 and the layout of
  D18.3, which `toolchain.md` 5.1 describes -- is an implementation detail (1.1).

### 2.13 `std.net`

A TCP listener and a TCP connection over `std.libc` (D13.2). IPv4 only, and no name resolution:
`connect` takes a dotted quad. `getaddrinfo` is absent because it answers with a list the caller
must release with `freeaddrinfo`, and no `extern` signature can carry that ownership across the
boundary (D17, D9.8).

A descriptor is not an owning value. D17 has no rule that releases one, so **the caller closes
every descriptor these functions answer with**, and the idiom is `defer libc.close(fd);` beside
the call that opened it. `listen`, `accept` and `connect` answer with the descriptor, or with
**`-1`** and the reason in `sys.errno()`, which is the sentinel 1.2 names for a descriptor that
could not be opened and the form `io.open_read` already takes (D13.3). A call that fails after it
opened a descriptor closes that descriptor before it answers, so a failure leaks nothing, and it
writes `errno` back after that `close(2)`, so the value a caller reads is the failing call's.

```fort
// <sys/socket.h> and <netinet/in.h>, Linux x86-64 values.
i32 AF_INET = 2;
i32 SOCK_STREAM = 1;
i32 SOL_SOCKET = 1;
i32 SO_REUSEADDR = 2;
u32 INADDR_ANY = 0;

struct sockaddr_in {
    u16 family;
    u16 port;
    u32 addr;
    u8[8] zero;
}
```

```fort
fn hton16(u16 v) u16
fn hton32(u32 v) u32
fn parse_ipv4(string text, u32 mut* out) bool
fn listen(u16 port, i32 backlog) i32
fn accept(i32 fd) i32
fn connect(string host_ipv4, u16 port) i32
fn local_port(i32 fd, u16 mut* out) bool
```

- `sockaddr_in`: the IPv4 socket address of `<netinet/in.h>`, which `bind(2)` and `connect(2)`
  read through a pointer. Struct layout is the C ABI's (D3.8), so the four fields must land where
  C puts them, and `sizeof` must be C's 16. `zero` is written `u8[8]` and not `u64` because C's
  field is a character array of 8, which asks 1 byte of alignment where a `u64` asks 8. Nothing
  holds that choice: fort has no `alignof` (`type-system.md` 8), so `run/ffi/013` holds C's
  `_Alignof` against 4 and nothing against fort's, and a `u64` there leaves the size, all four
  offsets and the C alignment true. `port` and `addr` hold network byte order.
- `hton16`, `hton32`: host to network order for a 16-bit and a 32-bit value. Each is its own
  inverse, so the same function reads a port or an address back. The bytes reverse, which is what
  a little-endian target needs (D19.2). Each byte is masked to 8 bits and moved by a shift, and
  `|` joins the parts, so no step traps in the checked mode (D11.1, D6.2). Ownership: none.
- `parse_ipv4`: reads a dotted quad (`"127.0.0.1"`) and delivers the address in network byte
  order, ready for a `sockaddr_in.addr` field. Exactly four fields, each one to three decimal
  digits with a value of at most 255, no space, no sign and no text after the fourth field; a
  leading zero is decimal and not octal. Returns `false` and leaves `*out` unchanged for anything
  else (D13.3). Ownership: none.
- `listen`: a listening TCP socket bound to `port` on every interface (`INADDR_ANY`), with the
  backlog of `listen(2)`, or `-1`. `SO_REUSEADDR` is set before the bind, so a
  restart inside the TIME_WAIT window still binds. Port 0 asks the kernel for a free port, which
  `local_port` then reads back; it is what a test uses, so that two programs never choose one
  number. Ownership: none in the types; the caller closes the descriptor.
- `accept`: waits for the next connection on a listening descriptor and answers with a descriptor
  for it, or with `-1`. The peer's address is not delivered. Ownership: none in the
  types; the caller closes the descriptor.
- `connect`: a TCP connection to `host_ipv4` on `port`, or `-1`. A `host_ipv4` that `parse_ipv4`
  refuses makes no system call: `connect` sets `errno` to `EINVAL` itself, so that one test
  answers every failure. Ownership: none in the types;
  the caller closes the descriptor.
- `local_port`: the port `fd` is bound to, in host order, through `getsockname(2)`. Returns
  `false` and leaves `*out` unchanged on failure, with the reason in `sys.errno()`. Ownership:
  none.

```fort
import std.io;
import std.net;

fn serve_one(u16 port) bool {
    i32 server = net.listen(port, 16);
    if (server < 0) {
        return false;
    }
    defer io.close(server);
    i32 session = net.accept(server);
    if (session < 0) {
        return false;
    }
    defer io.close(session);
    u8[4096] mut buf = {};
    i64 n = io.read(session, buf[..]);
    return n > 0 && io.write_all(session, buf[0..cast(n, u64)]);
}
```

### 2.14 `std.os`

The per-target facts of the standard library (D13.2). Each target has its own source, and both
sources declare the same names, but for one name of each source that the other source does not
declare: `PROC_SELF_EXE` on Linux and `_NSGetExecutablePath` on Mac. Each name is exported, as
each module-level name is (D9.6). The ctest `std_os_shared` holds five things: the names, the
text that both sources share, the text of each item above the shared part with the comment lines
directly above it, but for the items whose text is per target, the imports, and that each line
above the shared part is a blank line, a comment, an import or a line of an item, so a `$if`
block there is an error.
`TARGET` is the target triple of the standard root. A compiler
built with that root stores it as its built target (D14.1). `exe_path` gives the path of the
running binary: Linux reads the symbolic link `/proc/self/exe` with `libc.readlink`. Mac calls
dyld's `_NSGetExecutablePath` and resolves the answer with `real_path` when the file still
exists at that path. `_NSGetExecutablePath` is not a C library function, so `std/darwin/os.ft`
declares it, and `std.libc` does not. `real_path` gives the canonical form of a path with
`libc.realpath`: an absolute path with symbolic links, `.` and `..` resolved. `open_nonblocking`
and `file_mode` give the type of a file without a read: the open of a FIFO does not wait for a
writer, and `fstat(2)` gives the type bits. `path_mode` gives them from a path with `stat(2)`,
which opens nothing.

```fort
string TARGET = "x86_64-linux-gnu";          // std/linux/os.ft
string TARGET = "arm64-apple-macosx11.0.0";  // std/darwin/os.ft

fn exe_path(strbuf.str_buf mut* out) bool;
fn real_path(string path, strbuf.str_buf mut* out) bool;
fn open_nonblocking(string path) i32;
fn file_mode(i32 fd, u32 mut* mode) bool;
fn path_mode(string path, u32 mut* mode) bool;
fn make_fifo(string path, u32 mode) bool;
fn set_umask(u32 mask) u32;

u32 PERMISSION_BITS = 0o7777;
u32 SET_ID_BITS = 0o6000;
u32 UMASK_BITS = 0o777;
fn error_text(i32 code, strbuf.str_buf mut* out) void;
fn can_access(string path, i32 mode) bool;
fn make_dir(string path, u32 mode) bool;
fn set_mode(string path, u32 mode) bool;
fn make_symlink(string target, string link_path) bool;
fn remove_file(string path) bool;
fn remove_dir(string path) bool;
fn remove_at(i32 dir_fd, string name, bool directory) bool;
fn rename(string old_path, string new_path) bool;
fn read_link(string path, strbuf.str_buf mut* out) bool;
fn current_dir(strbuf.str_buf mut* out) bool;
fn make_temp_dir(string template, strbuf.str_buf mut* out) bool;
fn list_dir(string path, strbuf.str_buf mut* out) bool;
fn list_dir_fd(i32 dir_fd, strbuf.str_buf mut* out) bool;

struct dir_entry {
    string name;
    u8 kind;
}
fn next_entry(string list, u64 mut* at, dir_entry mut* entry) bool;
```

`exe_path` appends the path to `out` and returns true. It returns false and leaves `out`
unchanged when the system cannot give the path. The caller owns `out`.
`real_path` appends the canonical form of `path` to `out` and returns true. It returns false and
leaves `out` unchanged when `realpath(3)` fails. It
allocates the buffer of `libc.PATH_MAX` bytes that `realpath` writes, with `new`, and frees it
before it returns. This is the owning form of `realpath` that D17.13 describes: the `std.libc`
declaration takes a buffer and returns a view of it.
`open_nonblocking` opens `path` to read with `O_NONBLOCK` and returns the descriptor, or -1 with
errno set. `file_mode` writes the low 16 bits of the `st_mode` of the file that `fd` names to
`mode`, its type bits and its permission bits, and returns true. It returns false with errno set
when `fstat(2)` fails. `std.os` declares `fstat` itself and `std.libc` does not, so only a
program that imports `std.os` binds the C symbol (D9.8). The buffer is 18 words of 8 bytes, with
the alignment of C, and not a struct, because a struct would give the symbol a type that a
program cannot write again. `st_mode` is a u32 at byte 24 on Linux x86-64 and a u16 at byte 4 on
Darwin arm64. `path_mode` writes the same bits of the file that `path` names, after each
symbolic link, and returns true; it returns false with errno set when `stat(2)` fails. It opens
nothing, so a FIFO that has no writer and a file that the process cannot read give their mode.
`std.os` declares `stat` for the reason of `fstat`, with the same buffer.
`make_fifo` makes a FIFO at `path` with `mkfifo(3)` and the permission bits `mode`, less the
umask; an entry at `path` gives `EEXIST`. A `mode` with a bit outside `PERMISSION_BITS`, or with
a bit of `SET_ID_BITS` (`S_ISUID` and `S_ISGID`), makes nothing and gives `EINVAL` on both
targets: the two C libraries read a type bit in different ways, and the Darwin kernel refuses
`S_ISGID` with `EPERM` in a directory whose group is no group of the process. After a success
errno stays as it was. `set_umask` sets the file mode creation mask of the process to the bits
of `mask` in `UMASK_BITS` with `umask(2)` and returns the bits in `UMASK_BITS` of the mask that
was there before; it does not fail and does not change errno. Linux keeps only these bits of a
mask, and Darwin keeps the bits `0o7777`, also of a mask that the parent process set, so
`set_umask` drops the others on both targets. The two functions are per target, because
`mode_t` is a `u32` on Linux and a `u16` on Mac (D9.8).

The functions of the second group are the same on both targets. Each one that takes a path
copies it into a NUL-terminated buffer with `str.to_cstr`, and frees it before it returns. Each
one that returns `bool` returns `true` when its C call succeeds, and `false` with the errno of
that call in `sys.errno()` when the call fails; a function with an `out` buffer then leaves it
unchanged.
`error_text` appends the `strerror(3)` text of an errno value to `out`, for example `No such
file or directory` for `ENOENT`. A value that the system does not have gives a text that names
the value. glibc frees the storage of such a text at the next call with another unknown value,
and the Darwin C library writes it into one static buffer, so `error_text` copies the text before
it returns. errno stays as it was: the Darwin `strerror` sets `EINVAL` for an unknown value, and
`error_text` puts the old value back, as glibc keeps it.
`can_access` asks `access(2)` whether the process can reach `path` for `mode`, such
as `libc.R_OK`. `make_dir` makes a directory with `mkdir(2)` and the permission bits `mode`,
less the umask; an entry at `path` gives `EEXIST`. `set_mode` gives the file `path`, after each
symbolic link, the permission bits `mode` with `chmod(2)`; the bits of `mode` outside
`PERMISSION_BITS` do not count. A `mode` with a bit of `SET_ID_BITS` changes nothing and gives
`EINVAL` on both targets: the Darwin `chmod` clears `S_ISGID` for a file whose group is no group
of the process and leaves `EPERM` in errno after a success, because it tries again without
`S_ISGID` after an `EPERM`. Without these bits, a success keeps errno. `make_symlink` makes a
symbolic link at `link_path` whose target is the text `target` with `symlink(2)`; no file need
stand at `target`, and an entry at `link_path` gives `EEXIST`. `remove_file` removes an entry that
is no directory with `unlink(2)`, and a symbolic link goes, not its target. `remove_dir` removes an
empty directory with `rmdir(2)`. `remove_at` removes the entry `name` of the directory that `dir_fd`
names with `unlinkat(2)`: an empty directory when `directory` is true, and else an entry that is no
directory. `rename` gives a file a new name with `rename(2)` (2.2).
`read_link` appends the target of the symbolic link `path` to `out`; a path that is no link
gives `EINVAL`. `current_dir` appends the absolute path of the working directory with
`getcwd(3)`; a working directory that was removed gives `ENOENT`. Both read into a buffer of 256
bytes first and double it while the path does not fit, up to 65536 bytes; a longer path gives
`ERANGE`. `make_temp_dir` makes a new directory with `mkdtemp(3)` from `template`, whose last six
bytes are `XXXXXX`, and appends its path to `out`. glibc refuses a template that does not end in
six X with `EINVAL`, and the Darwin C library takes it.
`list_dir` appends each entry of the directory `path` but `.` and `..` to `out`, in the order of
`readdir(3)`: one byte of its kind, a `libc.DT_` value, then its name, then a NUL. A file system
that does not record kinds gives `libc.DT_UNKNOWN`. For each record it checks `reclen` against
the sizes of `dirent_head` and `dirent`, copies `reclen` bytes into a zero local `dirent`, and
reads the name there with `str.from_cbuf` (2.2, D9.8); a record whose size does not fit gives
`EINVAL`. errno is 0 before each `readdir`, so an error of `readdir` is told from the end of the
stream. `list_dir_fd` does the same for the directory that the open descriptor `dir_fd` names: it
reads through a duplicate from `dup(2)` and `fdopendir(3)`, so `dir_fd` stays open, and a
symbolic link that replaces the directory at its path does not change what it reads. The
duplicate shares the position of `dir_fd`, so `list_dir_fd` moves `dir_fd` to the first entry
with `lseek(2)` before the `dup`, and a second call gives each entry again. A descriptor whose
position does not move gives the errno of `lseek` before the `dup`. It reads the type of
`dir_fd` with `fstat(2)` first: a descriptor of a file of another type gives `ENOTDIR`, and its
position does not change.
`next_entry` reads the entry at `*at` of such a list into `*entry`, moves `*at` to the next
entry and returns `true`; it returns `false` at the end of the list, and for an entry that no NUL
ends, with `*at` and `*entry` unchanged, so it reads only inside the list. A caller starts with
`*at` at 0. The name is a view of the list. Ownership: the buffers belong to the caller; the
name of a `dir_entry` is a view (D17.3).

### 2.15 `std.digest.sha256`

The SHA-256 digest of FIPS 180-4. The module is the same on both targets and imports nothing. A
caller hashes a message in parts with `init`, `update` and `finish`, or in one call with `hash`.
The digest is the 32 bytes of FIPS 180-4 6.2.2, with the most significant byte of H0 first.

```fort
u64 BLOCK = 64;
u64 SIZE = 32;

struct state {
    u32[8] h;
    u8[64] block;
    u64 used;
    u64 total;
}

fn init(state mut* s) void;
fn update(state mut* s, u8@ data) void;
fn finish(state mut* s) u8[32];
fn hash(u8@ data) u8[32];
```

`init` sets `s` to the state of an empty message: `h` holds the initial hash value of FIPS 180-4
5.3.3 and the other fields are zero. `update` adds the bytes of `data` to the message. A caller
may split a message at any byte, and the digest does not change. `update` panics when the
message length in bits cannot fit in `u64`, the limit of FIPS 180-4 5.1.1. `finish` pads the
message (FIPS 180-4 5.1.1), compresses the last one or two blocks and returns the digest. After
`finish`, a caller calls `init` before it gives `s` another message. `hash(data)` returns the
digest that `init`, one `update` with `data` and `finish` give. `state` holds no owning field, so
it needs no `free`, and every function takes views (D17).

```fort
import std.digest.sha256;

u8[32] d = sha256.hash(cast("abc", u8@));   // ba7816bf...f20015ad

sha256.state mut s = {};
sha256.init(&s);
sha256.update(&s, cast("ab", u8@));
sha256.update(&s, cast("c", u8@));
u8[32] same = sha256.finish(&s);            // the same 32 bytes as d
```

### 2.16 `std.heap`

The allocator of the runtime (D10.3, D10.9). `std.rt` imports it, so each import closure holds it
(D9.10), and the compiled code reaches it through `std.rt.alloc` and `std.rt.release`
(`toolchain.md` 5.1). The module is the same on both targets and imports `std.libc`. It takes
address ranges and storage from the system with `mmap`, `mprotect` and `madvise`, and it calls no
allocator of C. One thread uses it, so it has no lock.

```fort
u64 LIMIT_BYTES = 140737488355328;

fn take(u64 size) void mut*
fn drop(void* p) bool
fn is_start(void* p) bool
fn can_take(u64 size) bool
fn live_bytes() u64
fn kept_bytes(u64 size) u64
```

- `take`: gives the storage of a new allocation of `size` bytes, or of 1 byte when `size` is 0.
  The storage holds zero bytes and starts at a multiple of 16. No byte of it is a byte of an
  earlier allocation, live or dead (D10.9 rule 1). Returns null when `size` is above
  `LIMIT_BYTES` (2^47), when the allocator has no address range left, or when the system refuses
  the storage. `std.rt.alloc` reports each null as the runtime error `out of memory` (D10.9
  rule 5).
- `drop`: releases the allocation that starts at `p` and returns `true`. Returns `false`, and
  changes nothing, when no live allocation of `take` starts at `p`: `p` is null, an address inside
  an allocation, an address of dead storage, or an address that `take` did not give.
  `std.rt.release` reports a `false` for a `p` that is not null as the runtime error `invalid
  release` (D10.9 rule 4). `drop` reads no byte at `p`: it tests the address against the state of
  the allocator. Its parameter is a `void*`, so it is a boundary function (D9.8). The test reads
  the address and not the allocation that the pointer came from. Two small allocations can stand
  side by side, so the address one after the last byte of one is the start of the next, and a
  `del` of an owner of that address releases the next allocation. Only an owner that a trusted
  module forges (D9.8), or a store through one of the routes below, gives such an address to
  `del`: no other rule of the language makes an owner of it (D3.14).
- `is_start`: reports whether a live allocation of `take` starts at `p`, and changes nothing. It
  is the test of `drop` with no release: the two functions share the test, so they give the same
  answer for each address. `regrow` makes this test of the `.ptr` of its span before it takes
  storage (D10.9 rule 4, D12.2). `is_start` reads no byte at `p`. Its parameter is a `void*`, so
  it is a boundary function (D9.8).
- `can_take`: reports whether `take(size)` gives storage now. It gives no storage to an
  allocation, and `live_bytes` does not change. For a large size, and for a small size that the
  open chunk does not hold, the probe needs the chunks that `take` needs: it can map a new
  segment, which stays and is the current segment after the probe, as after a `take`. The probe
  gives the range access and takes the access away again. For a huge size the probe maps the size
  and unmaps it again.
- `live_bytes`: the bytes of the live allocations of `take`. An allocation counts with the bytes
  that the allocator keeps for it: `take` adds them and `drop` subtracts them. Storage that C code
  allocated (`malloc`, `calloc`) does not count. The leak probes of `test/fort` read this count.
- `kept_bytes`: the bytes that the allocator keeps for an allocation of `size` bytes: `size`
  rounded up to 16 bytes and at least 16, or rounded up to 2 MiB when `size` is above 2,048,000
  bytes. `size` is at most `LIMIT_BYTES`.

A program uses `new` and `del`. A module that calls `take` gets a `void mut*`, which no cast gives
a type to (D3.14), and only a trusted module names `drop` and `is_start` (D9.8). These two rules
do not close the allocator to a module outside the trusted set. D9.6 exports each module-level
name, and no rule limits a name of `std.heap` to the trusted set. So such a module can call each
of the 26 other functions of `std.heap` (`room`, `seal` and `drop_huge` among them), and it can
store into each
of the 7 `mut` globals (`segments`, `segment_count`, `current`, `open_segment`, `bump`, `live`
and `holes`). Each such call or store can give the storage of a live or a dead allocation to a
later allocation, or take the access to live storage away. So each such route breaks rule 1 and
rule 2 of D10.9, and the guarantee of D10.7 for a module outside the trusted set does not hold
for a program that takes one. A later decision closes the routes with a rule of visibility. No
file of the repository outside `std/heap.ft` calls one of the 26 functions or names one of the 7
globals.

**Address ranges.** A segment is one mapping of 2^32 bytes (4 GiB) that nothing can access at
first. A chunk is 2 MiB of a segment, a unit is 16 KiB and a granule is 16 bytes. The allocator
has three kinds of allocation:
- A small allocation has at most 2,048,000 bytes. It takes granules of the open chunk at a bump
  pointer, which only moves up. When the open chunk has too few granules left, the allocator
  closes it and opens the next chunk.
- A large allocation is larger and has fewer than 2048 chunks. It takes whole chunks at the
  frontier of the current segment, which only moves up. When the current segment has too few
  chunks left, the allocator maps a new segment, and the rest of the old one takes no later
  large allocation.
- A huge allocation has 2048 chunks or more. It takes a mapping of its own.

No operation moves a bump pointer or a frontier down, the allocator maps no range at a fixed
address, and it unmaps no mapping that an allocation used: the mapping of a dead huge allocation
stays. So the address range of a released allocation takes no later object of the allocator or
of the system while the process runs (D10.9 rule 1). The allocator holds at most 32768 mappings,
its segments and its live huge allocations: when each record is in use, a huge allocation and an
allocation that needs a new segment get no storage, and `can_take` gives `false` for them. The
first `new` of a process needs an address range of 4 GiB, and the runtime makes that `new` for the
arguments of `main`, before `main` runs. A process whose address-space limit is lower, or whose
system refuses that mapping, stops there with `out of memory` (D10.9 rule 5).

**A budget for the life of a process.** No address comes twice, so each allocation spends its
address range for the rest of the process, live or dead. The address range of 2^47 bytes (128
TiB) is the budget: a process gets `out of memory` (D10.9 rule 5) after about 2^47 bytes of
allocation in all, counted as `kept_bytes`, and earlier by the address range that no allocation
uses (chunk 0 of each segment, the rest of a segment that a large allocation skips). On Linux the
end comes earlier, at the 65,530 map entries of `vm.max_map_count` ("Map entries" below): with 4
entries for each segment and 16,384 for the holes, after about 12,000 segments, about 48 TiB. One
compile of the compiler allocates 530.7 MiB in all, so 2^47 bytes hold about 250,000 such
compiles. At 1 GiB of allocation each second the budget lasts about 36 hours. `fort-lsp` runs for
days, so its rate of allocation decides when it gets `out of memory`. A later change ends this
bound: it gives dead storage to a new allocation after a sweep finds no pointer to that storage.

**State.** The state stands in the globals of the module, in the first unit of each segment (one
word for each chunk) and in the first 3 units of each small chunk (the start and the end of each
live allocation, as bits). None stands in the storage of an allocation. So a store through a
view of dead storage changes no state of the allocator (D10.9 rule 1), and `drop` needs no byte
at its argument.

**Give-back.** A release gives dead storage back to the system where it can. It takes the access
to the range away (`mprotect`) and then tells the system to drop the pages (`madvise`). The order
is fixed: a page that the system drops while a read can still reach it reads as zero bytes,
which is no result of D10.9 rule 2. A read of storage that went back stops the program with a
memory fault, and dead storage that did not go back keeps the bytes that it held last. A large
and a huge allocation go back at their release. Small allocations go back in whole units: when 8
more units of a chunk hold no live allocation than its last give-back left, and as a whole chunk
when a closed chunk holds no live allocation. A unit of the open chunk above the bump pointer
never goes back, because a later allocation takes it. The system can refuse to take the access
away. The allocator then tells the system to drop no page, and the storage stays as it is. A
refused call can take the access away from the first part of its range, and a read there stops
the program with a memory fault. Both are results of rule 2. A small chunk whose give-back the
system refused keeps no state that the allocator reads again.

**Holes.** A hole is a run of units that went back between two units that the program can
access. The system keeps map entries for each hole and limits the entries of one process
(`vm.max_map_count` of Linux, 65,530 by default). So the allocator keeps at most 8192 holes: a
give-back that makes one more hole does not run, and its storage stays as it is. A give-back
that makes no new hole, because its units stand beside a hole, runs at the limit too.

At the limit the allocator does not tell the system to drop the pages of such storage either.
The program can still read the storage, and on Linux a page that the system drops while the
program can read it reads as zero bytes (`MADV_DONTNEED`), a third result that rule 2 forbids.
So the memory of the process grows at the limit. Each release whose give-back would make a new
hole keeps the pages that the program stored into, up to `kept_bytes(size)` bytes for each such
allocation and up to 2 MiB for each such small chunk, until the process ends. The growth has no
other bound than the address range. `test/lang/run/stdlib/189` measures it: 16 releases of 4 MiB,
each with a store into each page, keep 14 to 18 times 4 MiB at the limit and less than 3 times
4 MiB below it.

**Map entries.** A segment costs at most 4 map entries, a huge allocation 1, live or dead, and a
hole 2. So the allocator adds at most 4 entries for each segment and each huge mapping, and 2 for
each of at most 8192 holes. The address range holds at most 32768 mappings of 4 GiB or more, so
the bound is 4 × 32768 + 2 × 8192 = 147,456 entries, above the 65,530 of Linux. The limit of the
holes holds only the last term: segments add entries past it. When the process has no map entry
left, the system refuses the call that needs one (ENOMEM). A refused `mmap` or `mprotect` for
new storage is the runtime error `out of memory` (D10.9 rule 5), and a refused give-back leaves
the storage as it is (rule 2).

**Commit charge.** Linux charges a range of a private mapping against its commit limit when the
allocator gives the range write access. After the program stored into the mapping, the charge of
the range stays when the range goes back: `mprotect` with `PROT_NONE` and `madvise` do not give it
back. So the charge of a process grows with each range that it used in the run, live or dead.
Under `vm.overcommit_memory = 2` the system refuses the access to new storage when the charge
reaches its limit (`CommitLimit`), and that `new` is the runtime error `out of memory` (D10.9
rule 5). Under the default mode 0 the charge has no such limit.

## 3. The runtime surface the library relies on

`toolchain.md` owns the runtime: the full entry-point list, the signatures, the print buffers and
process start. `std.rt` is a library module like any other (D13.1, 2.11) and this section names only
what the rest of the library calls of it. The library uses the builtins `new`, `del`, `move`,
`panic`, `assert` and the print family as any program does (D12); the runtime calls behind them are
emitted by the compiler and never named in library source. Beyond that, `sys` and `io` import
`std.rt` and call five of its functions. `toolchain.md` 5.1 fixes the signatures of the four
entry points among them; `reset_buffer` is no entry point, because the compiler emits no call of
it:

```fort
fn args() string@;
fn flush(i32 fd) void;
fn flush_all() void;
fn reset_buffer(i32 fd) void;
fn exit(i32 status) noreturn;
```

- `args`: the `string@` the runtime built from `argv` at process start (D11.6). It is the same
  span `main` receives, so `sys.args()` and `main`'s parameter are equal span for span. The
  result is a view (D17.3): the runtime keeps the storage, and the span carries no `own`.
- `flush`: writes out the runtime's buffer for one descriptor, if it has one, and is a
  no-op otherwise. `io.flush` and `io.dup2` call it, as D11.5 specifies.
- `reset_buffer`: writes out the runtime's buffer for one descriptor and makes it ask `isatty`
  again at the next output, so it fits the file that the descriptor names then (D11.5). `io.close`
  calls it before `close(2)`, and `io.dup2` calls it for its new descriptor.
- `flush_all`: writes out every runtime buffer. The library does not call it; it is
  there for programs that write through `libc.write` after printing (2.2), and the `main` the
  compiler emits calls it at exit (D11.6).
- `exit`: flushes every runtime buffer and exits with `status & 0xFF`; `sys.exit` is a
  call to it.

Three properties of the runtime the library also depends on: `del` frees by the pointer alone,
and its caller gives no length (D10.3, D17.9, used by 1.3); `own` changes no bits (D17.1, D17.3),
and the runtime knows the start of each live allocation, so `del` releases a value of `new` and
`libc.free` releases a value of `malloc` (D10.3, D10.9);
and `new(T, n)` returns zeroed storage (D10.2), which is why fresh `str_buf`, `vec` and `strmap`
slots read as zero and why a fresh `own` slot passes the overwrite check (D17.11).

## 4. Worked example

`wc.ft` reads the file named on the command line and prints its line and word counts, using
only this library and the builtins. Every line is v1 fort.

```fort
import std.io;
import std.str;
import std.sys;

// Prints the line and word counts of the bytes. data is a pointer, so its owner is no local
// owner here (D17.21).
fn count(u8 mut@ own* data) void {
    string text = cast((*data)[..], string);
    u64 mut lines = 0;
    u64 mut words = 0;
    bool mut in_word = false;
    {
        string range_values = text;
        u64 mut range_index = 0;
        while (range_index < range_values.len) {
            char ch = range_values[range_index];
            range_index += 1;
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
    }
    if (text.len > 0 && text[text.len - 1] != '\n') {
        lines++;
    }
    println(lines, " ", words);
}

fn main(string@ args) i32 {
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
    count(&data);
    return 0;
}
```

Notes: `data` is declared `u8 mut@ own mut` so that `&data` has type `u8 mut@ own mut*` (D5.8,
D17.2), the slot type `read_file_bytes` fills, and it starts as `{}` because the callee stores
over it (D17.11); `defer del(data)` is registered before the call, so both later `return` paths
free the bytes (D7.8), and on the failure path `del` of the still-zero span is a no-op (D17.9);
`count` reads the bytes through a pointer, because a read of `data` in `main` after the call
is a use that D17.21 cannot prove;
`text` is a view of the bytes (`(*data)[..]` and a cast between the span families, D17.3, D3.14),
so no second owner exists; the counters are `u64` because `.len` is (D16); and the
`text.len > 0` guard keeps `text.len - 1` from trapping on an empty file (D11.1).

## 5. Not in v1

Deliberately absent, with the idiom to use instead; the language-level list is D15.

- Formatted output beyond the print family: build text in a `str_buf` with `append`,
  `append_i64`, `append_u64` and `rt.append_f64` (2.12), then `io.write_all` or `print` the
  result.
- Generic containers: copy `std.vec` for each element type (2.7); use `str_map` with indices
  for pointer values (2.8).
- Containers that own their elements: copy `std.vec` with a `node mut* own mut@ own` slot type
  and a `free` that `del`s every element (2.7).
- Compile-time leak and use-after-`move` detection (D15, D17.14), outside the leak rule of
  D17.20: the idiom is `defer del` right after the declaration, and the zero value that `move`
  and `del` leave behind (1.3).
- Unicode: strings are bytes (D3.7); the classification functions are ASCII-only.
- Floating-point formatting and parsing beyond what the print family emits (D11.7): the library
  gives that one text and no other, to a descriptor or to a `str_buf` (2.12); a program that needs
  a width, a precision or a float from text writes its own conversion or calls C through
  `std.libc`.
- Networking, directories, time, line-at-a-time input: call libc through your own `extern`
  declarations following the `std.libc` conventions, or `read_all` and scan.
- Threads and signal handlers: a fort program has one thread (D10.7). No declaration starts a
  thread, and none gives C a fort function as the handler of a signal (D9.8).
