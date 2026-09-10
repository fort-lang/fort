# fort standard library

The standard library modules of decision D13.2 (`std::sys`, `std::libc`, `std::mem`, `std::io`,
`std::str`, `std::strbuf`, `std::vec`, `std::strmap`, `std::math`) live here as `.ft` sources
once tickets T-027 and T-028 write them; `notes/stdlib.md` specifies them. The build copies
`std/*.ft` next to the runtime object into `build/<preset>/std/`, which the compiler uses as its
fallback `--std-dir` (`notes/toolchain.md` sections 1 and 2).
