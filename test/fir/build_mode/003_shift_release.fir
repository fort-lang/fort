//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32, _2 (b): u8) -> i32 {
//|     let _0: i32;
//|     let _3: i64 #2:14;
//|     let _4: u64 #2:14;
//|     let _5: bool #2:14;
//|     let _6: i32 #2:14;
//|     let _7: i32 #2:14;
//|     let _8: i64 #2:14;
//|
//|     bb0: {
//|         _3 = cast<i64>(copy _2) #2:14 s1;
//|         _8 = and(copy _3, const i64 31) #2:14 s1;
//|         _6 = cast<i32>(copy _8) #2:14 s1;
//|         _7 = shl(copy _1, copy _6) #2:14 s1;
//|         _0 = copy _7 #2:14 s1;
//|         return #2:5 s1;
//|     }
//| }
// The lowering of `return a << b;`. Under --release the shift check becomes the mask `_8 = and(copy
// _3, const i64 31)`, at the location and the statement index of the check, and `goto bb1`. The
// read of the count `_3` in bb1 reads `_8`. `_5` fed the check and `_4` fed `_5`, so both go, one
// round after the other. bb1 merges into bb0.
fn main.f(_1 (a): i32, _2 (b): u8) -> i32 {
    let _3: i64 #2:14;
    let _4: u64 #2:14;
    let _5: bool #2:14;
    let _6: i32 #2:14;
    let _7: i32 #2:14;

    bb0: {
        _3 = cast<i64>(copy _2) #2:14 s1;
        _4 = cast<u64>(copy _3) #2:14 s1;
        _5 = ge(copy _4, const u64 32) #2:14 s1;
        check(copy _5, shift, copy _3, const bytes "i32") -> bb1 #2:14 s1;
    }
    bb1: {
        _6 = cast<i32>(copy _3) #2:14 s1;
        _7 = shl(copy _1, copy _6) #2:14 s1;
        _0 = copy _7 #2:14 s1;
        return #2:5 s1;
    }
}
