//! pass: verify
//! panic: fir.verify: V3 a deref of a type that is not a pointer: main.f bb0 statement 0 (s1)
// V3: a deref applies to a place of pointer type.
fn main.f(_1 (x): i32) -> i32 {
    bb0: {
        _0 = copy (*_1) #2:12 s1;
        return #2:5 s1;
    }
}
