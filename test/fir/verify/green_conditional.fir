//! pass: verify
//! expect:
//| fn main.f(_1 (c): bool) -> i32 {
//|     let _0: i32;
//|     let _2: i32;
//|
//|     bb0: {
//|         switch(copy _1) -> [true: bb1, otherwise: bb2] #2:12 s1;
//|     }
//|     bb1: {
//|         _2 = const i32 1 #2:16 s1;
//|         goto bb3 #2:12 s1;
//|     }
//|     bb2: {
//|         _2 = const i32 2 #2:20 s1;
//|         goto bb3 #2:12 s1;
//|     }
//|     bb3: {
//|         _0 = copy _2 #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// V13: `return c ? 1 : 2;` as spec/fir.md 9.4 lowers it. Each arm assigns the slot `_2`, and the
// join reads it.
fn main.f(_1 (c): bool) -> i32 {
    let _2: i32;

    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:12 s1;
    }
    bb1: {
        _2 = const i32 1 #2:16 s1;
        goto bb3 #2:12 s1;
    }
    bb2: {
        _2 = const i32 2 #2:20 s1;
        goto bb3 #2:12 s1;
    }
    bb3: {
        _0 = copy _2 #2:12 s1;
        return #2:5 s1;
    }
}
