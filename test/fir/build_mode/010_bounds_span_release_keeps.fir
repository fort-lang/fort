//! pass: build-mode --release
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
//|         _4 = ge(copy _3, const u64 4) #2:14 s1;
//|         _5 = cast<i64>(copy _3) #2:14 s1;
//|         check(copy _4, bounds, copy _5, const u64 4) -> bb1 #2:14 s1;
//|     }
//|     bb1: {
//|         _0 = copy _1[_3] #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
//|
//| fn main.g(_1 (s): u8@, _2 (i): u64) -> u8@ {
//|     let _0: u8@;
//|     let _3: u64 #3:13;
//|     let _4: u64 #3:13;
//|     let _5: bool #3:13;
//|     let _6: bool #3:13;
//|     let _7: bool #3:13;
//|     let _8: i64 #3:13;
//|     let _9: i64 #3:13;
//|
//|     bb0: {
//|         _3 = cast<u64>(const i32 1) #3:13 s1;
//|         _4 = cast<u64>(copy _2) #3:13 s1;
//|         _5 = gt(copy _3, copy _4) #3:13 s1;
//|         _6 = gt(copy _4, copy _1.1) #3:13 s1;
//|         _7 = or(copy _5, copy _6) #3:13 s1;
//|         _8 = cast<i64>(copy _3) #3:13 s1;
//|         _9 = cast<i64>(copy _4) #3:13 s1;
//|         check(copy _7, span, copy _8, copy _9, copy _1.1) -> bb1 #3:13 s1;
//|     }
//|     bb1: {
//|         _0 = slice(_1, copy _3, copy _4) #3:13 s1;
//|         return #3:5 s1;
//|     }
//| }
// --release removes the overflow, shift and overwrite checks only, so the bounds check of `a[i]`
// and the span check of `s[1..i]` stay, with every statement that feeds them.
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

fn main.g(_1 (s): u8@, _2 (i): u64) -> u8@ {
    let _3: u64 #3:13;
    let _4: u64 #3:13;
    let _5: bool #3:13;
    let _6: bool #3:13;
    let _7: bool #3:13;
    let _8: i64 #3:13;
    let _9: i64 #3:13;

    bb0: {
        _3 = cast<u64>(const i32 1) #3:13 s1;
        _4 = cast<u64>(copy _2) #3:13 s1;
        _5 = gt(copy _3, copy _4) #3:13 s1;
        _6 = gt(copy _4, copy _1.1) #3:13 s1;
        _7 = or(copy _5, copy _6) #3:13 s1;
        _8 = cast<i64>(copy _3) #3:13 s1;
        _9 = cast<i64>(copy _4) #3:13 s1;
        check(copy _7, span, copy _8, copy _9, copy _1.1) -> bb1 #3:13 s1;
    }
    bb1: {
        _0 = slice(_1, copy _3, copy _4) #3:13 s1;
        return #3:5 s1;
    }
}
