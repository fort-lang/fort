//! pass: verify
//! panic: fir.verify: V3 a field that its type does not have: main.f bb0 statement 0 (s1)
//! prelude:
//| struct pair {
//|     i32 a;
//|     u64 b;
//| }
// V3: a field applies to a struct that has that field. `pair` has fields 0 and 1.
fn main.f(_1 (p): pair) -> i32 {
    bb0: {
        _0 = copy _1.2 #2:12 s1;
        return #2:5 s1;
    }
}
