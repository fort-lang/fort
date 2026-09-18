# fort standard library

The standard library modules of decision D13.2 live here as `.ft` sources; `spec/stdlib.md`
specifies them. `std.libc` (thin libc `extern` declarations and the open, lseek and errno
constants), `std.rt` (the runtime itself: it starts and ends the process, allocates, writes the
failure lines, and formats and buffers what the print family writes, over `std.libc` and
`std.strbuf`;
`spec/toolchain.md` 5 owns its entry points and `spec/stdlib.md` 3 owns the four that the rest
of the library calls),
`std.mem`, `std.str`, `std.sys`, `std.strbuf` (the growable buffer), `std.vec` (`ptr_vec`,
`int_vec` and the non-generic container pattern), `std.strmap` (the open-addressing table),
`std.io` (descriptors, whole files and streams), `std.math` (the integer limits, the float
bit casts and the per-type `abs`, `min` and `max`), `std.net` (a TCP listener and a TCP
connection, IPv4 only) and `std.os` (the target triple and the path of the running binary) are
written.

`std.libc`, `std.net` and `std.os` have one source for each target, in `std/linux/` and
`std/darwin/`. The other modules stand in `std/` and serve both targets.

`std.math` holds f64 and f32 functions. Product tests use this current standard library.
The C compiler reads only the standard library of bootstrap-0.

`std.rt` is the only runtime there is. A `print`, a `new` and a `panic` are calls to its entry
points, which the compiler emits by their mangled names (D9.7, D12.2), and the module declares no
C symbol of its own: what it needs of the operating system it reaches through `std.libc`. Every
import closure holds it, so every program carries it (D9.10). The tests under
`test/lang/run/stdlib` call an entry point directly beside the builtin the compiler lowers to it,
which is what holds the lowering to the text D11.7 and D11.4 fix.

The build copies `std/*.ft` and `std/<target>/*.ft` into `build/<preset>/std/`, which the
compiler uses as its fallback `--std-dir` (`spec/toolchain.md` sections 1 and 2), so a new module
is visible to `test/lang/run_tests.py` only after `tools/vm build <preset>` has copied it. The
language tests of the library are `test/lang/run/stdlib/`.
