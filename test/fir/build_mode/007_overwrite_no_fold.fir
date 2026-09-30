//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.other() -> void {
//|     let _0: void;
//|     let _1 (p): i32 mut* own;
//|     let _2: i32 mut* own;
//|     let _3 (q): i32 mut* own;
//|
//|     bb0: {
//|         _1 = const zero i32 mut* own;
//|         _3 = const zero i32 mut* own;
//|         _2 = alloc<i32>(const u64 1);
//|         _3 = move _2;
//|         del(move _3);
//|         return;
//|     }
//| }
//|
//| fn main.twice(_1 (c): bool) -> void {
//|     let _0: void;
//|     let _2 (p): i32 mut* own;
//|     let _3: i32 mut* own;
//|     let _4 (q): i32 mut* own;
//|
//|     bb0: {
//|         _2 = const zero i32 mut* own;
//|         _4 = const zero i32 mut* own;
//|         switch(copy _1) -> [true: bb1, otherwise: bb2];
//|     }
//|     bb1: {
//|         _3 = alloc<i32>(const u64 1);
//|         _2 = move _3;
//|         del(move _2);
//|         return;
//|     }
//|     bb2: {
//|         _3 = alloc<i32>(const u64 1);
//|         _4 = move _3;
//|         del(move _4);
//|         return;
//|     }
//| }
//|
//| fn main.join(_1 (c): bool) -> void {
//|     let _0: void;
//|     let _2 (p): i32 mut* own;
//|     let _3: i32 mut* own;
//|
//|     bb0: {
//|         _2 = const zero i32 mut* own;
//|         switch(copy _1) -> [true: bb1, otherwise: bb2];
//|     }
//|     bb1: {
//|         _3 = alloc<i32>(const u64 1);
//|         goto bb3;
//|     }
//|     bb2: {
//|         _3 = const null i32 mut* own;
//|         goto bb3;
//|     }
//|     bb3: {
//|         _2 = move _3;
//|         del(move _2);
//|         return;
//|     }
//| }
// The overwrite check goes in each function, and its continuation merges into its block, but no
// store folds. In `other` the store writes `_3` and the check tests `_1`. In `twice` a second
// statement reads `_3` on another path. In `join` the block of the check does not end in the
// assignment of `_3`, which the two arms of a `?:` make.
fn main.other() -> void {
    let _1 (p): i32 mut* own;
    let _2: i32 mut* own;
    let _3 (q): i32 mut* own;

    bb0: {
        _1 = const zero i32 mut* own;
        _3 = const zero i32 mut* own;
        _2 = alloc<i32>(const u64 1);
        check(overwrite: _1) -> bb1;
    }
    bb1: {
        _3 = move _2;
        del(move _3);
        return;
    }
}

fn main.twice(_1 (c): bool) -> void {
    let _2 (p): i32 mut* own;
    let _3: i32 mut* own;
    let _4 (q): i32 mut* own;

    bb0: {
        _2 = const zero i32 mut* own;
        _4 = const zero i32 mut* own;
        switch(copy _1) -> [true: bb1, otherwise: bb3];
    }
    bb1: {
        _3 = alloc<i32>(const u64 1);
        check(overwrite: _2) -> bb2;
    }
    bb2: {
        _2 = move _3;
        del(move _2);
        return;
    }
    bb3: {
        _3 = alloc<i32>(const u64 1);
        check(overwrite: _4) -> bb4;
    }
    bb4: {
        _4 = move _3;
        del(move _4);
        return;
    }
}

fn main.join(_1 (c): bool) -> void {
    let _2 (p): i32 mut* own;
    let _3: i32 mut* own;

    bb0: {
        _2 = const zero i32 mut* own;
        switch(copy _1) -> [true: bb1, otherwise: bb2];
    }
    bb1: {
        _3 = alloc<i32>(const u64 1);
        goto bb3;
    }
    bb2: {
        _3 = const null i32 mut* own;
        goto bb3;
    }
    bb3: {
        check(overwrite: _2) -> bb4;
    }
    bb4: {
        _2 = move _3;
        del(move _2);
        return;
    }
}
