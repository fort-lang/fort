//! pass: verify
//! panic: fir.verify: V9 a path from bb0 reaches unreachable: main.f bb2 statement 0 (s0)
// V9: `if (c) { return 1; }` falls off the end of a function that returns a value. The switch on
// the parameter is no switch on a constant, so its false edge reaches the unreachable of the end.
fn main.f(_1 (c): bool) -> i32 {
    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:5 s1;
    }
    bb1: {
        _0 = const i32 1 #3:16 s2;
        return #3:9 s2;
    }
    bb2: {
        unreachable #5:1 s0;
    }
}
