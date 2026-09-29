//! pass: verify
//! panic: fir.verify: V4 an overwrite check in the operand form: main.f bb0 statement 0 (s1)
// V4: section 8 gives the overwrite check the place form alone, `check(overwrite: p)`, because its
// condition is the null test of the place. The operand form names no place to test.
fn main.f(_1 (c): bool) -> void {
    bb0: {
        check(copy _1, overwrite) -> bb1 #2:5 s1;
    }
    bb1: {
        return #3:1 s0;
    }
}
