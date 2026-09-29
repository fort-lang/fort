//! pass: verify
//! panic: fir.verify: V4 a cast that D3.14 refuses: main.f bb0 statement 0 (s1)
//! prelude:
//| struct pair {
//|     i32 a;
//|     u64 b;
//| }
// V4: a cast to an aggregate type agrees with its destination by the rule of D3.14, and D3.14
// allows no struct cast. The destination has the type of the cast, so only the cast rule fails.
fn main.f(_1 (x): i32) -> pair {
    bb0: {
        _0 = cast<pair>(copy _1) #2:12 s1;
        return #2:5 s1;
    }
}
