//! pass: verify
//! panic: fir.verify: V7 a call with the wrong count of arguments: main.f bb0 statement 0 (s1)
//! prelude:
//| fn add(i32 x, i64 y) i64 {
//|     return 0;
//| }
// V7: a call has the argument count of its callee's fort type (items 7, 8).
fn main.f(_1 (x): i32) -> i64 {
    bb0: {
        _0 = call fn main.add(copy _1) #2:12 s1;
        return #2:5 s1;
    }
}
