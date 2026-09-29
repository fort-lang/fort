//! pass: verify
//! panic: fir.verify: V4 two switch arms with one value: main.f bb0 statement 0 (s1)
// V4: no two arms of one switch have one value. The translator writes an LLVM `switch`, which
// takes each case value once (spec/fir.md 12.4). `i32 0` and `i32 -0` are one value.
fn main.f(_1 (x): i32) -> void {
    bb0: {
        switch(copy _1) -> [i32 0: bb1, i32 7: bb1, i32 -0: bb1, otherwise: bb1] #2:5 s1;
    }
    bb1: {
        return #3:1 s0;
    }
}
