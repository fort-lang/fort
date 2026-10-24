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
//| fn make() i32 mut* own {
//|     return new(i32);
//| }
//! expect:
//| fn main.f() -> i32 {
//|     let _0: i32;
//|     let _1: sbox mut* own;
//|     let _2: i32 mut* own;
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
//| fn main.k() -> void {
//|     let _0: void;
//|     let _1 (p): i32 mut* own;
//|
//|     bb0: {
//|         _1 = const zero i32 mut* own;
//|         _1 = call fn main.make() #41:1 s0;
//|         del(move _1);
//|         return;
//|     }
//| }
// The call of the store after a check runs before the store writes, and `fill` fills `(*_1).0`
// there. So the emptiness pass does not prove the check of `f`, and the check stays (spec/fir.md
// 11, Proof). No call fills the local `_1` of `k`, which is no memory, so its check goes.
fn main.f() -> i32 {
    let _1: sbox mut* own;
    let _2: i32 mut* own;
    bb0: {
        _1 = alloc<sbox>(const u64 1);
        check(overwrite: (*_1).0) -> bb1 #31:1;
    }
    bb1: {
        (*_1).0 = call fn main.fill(copy _1) #31:1;
        _2 = move (*_1).0;
        del(move _2);
        del(move _1);
        _0 = const i32 0;
        return;
    }
}

fn main.k() -> void {
    let _1 (p): i32 mut* own;
    bb0: {
        _1 = const zero i32 mut* own;
        check(overwrite: _1) -> bb1 #41:1;
    }
    bb1: {
        _1 = call fn main.make() #41:1;
        del(move _1);
        return;
    }
}
