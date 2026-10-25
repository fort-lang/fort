//! pass: build-mode
//! pass: verify
//! prelude:
//| struct sbox {
//|     i32 mut* own p;
//| }
//| fn fill(sbox mut* b) i32 mut* own {
//|     del(b->p);
//|     b->p = new(i32);
//|     return new(i32);
//| }
//! expect:
//| fn main.f() -> i32 {
//|     let _0: i32;
//|     let _1: sbox mut* own;
//|     let _2: i32 mut* own;
//|     let _3: u64;
//|
//|     bb0: {
//|         _1 = alloc<sbox>(const u64 1);
//|         check(overwrite: (*_1).0) -> bb1 #31:1 s0;
//|     }
//|     bb1: {
//|         (*_1).0 = call fn main.fill(copy _1) #31:1 s0;
//|         _2 = move (*_1).0;
//|         del(move _2);
//|         del(move _1);
//|         _0 = const i32 0;
//|         return;
//|     }
//| }
//|
//| fn main.g() -> i32 {
//|     let _0: i32;
//|     let _1: sbox mut* own;
//|     let _2: i32 mut* own;
//|
//|     bb0: {
//|         _1 = alloc<sbox>(const u64 1);
//|         check(overwrite: (*_1).0) -> bb1 #41:1 s0;
//|     }
//|     bb1: {
//|         goto bb2;
//|     }
//|     bb2: {
//|         (*_1).0 = call fn main.fill(copy _1) #41:1 s0;
//|         _2 = move (*_1).0;
//|         del(move _2);
//|         del(move _1);
//|         _0 = const i32 0;
//|         return;
//|     }
//| }
//|
//| fn main.h() -> i32 {
//|     let _0: i32;
//|     let _1: sbox mut* own;
//|     let _2: i32 mut* own;
//|     let _3: i32 mut* own;
//|
//|     bb0: {
//|         _1 = alloc<sbox>(const u64 1);
//|         _3 = alloc<i32>(const u64 1);
//|         goto bb1;
//|     }
//|     bb1: {
//|         (*_1).0 = move _3 #51:1 s0;
//|         _2 = move (*_1).0;
//|         del(move _2);
//|         del(move _1);
//|         _0 = const i32 0;
//|         return;
//|     }
//| }
// A check takes the verdict of each store into its place that a path from it reaches before
// another store into that place (spec/fir.md 11, Proof). In `f` a statement stands between the
// check and its store, and in `g` a `goto` does; `fill` fills `(*_1).0` before each store writes,
// so the emptiness pass proves neither store, and both checks stay. In `h` the `goto` leads to a
// store that the pass proves, so the check goes.
fn main.f() -> i32 {
    let _1: sbox mut* own;
    let _2: i32 mut* own;
    let _3: u64;
    bb0: {
        _1 = alloc<sbox>(const u64 1);
        check(overwrite: (*_1).0) -> bb1 #31:1;
    }
    bb1: {
        _3 = const u64 0;
        (*_1).0 = call fn main.fill(copy _1) #31:1;
        _2 = move (*_1).0;
        del(move _2);
        del(move _1);
        _0 = const i32 0;
        return;
    }
}

fn main.g() -> i32 {
    let _1: sbox mut* own;
    let _2: i32 mut* own;
    bb0: {
        _1 = alloc<sbox>(const u64 1);
        check(overwrite: (*_1).0) -> bb1 #41:1;
    }
    bb1: {
        goto bb2;
    }
    bb2: {
        (*_1).0 = call fn main.fill(copy _1) #41:1;
        _2 = move (*_1).0;
        del(move _2);
        del(move _1);
        _0 = const i32 0;
        return;
    }
}

fn main.h() -> i32 {
    let _1: sbox mut* own;
    let _2: i32 mut* own;
    let _3: i32 mut* own;
    bb0: {
        _1 = alloc<sbox>(const u64 1);
        _3 = alloc<i32>(const u64 1);
        check(overwrite: (*_1).0) -> bb1 #51:1;
    }
    bb1: {
        goto bb2;
    }
    bb2: {
        (*_1).0 = move _3 #51:1;
        _2 = move (*_1).0;
        del(move _2);
        del(move _1);
        _0 = const i32 0;
        return;
    }
}
