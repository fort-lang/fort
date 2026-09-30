//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! prelude:
//| enum color {
//|     red,
//|     green,
//| }
//! expect:
//| fn main.en(_1 (c): color) -> i32 {
//|     let _0: i32;
//|     let _2: i64 #16:5;
//|
//|     bb0: {
//|         _2 = cast<i64>(copy _1) #16:5 s1;
//|         switch(copy _1) -> [color 0: bb2, color 1: bb3, otherwise: bb1] #16:5 s1;
//|     }
//|     bb1: {
//|         fail(enum, copy _2, const bytes "color") #16:5 s1;
//|     }
//|     bb2: {
//|         _0 = const i32 1 #18:16 s2;
//|         return #18:9 s2;
//|     }
//|     bb3: {
//|         _0 = const i32 2 #20:16 s3;
//|         return #20:9 s3;
//|     }
//|     bb4: {
//|         unreachable #22:1 s0;
//|     }
//| }
// The lowering of a `switch` on an enum that names every member and has no `default`. The block
// of `fail(enum, ...)` stays, and so does the `cast` that its reported operand reads. The block
// after the `switch`, which no terminator names, keeps its number.
fn main.en(_1 (c): color) -> i32 {
    let _0: i32;
    let _2: i64 #16:5;

    bb0: {
        _2 = cast<i64>(copy _1) #16:5 s1;
        switch(copy _1) -> [color 0: bb2, color 1: bb3, otherwise: bb1] #16:5 s1;
    }
    bb1: {
        fail(enum, copy _2, const bytes "color") #16:5 s1;
    }
    bb2: {
        _0 = const i32 1 #18:16 s2;
        return #18:9 s2;
    }
    bb3: {
        _0 = const i32 2 #20:16 s3;
        return #20:9 s3;
    }
    bb4: {
        unreachable #22:1 s0;
    }
}
