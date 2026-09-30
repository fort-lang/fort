//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32, _2 (n): i64, _3 (c): bool) -> i32 {
//|     let _0: i32;
//|     let _4: i64;
//|     let _5: bool;
//|     let _6: i32;
//|     let _7 (x): i64;
//|     let _8: i64;
//|
//|     bb0: {
//|         _4 = copy _2;
//|         switch(copy _3) -> [true: bb1, otherwise: bb3];
//|     }
//|     bb1: {
//|         _8 = and(copy _4, const i64 31);
//|         _6 = cast<i32>(copy _8);
//|         _0 = shl(copy _1, copy _6);
//|         goto bb2;
//|     }
//|     bb2: {
//|         _7 = copy _8;
//|         _4 = add(copy _8, const i64 1);
//|         _7 = copy _4;
//|         goto bb3;
//|     }
//|     bb3: {
//|         _7 = copy _4;
//|         return;
//|     }
//| }
// Which reads of the count `_4` read the mask `_8`: each read that the mask reaches on every path
// from bb0 with no write of `_4` after it. The first two reads of bb3 do, the read of the `add`
// included, because an item reads before it writes. The read after that write does not, and nor
// does the read of bb4, which the path bb0 -> bb4 reaches with no mask. bb2 merges into bb1, so
// bb3 and bb4 become bb2 and bb3.
fn main.f(_1 (a): i32, _2 (n): i64, _3 (c): bool) -> i32 {
    let _4: i64;
    let _5: bool;
    let _6: i32;
    let _7 (x): i64;

    bb0: {
        _4 = copy _2;
        switch(copy _3) -> [true: bb1, otherwise: bb4];
    }
    bb1: {
        _5 = ge(copy _4, const i64 32);
        check(copy _5, shift, copy _4, const bytes "i32") -> bb2;
    }
    bb2: {
        _6 = cast<i32>(copy _4);
        _0 = shl(copy _1, copy _6);
        goto bb3;
    }
    bb3: {
        _7 = copy _4;
        _4 = add(copy _4, const i64 1);
        _7 = copy _4;
        goto bb4;
    }
    bb4: {
        _7 = copy _4;
        return;
    }
}
