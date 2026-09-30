//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32, _2 (b): i32) -> i32 {
//|     let _0: i32;
//|     let _3: bool #2:14;
//|     let _4: i32 #2:14;
//|
//|     bb0: {
//|         _4 = add(copy _1, copy _2) #2:14 s1;
//|         _0 = copy _4 #2:14 s1;
//|         return #2:5 s1;
//|     }
//| }
// The lowering of `return a + b;`. Under --release the overflow check becomes `goto bb1`, the
// `add_overflows` that fed it has no reader and goes, and bb1 merges into bb0. The `add` wraps.
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
