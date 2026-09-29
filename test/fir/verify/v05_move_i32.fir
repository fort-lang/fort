//! pass: verify
//! panic: fir.verify: V5 a move of a place whose type does not own: main.f bb0 statement 0 (s1)
// V5: `move` applies only to a place of an owning type (spec/fir.md 5.6). An i32 owns nothing.
fn main.f(_1 (x): i32) -> i32 {
    bb0: {
        _0 = move _1 #2:12 s1;
        return #2:5 s1;
    }
}
