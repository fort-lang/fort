//! pass: build-mode --no-bounds-check
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32, _2 (b): i32) -> i32 {
//|     let _0: i32;
//|     let _3: bool #2:14;
//|     let _4: i32 #2:14;
//|
//|     bb0: {
//|         _3 = add_overflows(copy _1, copy _2) #2:14 s1;
//|         check(copy _3, overflow) -> bb1 #2:14 s1;
//|     }
//|     bb1: {
//|         _4 = add(copy _1, copy _2) #2:14 s1;
//|         _0 = copy _4 #2:14 s1;
//|         return #2:5 s1;
//|     }
//| }
// --no-bounds-check removes the bounds and span checks only, so the overflow check of
// `return a + b;` and the `add_overflows` that feeds it stay.
fn main.f(_1 (a): i32, _2 (b): i32) -> i32 {
    let _3: bool #2:14;
    let _4: i32 #2:14;

    bb0: {
        _3 = add_overflows(copy _1, copy _2) #2:14 s1;
        check(copy _3, overflow) -> bb1 #2:14 s1;
    }
    bb1: {
        _4 = add(copy _1, copy _2) #2:14 s1;
        _0 = copy _4 #2:14 s1;
        return #2:5 s1;
    }
}
