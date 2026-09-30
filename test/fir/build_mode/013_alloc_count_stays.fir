//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.ac(_1 (n): i32) -> void {
//|     let _0: void;
//|     let _2 (s): u8 mut@ own #12:17;
//|     let _3: u8 mut@ own #12:21;
//|     let _4: i64 #12:21;
//|     let _5: bool #12:21;
//|     let _6: u64 #12:21;
//|     let _7: u8 mut* own #12:21;
//|
//|     bb0: {
//|         _2 = const zero u8 mut@ own #12:17 s0;
//|         live(_2) #12:17 s1;
//|         _4 = cast<i64>(copy _1) #12:21 s1;
//|         _5 = lt(copy _4, const i64 0) #12:21 s1;
//|         check(copy _5, alloc_count, copy _4) -> bb1 #12:21 s1;
//|     }
//|     bb1: {
//|         _6 = cast<u64>(copy _4) #12:21 s1;
//|         _7 = alloc<u8>(copy _6) #12:21 s1;
//|         _2 = aggregate u8 mut@ own(move _7, copy _6) #12:21 s1;
//|         del(move _2) #13:5 s2;
//|         dead(_2) #14:1 s0;
//|         return #14:1 s0;
//|     }
//| }
// The lowering of `u8 mut@ own s = new(u8, n); del(s);`. No build mode removes the check of the
// kind `alloc_count`, so it stays. --release removes the overwrite check of the declaration: the
// `aggregate` folds into `_2` and bb2 merges into bb1.
fn main.ac(_1 (n): i32) -> void {
    let _0: void;
    let _2 (s): u8 mut@ own #12:17;
    let _3: u8 mut@ own #12:21;
    let _4: i64 #12:21;
    let _5: bool #12:21;
    let _6: u64 #12:21;
    let _7: u8 mut* own #12:21;

    bb0: {
        _2 = const zero u8 mut@ own #12:17 s0;
        live(_2) #12:17 s1;
        _4 = cast<i64>(copy _1) #12:21 s1;
        _5 = lt(copy _4, const i64 0) #12:21 s1;
        check(copy _5, alloc_count, copy _4) -> bb1 #12:21 s1;
    }
    bb1: {
        _6 = cast<u64>(copy _4) #12:21 s1;
        _7 = alloc<u8>(copy _6) #12:21 s1;
        _3 = aggregate u8 mut@ own(move _7, copy _6) #12:21 s1;
        check(overwrite: _2) -> bb2 #12:17 s1;
    }
    bb2: {
        _2 = move _3 #12:17 s1;
        del(move _2) #13:5 s2;
        dead(_2) #14:1 s0;
        return #14:1 s0;
    }
}
