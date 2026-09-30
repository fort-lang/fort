//! pass: verify
//! prelude:
//| enum color {
//|     red,
//|     green,
//| }
//! expect:
//| fn main.f(_1 (c): color) -> i32 {
//|     let _0: i32;
//|     let _2: i64 #2:13;
//|
//|     bb0: {
//|         _2 = cast<i64>(copy _1) #2:13 s1;
//|         switch(copy _1) -> [color 0: bb2, color 1: bb3, otherwise: bb1] #2:5 s1;
//|     }
//|     bb1: {
//|         fail(enum, copy _2, const bytes "color") #2:5 s1;
//|     }
//|     bb2: {
//|         _0 = const i32 1 #3:24 s2;
//|         return #3:17 s2;
//|     }
//|     bb3: {
//|         _0 = const i32 2 #4:26 s3;
//|         return #4:19 s3;
//|     }
//|     bb4: {
//|         unreachable #6:1 s0;
//|     }
//| }
// V9 and V13 on a `switch` over an enum with no `default` (spec/fir.md 9.3): `_2` holds the value
// before the switch, and the fail block bb1 reads it. The fail block is an abort block, and the
// end of the body after the switch ends in unreachable, which no path reaches.
fn main.f(_1 (c): color) -> i32 {
    let _2: i64 #2:13;

    bb0: {
        _2 = cast<i64>(copy _1) #2:13 s1;
        switch(copy _1) -> [color 0: bb2, color 1: bb3, otherwise: bb1] #2:5 s1;
    }
    bb1: {
        fail(enum, copy _2, const bytes "color") #2:5 s1;
    }
    bb2: {
        _0 = const i32 1 #3:24 s2;
        return #3:17 s2;
    }
    bb3: {
        _0 = const i32 2 #4:26 s3;
        return #4:19 s3;
    }
    bb4: {
        unreachable #6:1 s0;
    }
}
