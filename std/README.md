# fort standard library

The standard library modules of decision D13.2 live here as `.ft` sources; `notes/stdlib.md`
specifies them. `std.libc` (thin libc `extern` declarations and the open, lseek and errno
constants), `std.rt` (the five `fort_rt_*` runtime entry points the library calls, and nothing
else; `notes/stdlib.md` 3 owns their contract),
`std.mem`, `std.str`, `std.sys`, `std.strbuf` (the growable buffer), `std.vec` (`ptr_vec`,
`int_vec` and the non-generic container pattern), `std.strmap` (the open-addressing table) and
`std.io` (descriptors, whole files and streams) are written; `std.math` arrives with ticket
T-042.

The build copies `std/*.ft` next to the runtime object into `build/<preset>/std/`, which the
compiler uses as its fallback `--std-dir` (`notes/toolchain.md` sections 1 and 2), so a new module
is visible to `test/lang/run_tests.py` only after `tools/vm build <preset>` has copied it. The
language tests of the library are `test/lang/run/stdlib/`.
