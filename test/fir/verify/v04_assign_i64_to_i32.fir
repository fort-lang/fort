//! pass: verify
//! panic: fir.verify: V4 the two sides of an assignment do not agree: main.f bb0 statement 0 (s1)
// V4: the two sides of an assignment agree in type. An i64 does not agree with an i32.
fn main.f(_1 (w): i64) -> i32 {
    bb0: {
        _0 = copy _1 #2:12 s1;
        return #2:5 s1;
    }
}
