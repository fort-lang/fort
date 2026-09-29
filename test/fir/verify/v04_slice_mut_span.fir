//! pass: verify
//! panic: fir.verify: V4 a slice mut of a span or a string: main.f bb0 statement 0 (s1)
// V4: `mut` appears only on a slice of a fixed-array place (spec/fir.md 6). A slice of a span
// keeps the `mut` of the span, so `slice mut` of a span gives two texts for one rvalue.
fn main.f(_1 (s): i32 mut@) -> i32 mut@ {
    bb0: {
        _0 = slice mut(_1, const u64 0, const u64 1) #2:12 s1;
        return #2:5 s1;
    }
}
