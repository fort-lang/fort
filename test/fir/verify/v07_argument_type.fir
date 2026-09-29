//! pass: verify
//! panic: fir.verify: V7 an argument does not agree with its parameter: main.f bb0 statement 0 (s1)
//! prelude:
//| fn add(i32 x, i64 y) i64 {
//|     return 0;
//| }
// V7: every argument agrees with its parameter as V4 defines agreement. The second argument is
// an i32 and the second parameter an i64.
fn main.f(_1 (x): i32) -> i64 {
    bb0: {
        _0 = call fn main.add(copy _1, copy _1) #2:12 s1;
        return #2:5 s1;
    }
}
