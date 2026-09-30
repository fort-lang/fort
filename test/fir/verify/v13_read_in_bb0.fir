//! pass: verify
//! panic: V13 a read of a temporary before an assignment of it: main.f bb0 statement 0 (s1)
// V13: bb0 reads `_1` before any assignment of it. The translator keeps `_1` in an alloca, so the
// read would load undefined memory, and the LLVM verifier does not report it.
fn main.f() -> i32 {
    let _1: i32;

    bb0: {
        _0 = copy _1 #2:12 s1;
        return #2:5 s1;
    }
}
