//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     i32 b;
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 1 (s2)
// V15: an `addr` with no `mut` is an address of `_0` too, and so is one of a result that owns
// nothing. A cast gives a `mut` pointer from it, so the rule reads no `mut`.
fn main.f(_1 (p): pair) -> pair {
    let _2: pair*;
    bb0: {
        _0 = copy _1 #2:5 s1;
        _2 = addr(_0) #3:5 s2;
        return #4:5 s3;
    }
}
