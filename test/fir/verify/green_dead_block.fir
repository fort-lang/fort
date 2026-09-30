//! pass: verify
//! expect:
//| fn main.f() -> i32 {
//|     let _0: i32;
//|     let _1: i32;
//|
//|     bb0: {
//|         _0 = const i32 1 #2:12 s1;
//|         return #2:5 s1;
//|     }
//|     bb1: {
//|         _0 = copy _1 #3:12 s2;
//|         unreachable #4:1 s0;
//|     }
//| }
// V9 and V13 on a block that nothing names, such as the block after a terminator (D14.2). No path
// from bb0 reaches it, so its unreachable keeps V9, and it starts with every temporary assigned,
// so its read of `_1` keeps V13.
fn main.f() -> i32 {
    let _1: i32;

    bb0: {
        _0 = const i32 1 #2:12 s1;
        return #2:5 s1;
    }
    bb1: {
        _0 = copy _1 #3:12 s2;
        unreachable #4:1 s0;
    }
}
