//! pass: verify
//! expect:
//| fn main.f(_1 (c): bool) -> i32 {
//|     let _0: i32;
//|
//|     bb0: {
//|         switch(copy _1) -> [true: bb1, otherwise: bb2] #2:5 s1;
//|     }
//|     bb1: {
//|         _0 = const i32 1 #3:16 s2;
//|         return #3:9 s2;
//|     }
//|     bb2: {
//|         _0 = const i32 2 #5:16 s3;
//|         return #5:9 s3;
//|     }
//|     bb3: {
//|         unreachable #7:1 s0;
//|     }
//| }
// V9: `if (c) { return 1; } else { return 2; }` returns on every path. The block after the `if`
// ends the body in unreachable, and nothing names it.
fn main.f(_1 (c): bool) -> i32 {
    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:5 s1;
    }
    bb1: {
        _0 = const i32 1 #3:16 s2;
        return #3:9 s2;
    }
    bb2: {
        _0 = const i32 2 #5:16 s3;
        return #5:9 s3;
    }
    bb3: {
        unreachable #7:1 s0;
    }
}
