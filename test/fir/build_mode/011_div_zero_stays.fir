//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.dz(_1 (a): u32, _2 (b): u32) -> u32 {
//|     let _0: u32;
//|     let _3: bool #6:14;
//|     let _4: u32 #6:14;
//|
//|     bb0: {
//|         _3 = eq(copy _2, const u32 0) #6:14 s1;
//|         check(copy _3, div_zero) -> bb1 #6:14 s1;
//|     }
//|     bb1: {
//|         _4 = div(copy _1, copy _2) #6:14 s1;
//|         _0 = copy _4 #6:14 s1;
//|         return #6:5 s1;
//|     }
//| }
// The lowering of `return a / b;` on `u32`. No build mode removes the check of the kind
// `div_zero`, so the check and the `eq` that feeds it stay under --release --no-bounds-check.
fn main.dz(_1 (a): u32, _2 (b): u32) -> u32 {
    let _3: bool #6:14;
    let _4: u32 #6:14;

    bb0: {
        _3 = eq(copy _2, const u32 0) #6:14 s1;
        check(copy _3, div_zero) -> bb1 #6:14 s1;
    }
    bb1: {
        _4 = div(copy _1, copy _2) #6:14 s1;
        _0 = copy _4 #6:14 s1;
        return #6:5 s1;
    }
}
