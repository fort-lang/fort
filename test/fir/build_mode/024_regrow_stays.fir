//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.f(_1 (n): u64) -> u64 {
//|     let _0: u64;
//|     let _2 (s): u8 mut@ own;
//|     let _3: u8 mut* own;
//|     let _4: u8 mut@ own;
//|     let _5: u8 mut@ own;
//|     let _6: u8 mut* own;
//|     let _7: u8 mut@ own;
//|
//|     bb0: {
//|         _2 = const zero u8 mut@ own #9:1 s0;
//|         live(_2) #9:1 s0;
//|         _3 = alloc<u8>(const u64 2) #10:1 s0;
//|         _2 = aggregate u8 mut@ own(move _3, const u64 2) #10:1 s0;
//|         _5 = move _2 #11:1 s0;
//|         _6 = regrow<u8>(move _5, copy _1) #11:2 s0;
//|         _2 = aggregate u8 mut@ own(move _6, copy _1) #11:2 s0;
//|         _0 = copy _2.1 #12:1 s0;
//|         del(move _2) #13:1 s0;
//|         dead(_2) #14:1 s0;
//|         return #14:1 s0;
//|     }
//| }
// No build mode removes a `regrow` or one of its tests: the rvalue has no `check` that the
// removal pass reads (D12.2, spec/fir.md 11). --release removes the two overwrite checks, so each
// `aggregate` folds into `_2` and the three blocks merge.
fn main.f(_1 (n): u64) -> u64 {
    let _2 (s): u8 mut@ own;
    let _3: u8 mut* own;
    let _4: u8 mut@ own;
    let _5: u8 mut@ own;
    let _6: u8 mut* own;
    let _7: u8 mut@ own;

    bb0: {
        _2 = const zero u8 mut@ own #9:1;
        live(_2) #9:1;
        _3 = alloc<u8>(const u64 2) #10:1;
        _4 = aggregate u8 mut@ own(move _3, const u64 2) #10:1;
        check(overwrite: _2) -> bb1 #10:2;
    }
    bb1: {
        _2 = move _4 #10:2;
        _5 = move _2 #11:1;
        _6 = regrow<u8>(move _5, copy _1) #11:2;
        _7 = aggregate u8 mut@ own(move _6, copy _1) #11:2;
        check(overwrite: _2) -> bb2 #11:3;
    }
    bb2: {
        _2 = move _7 #11:3;
        _0 = copy _2.1 #12:1;
        del(move _2) #13:1;
        dead(_2) #14:1;
        return #14:1;
    }
}
