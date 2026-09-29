//! pass: verify
//! panic: fir.verify: V4 a switch arm of another type: main.f bb0 statement 0 (s1)
// V4: every arm of a switch is a constant of the type of its operand. `true` is no i32.
fn main.f(_1 (x): i32) -> void {
    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb1] #2:5 s1;
    }
    bb1: {
        return #3:1 s0;
    }
}
