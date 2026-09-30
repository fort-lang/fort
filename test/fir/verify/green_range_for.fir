//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[4]) -> i32 {
//|     let _0: i32;
//|     let _2 (s): i32 #2:15;
//|     let _3 (x): i32 #3:14;
//|     let _4: i32[4] #3:23;
//|     let _5: u64 #3:5;
//|     let _6: bool #3:5;
//|     let _7: i32 #4:9;
//|     let _8: bool #4:11;
//|
//|     bb0: {
//|         live(_2) #2:15 s1;
//|         _2 = const i32 0 #2:19 s1;
//|         _4 = copy _1 #3:23 s2;
//|         _5 = const u64 0 #3:5 s2;
//|         goto bb1 #3:5 s2;
//|     }
//|     bb1: {
//|         _6 = lt(copy _5, const u64 4) #3:5 s2;
//|         switch(copy _6) -> [true: bb2, otherwise: bb5] #3:5 s2;
//|     }
//|     bb2: {
//|         live(_3) #3:14 s2;
//|         _3 = copy _4[_5] #3:14 s2;
//|         _7 = copy _2 #4:9 s3;
//|         _8 = add_overflows(copy _7, copy _3) #4:11 s3;
//|         check(copy _8, overflow) -> bb3 #4:11 s3;
//|     }
//|     bb3: {
//|         _2 = add(copy _7, copy _3) #4:11 s3;
//|         dead(_3) #5:5 s2;
//|         goto bb4 #5:5 s2;
//|     }
//|     bb4: {
//|         _5 = add(copy _5, const u64 1) #3:5 s2;
//|         goto bb1 #3:5 s2;
//|     }
//|     bb5: {
//|         _0 = copy _2 #6:12 s4;
//|         dead(_2) #6:5 s4;
//|         return #6:5 s4;
//|     }
//| }
// V11 to V13: `i32 mut s = 0; for (i32 x : a) { s += x; } return s;` as spec/fir.md 9.3 lowers
// it. The collection is copied into `_4`, and the counter `_5` starts at 0 in bb0, grows in the
// step bb4 and is read in the head bb1 on the entry edge and on the back edge. The continuation
// bb3 of the overflow check has one predecessor.
fn main.f(_1 (a): i32[4]) -> i32 {
    let _2 (s): i32 #2:15;
    let _3 (x): i32 #3:14;
    let _4: i32[4] #3:23;
    let _5: u64 #3:5;
    let _6: bool #3:5;
    let _7: i32 #4:9;
    let _8: bool #4:11;

    bb0: {
        live(_2) #2:15 s1;
        _2 = const i32 0 #2:19 s1;
        _4 = copy _1 #3:23 s2;
        _5 = const u64 0 #3:5 s2;
        goto bb1 #3:5 s2;
    }
    bb1: {
        _6 = lt(copy _5, const u64 4) #3:5 s2;
        switch(copy _6) -> [true: bb2, otherwise: bb5] #3:5 s2;
    }
    bb2: {
        live(_3) #3:14 s2;
        _3 = copy _4[_5] #3:14 s2;
        _7 = copy _2 #4:9 s3;
        _8 = add_overflows(copy _7, copy _3) #4:11 s3;
        check(copy _8, overflow) -> bb3 #4:11 s3;
    }
    bb3: {
        _2 = add(copy _7, copy _3) #4:11 s3;
        dead(_3) #5:5 s2;
        goto bb4 #5:5 s2;
    }
    bb4: {
        _5 = add(copy _5, const u64 1) #3:5 s2;
        goto bb1 #3:5 s2;
    }
    bb5: {
        _0 = copy _2 #6:12 s4;
        dead(_2) #6:5 s4;
        return #6:5 s4;
    }
}
