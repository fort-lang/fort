# fort standard library

The standard library modules of decision D13.2 live here as `.ft` sources; `notes/stdlib.md`
specifies them. `std.libc` (thin libc `extern` declarations and the open, lseek and errno
constants), `std.rt` (the runtime itself: it starts and ends the process, allocates, writes the
failure lines, and formats and buffers what the print family writes, all over `std.libc`;
`notes/toolchain.md` 5 owns its entry points and `notes/stdlib.md` 3 owns the four that the rest
of the library calls),
`std.mem`, `std.str`, `std.sys`, `std.strbuf` (the growable buffer), `std.vec` (`ptr_vec`,
`int_vec` and the non-generic container pattern), `std.strmap` (the open-addressing table) and
`std.io` (descriptors, whole files and streams) are written; `std.math` arrives with ticket
T-042.

Two runtimes are live until T-091 retargets the compiler's builtins. A `print`, a `new` and a
`panic` still call `runtime/fort_rt.c`. So `std.rt` also declares the five `fort_rt_*` entry points
that `std.sys` and `std.io` reach it through, and each test under `test/lang/run/stdlib` calls a
fort entry point beside its C counterpart and compares the two. T-091 deletes the C file, those
declarations and that comparison.

The build copies `std/*.ft` next to the runtime object into `build/<preset>/std/`, which the
compiler uses as its fallback `--std-dir` (`notes/toolchain.md` sections 1 and 2), so a new module
is visible to `test/lang/run_tests.py` only after `tools/vm build <preset>` has copied it. The
language tests of the library are `test/lang/run/stdlib/`.
