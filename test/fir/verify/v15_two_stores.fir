//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     i32 b;
//| }
//! panic: fir.verify: V15 a second store into an aggregate result: main.f bb1 statement 0 (s3)
// V15: bb1 stores into `_0` after the store of bb0. `_0` is the destination of the caller, and
// owner.prover gives a store into it no proof, so a second store on a path is no lowering.
fn main.f(_1 (p): pair, _2 (c): bool) -> pair {
    bb0: {
        _0 = copy _1 #2:5 s1;
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
