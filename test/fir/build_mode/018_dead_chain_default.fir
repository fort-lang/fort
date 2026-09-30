//! pass: build-mode
//! pass: verify
//! expect:
//| fn main.chain(_1 (a): i32) -> i32 {
//|     let _0: i32;
//|     let _2: i32;
//|     let _3: i32;
//|     let _4: bool;
//|     let _5: i32*;
//|
//|     bb0: {
//|         _0 = copy _1;
//|         return;
//|     }
//| }
//|
//| fn main.checked(_1 (a): i32[4], _2 (i): u64, _3 (b): i32) -> i32 {
//|     let _0: i32;
//|     let _4: bool;
//|     let _5: i64;
//|     let _6: bool;
//|     let _7: i32;
//|
//|     bb0: {
//|         _4 = ge(copy _2, const u64 4);
//|         _5 = cast<i64>(copy _2);
//|         check(copy _4, bounds, copy _5, const u64 4) -> bb1;
//|     }
//|     bb1: {
//|         _6 = add_overflows(copy _1[_2], copy _3);
//|         check(copy _6, overflow) -> bb2;
//|     }
//|     bb2: {
//|         _7 = add(copy _1[_2], copy _3);
//|         _0 = copy _7;
//|         return;
//|     }
//| }
// The build-mode pass with no argument is the default mode: it removes no check, so `checked`
// keeps its bounds check and its overflow check. It still removes the dead temporaries, and it
// repeats until nothing changes: `_4` and `_5` go in the first round, `_3` in the second and `_2`
// in the third.
fn main.chain(_1 (a): i32) -> i32 {
    let _2: i32;
    let _3: i32;
    let _4: bool;
    let _5: i32*;

    bb0: {
        _2 = cast<i32>(copy _1);
        _3 = mul(copy _2, copy _2);
        _4 = lt(copy _3, const i32 0);
        _5 = addr(_1);
        _0 = copy _1;
        return;
    }
}


fn main.checked(_1 (a): i32[4], _2 (i): u64, _3 (b): i32) -> i32 {
    let _4: bool;
    let _5: i64;
    let _6: bool;
    let _7: i32;

    bb0: {
        _4 = ge(copy _2, const u64 4);
        _5 = cast<i64>(copy _2);
        check(copy _4, bounds, copy _5, const u64 4) -> bb1;
    }
    bb1: {
        _6 = add_overflows(copy _1[_2], copy _3);
        check(copy _6, overflow) -> bb2;
    }
    bb2: {
        _7 = add(copy _1[_2], copy _3);
        _0 = copy _7;
        return;
    }
}
