//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| struct row {
//|     u64 n;
//|     i32[2] cells;
//|     i32@ view;
//| }
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! expect:
//| fn main.f(_1 (h): holder) -> holder {
//|     let _0: holder;
//|     let _2: holder;
//|     let _3: holder mut*;
//|     let _4: u8 mut* own mut*;
//|     let _5: u8 mut*;
//|
//|     bb0: {
//|         _2 = const zero holder #2:5 s1;
//|         _3 = addr mut(_2) #3:5 s2;
//|         _4 = addr mut(_2.0) #4:5 s3;
//|         _0 = move _1 #5:5 s4;
//|         _5 = addr mut((*_0.0)) #6:5 s5;
//|         return #7:5 s6;
//|     }
//| }
//|
//| fn main.g(_1 (r): row) -> row {
//|     let _0: row;
//|     let _2: row;
//|     let _3: i32 mut@;
//|     let _4: i32@;
//|     let _5: u64;
//|     let _6: i32*;
//|
//|     bb0: {
//|         _2 = copy _1 #2:5 s1;
//|         _3 = slice mut(_2.1, const u64 0, const u64 2) #3:5 s2;
//|         _4 = slice(_1.2, const u64 0, const u64 0) #4:5 s3;
//|         _0 = copy _1 #5:5 s4;
//|         _5 = const u64 0 #6:5 s5;
//|         _6 = addr(_0.2[_5]) #6:5 s5;
//|         return #7:5 s6;
//|     }
//| }
//|
//| fn main.h() -> i32 {
//|     let _0: i32;
//|     let _1: i32 mut*;
//|
//|     bb0: {
//|         _0 = const i32 0 #2:5 s1;
//|         _1 = addr mut(_0) #3:5 s2;
//|         return #4:5 s3;
//|     }
//| }
//|
//| fn main.k() -> u8 mut* own {
//|     let _0: u8 mut* own;
//|     let _1: u8 mut* own mut*;
//|
//|     bb0: {
//|         _1 = addr mut(_0) #2:5 s1;
//|         _0 = call fn main.grab() #3:5 s2;
//|         return #4:5 s3;
//|     }
//| }
// The addresses that V15 keeps. In `f` and `g` the result is an aggregate, and each `addr` and
// each `slice` names another local, or a place through a pointer or a span that the result
// holds, which is no fixed storage of `_0`. In `h` and `k` the result is no aggregate, so V15
// reads nothing: the address makes the `own` reference `_0` of `k` a memory local.
fn main.f(_1 (h): holder) -> holder {
    let _2: holder;
    let _3: holder mut*;
    let _4: u8 mut* own mut*;
    let _5: u8 mut*;

    bb0: {
        _2 = const zero holder #2:5 s1;
        _3 = addr mut(_2) #3:5 s2;
        _4 = addr mut(_2.0) #4:5 s3;
        _0 = move _1 #5:5 s4;
        _5 = addr mut((*_0.0)) #6:5 s5;
        return #7:5 s6;
    }
}
fn main.g(_1 (r): row) -> row {
    let _2: row;
    let _3: i32 mut@;
    let _4: i32@;
    let _5: u64;
    let _6: i32*;

    bb0: {
        _2 = copy _1 #2:5 s1;
        _3 = slice mut(_2.1, const u64 0, const u64 2) #3:5 s2;
        _4 = slice(_1.2, const u64 0, const u64 0) #4:5 s3;
        _0 = copy _1 #5:5 s4;
        _5 = const u64 0 #6:5 s5;
        _6 = addr(_0.2[_5]) #6:5 s5;
        return #7:5 s6;
    }
}
fn main.h() -> i32 {
    let _1: i32 mut*;

    bb0: {
        _0 = const i32 0 #2:5 s1;
        _1 = addr mut(_0) #3:5 s2;
        return #4:5 s3;
    }
}
fn main.k() -> u8 mut* own {
    let _1: u8 mut* own mut*;

    bb0: {
        _1 = addr mut(_0) #2:5 s1;
        _0 = call fn main.grab() #3:5 s2;
        return #4:5 s3;
    }
}
