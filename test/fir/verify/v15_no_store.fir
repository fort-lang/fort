//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     i32 b;
//| }
//! panic: fir.verify: V15 a return with no store of the aggregate result: main.f bb2 statement 0 (s4)
// V15: the path through bb0 and bb2 reaches the `return` with no store of `_0`. The path through
// bb1 stores it.
fn main.f(_1 (p): pair, _2 (c): bool) -> pair {
    bb0: {
        switch(copy _2) -> [true: bb1, otherwise: bb2] #3:9 s2;
    }
    bb1: {
        _0 = copy _1 #4:9 s3;
        goto bb2 #4:9 s3;
    }
    bb2: {
        return #5:5 s4;
    }
}
