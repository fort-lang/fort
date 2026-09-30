//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.chain(_1 (a): i32, _2 (b): i32, _3 (c): i32) -> i32 {
//|     let _0: i32;
//|     let _4: bool;
//|     let _5: i32;
//|     let _6: bool;
//|     let _7: i32;
//|
//|     bb0: {
//|         switch(copy _1) -> [i32 0: bb2, i32 1: bb1, otherwise: bb3];
//|     }
//|     bb1: {
//|         _5 = add(copy _1, copy _2);
//|         _7 = mul(copy _5, copy _3);
//|         _0 = copy _7;
//|         goto bb3;
//|     }
//|     bb2: {
//|         _0 = const i32 0;
//|         goto bb3;
//|     }
//|     bb3: {
//|         return;
//|     }
//| }
//|
//| fn main.back(_1 (a): i32) -> i32 {
//|     let _0: i32;
//|     let _2: bool;
//|     let _3: i32;
//|
//|     bb0: {
//|         goto bb1;
//|     }
//|     bb1: {
//|         _3 = sub(copy _1, const i32 1);
//|         _0 = copy _3;
//|         return;
//|     }
//| }
// In `chain` two removed checks follow each other, so bb1 takes bb2 and then bb3, and a chain of
// merges ends in its first block. The blocks that stay take the numbers 0 to 3 in ascending order,
// and each target follows them: the two arms and `otherwise` of the `switch`, and both `goto`s.
// In `back` the continuation bb1 has a smaller number than its check block bb2. bb2 takes bb1, and
// it then moves to the number 1.
fn main.chain(_1 (a): i32, _2 (b): i32, _3 (c): i32) -> i32 {
    let _4: bool;
    let _5: i32;
    let _6: bool;
    let _7: i32;

    bb0: {
        switch(copy _1) -> [i32 0: bb4, i32 1: bb1, otherwise: bb5];
    }
    bb1: {
        _4 = add_overflows(copy _1, copy _2);
        check(copy _4, overflow) -> bb2;
    }
    bb2: {
        _5 = add(copy _1, copy _2);
        _6 = mul_overflows(copy _5, copy _3);
        check(copy _6, overflow) -> bb3;
    }
    bb3: {
        _7 = mul(copy _5, copy _3);
        _0 = copy _7;
        goto bb5;
    }
    bb4: {
        _0 = const i32 0;
        goto bb5;
    }
    bb5: {
        return;
    }
}

fn main.back(_1 (a): i32) -> i32 {
    let _2: bool;
    let _3: i32;

    bb0: {
        goto bb2;
    }
    bb1: {
        _3 = sub(copy _1, const i32 1);
        _0 = copy _3;
        return;
    }
    bb2: {
        _2 = sub_overflows(copy _1, const i32 1);
        check(copy _2, overflow) -> bb1;
    }
}
