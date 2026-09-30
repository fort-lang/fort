//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.s8(_1 (a): i8, _2 (n): i64) -> i8 {
//|     let _0: i8;
//|     let _3: i64;
//|     let _4: bool;
//|     let _5: i8;
//|     let _6: i64;
//|
//|     bb0: {
//|         _3 = copy _2;
//|         _6 = and(copy _3, const i64 7);
//|         _5 = cast<i8>(copy _6);
//|         _0 = shr(copy _1, copy _5);
//|         return;
//|     }
//| }
//|
//| fn main.h16(_1 (a): u16, _2 (n): i64) -> u16 {
//|     let _0: u16;
//|     let _3: i64;
//|     let _4: bool;
//|     let _5: u16;
//|     let _6: i64;
//|
//|     bb0: {
//|         _3 = copy _2;
//|         _6 = and(copy _3, const i64 15);
//|         _5 = cast<u16>(copy _6);
//|         _0 = shr(copy _1, copy _5);
//|         return;
//|     }
//| }
//|
//| fn main.s64(_1 (a): i64, _2 (n): i64) -> i64 {
//|     let _0: i64;
//|     let _3: i64;
//|     let _4: bool;
//|     let _5: i64;
//|     let _6: i64;
//|
//|     bb0: {
//|         _3 = copy _2;
//|         _6 = and(copy _3, const i64 63);
//|         _5 = cast<i64>(copy _6);
//|         _0 = shr(copy _1, copy _5);
//|         return;
//|     }
//| }
//|
//| fn main.w64(_1 (a): u64, _2 (n): i64) -> u64 {
//|     let _0: u64;
//|     let _3: i64;
//|     let _4: bool;
//|     let _5: u64;
//|     let _6: i64;
//|
//|     bb0: {
//|         _3 = copy _2;
//|         _6 = and(copy _3, const i64 63);
//|         _5 = cast<u64>(copy _6);
//|         _0 = shr(copy _1, copy _5);
//|         return;
//|     }
//| }
// The mask is the width of the type that the check names, less 1: 7 for `i8`, 15 for `u16` and 63
// for `i64` and `u64`. A check with no location gives a mask with no location.
fn main.s8(_1 (a): i8, _2 (n): i64) -> i8 {
    let _3: i64;
    let _4: bool;
    let _5: i8;

    bb0: {
        _3 = copy _2;
        _4 = ge(copy _3, const i64 8);
        check(copy _4, shift, copy _3, const bytes "i8") -> bb1;
    }
    bb1: {
        _5 = cast<i8>(copy _3);
        _0 = shr(copy _1, copy _5);
        return;
    }
}

fn main.h16(_1 (a): u16, _2 (n): i64) -> u16 {
    let _3: i64;
    let _4: bool;
    let _5: u16;

    bb0: {
        _3 = copy _2;
        _4 = ge(copy _3, const i64 16);
        check(copy _4, shift, copy _3, const bytes "u16") -> bb1;
    }
    bb1: {
        _5 = cast<u16>(copy _3);
        _0 = shr(copy _1, copy _5);
        return;
    }
}

fn main.s64(_1 (a): i64, _2 (n): i64) -> i64 {
    let _3: i64;
    let _4: bool;
    let _5: i64;

    bb0: {
        _3 = copy _2;
        _4 = ge(copy _3, const i64 64);
        check(copy _4, shift, copy _3, const bytes "i64") -> bb1;
    }
    bb1: {
        _5 = cast<i64>(copy _3);
        _0 = shr(copy _1, copy _5);
        return;
    }
}

fn main.w64(_1 (a): u64, _2 (n): i64) -> u64 {
    let _3: i64;
    let _4: bool;
    let _5: u64;

    bb0: {
        _3 = copy _2;
        _4 = ge(copy _3, const i64 64);
        check(copy _4, shift, copy _3, const bytes "u64") -> bb1;
    }
    bb1: {
        _5 = cast<u64>(copy _3);
        _0 = shr(copy _1, copy _5);
        return;
    }
}
