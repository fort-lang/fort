//! pass: build-mode --no-bounds-check
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[4], _2 (i): i32) -> i32 {
//|     let _0: i32;
//|     let _3: u64 #2:14;
//|     let _4: bool #2:14;
//|     let _5: i64 #2:14;
//|
//|     bb0: {
//|         _3 = cast<u64>(copy _2) #2:14 s1;
//|         _0 = copy _1[_3] #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// The lowering of `return a[i];`. --no-bounds-check turns the bounds check into `goto bb1`. The
// `ge` and the reported `cast` fed only the check and go. The index `_3` stays, because the place
// `_1[_3]` reads it, and bb1 merges into bb0.
fn main.f(_1 (a): i32[4], _2 (i): i32) -> i32 {
    let _3: u64 #2:14;
    let _4: bool #2:14;
    let _5: i64 #2:14;

    bb0: {
        _3 = cast<u64>(copy _2) #2:14 s1;
        _4 = ge(copy _3, const u64 4) #2:14 s1;
        _5 = cast<i64>(copy _3) #2:14 s1;
        check(copy _4, bounds, copy _5, const u64 4) -> bb1 #2:14 s1;
    }
    bb1: {
        _0 = copy _1[_3] #2:12 s1;
        return #2:5 s1;
    }
}
