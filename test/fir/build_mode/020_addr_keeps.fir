//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.pointed() -> i32 {
//|     let _0: i32;
//|     let _1: i32;
//|     let _2: i32*;
//|     let _3: i32[2];
//|     let _4: i32@;
//|
//|     bb0: {
//|         _1 = const i32 7;
//|         _2 = addr(_1);
//|         _3 = aggregate i32[2](const i32 1, const i32 2);
//|         _4 = slice(_3, const u64 0, const u64 2);
//|         _0 = copy (*_2);
//|         return;
//|     }
//| }
// An `addr` or a `slice` of a temporary is a use of it, because the pointer or the span reads it
// later. Nothing else reads `_1` or `_3`, and both assignments stay. `_4` has no reader, but a
// `slice` is not a rvalue that the pass removes.
fn main.pointed() -> i32 {
    let _1: i32;
    let _2: i32*;
    let _3: i32[2];
    let _4: i32@;

    bb0: {
        _1 = const i32 7;
        _2 = addr(_1);
        _3 = aggregate i32[2](const i32 1, const i32 2);
        _4 = slice(_3, const u64 0, const u64 2);
        _0 = copy (*_2);
        return;
    }
}
