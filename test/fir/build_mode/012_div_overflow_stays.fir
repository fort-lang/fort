//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.dov(_1 (a): i32, _2 (b): i32) -> i32 {
//|     let _0: i32;
//|     let _3: bool #9:14;
//|     let _4: bool #9:14;
//|     let _5: bool #9:14;
//|     let _6: bool #9:14;
//|     let _7: i32 #9:14;
//|
//|     bb0: {
//|         _3 = eq(copy _2, const i32 0) #9:14 s1;
//|         check(copy _3, div_zero) -> bb1 #9:14 s1;
//|     }
//|     bb1: {
//|         _4 = eq(copy _2, const i32 -1) #9:14 s1;
//|         _5 = eq(copy _1, const i32 -2147483648) #9:14 s1;
//|         _6 = and(copy _4, copy _5) #9:14 s1;
//|         check(copy _6, div_overflow) -> bb2 #9:14 s1;
//|     }
//|     bb2: {
//|         _7 = rem(copy _1, copy _2) #9:14 s1;
//|         _0 = copy _7 #9:14 s1;
//|         return #9:5 s1;
//|     }
//| }
// The lowering of `return a % b;` on `i32`. No build mode removes the check of the kind
// `div_overflow`, so it stays with the three statements that feed it, and no block merges.
fn main.dov(_1 (a): i32, _2 (b): i32) -> i32 {
    let _3: bool #9:14;
    let _4: bool #9:14;
    let _5: bool #9:14;
    let _6: bool #9:14;
    let _7: i32 #9:14;

    bb0: {
        _3 = eq(copy _2, const i32 0) #9:14 s1;
        check(copy _3, div_zero) -> bb1 #9:14 s1;
    }
    bb1: {
        _4 = eq(copy _2, const i32 -1) #9:14 s1;
        _5 = eq(copy _1, const i32 -2147483648) #9:14 s1;
        _6 = and(copy _4, copy _5) #9:14 s1;
        check(copy _6, div_overflow) -> bb2 #9:14 s1;
    }
    bb2: {
        _7 = rem(copy _1, copy _2) #9:14 s1;
        _0 = copy _7 #9:14 s1;
        return #9:5 s1;
    }
}
