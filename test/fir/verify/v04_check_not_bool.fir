//! pass: verify
//! panic: fir.verify: V4 a check condition that is not a bool: main.f bb0 statement 0 (s1)
// V4: the condition of a check is a bool (spec/fir.md 8).
fn main.f(_1 (x): i32) -> void {
    bb0: {
        check(copy _1, overflow) -> bb1 #2:5 s1;
    }
    bb1: {
        return #3:1 s0;
    }
}
