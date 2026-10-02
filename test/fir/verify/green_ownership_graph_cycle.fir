//! pass: verify
//! prelude:
//| fn left() void { right(); }
//| fn right() void { left(); }
//! expect:
//| fn main.left() -> void {
//|     let _0: void;
//|     let _1: void;
//|
//|     bb0: {
//|         _1 = call fn main.right() #1:1 s1;
//|         return #1:1 s0;
//|     }
//| }
//|
//| fn main.right() -> void {
//|     let _0: void;
//|     let _1: void;
//|
//|     bb0: {
//|         _1 = call fn main.left() #1:1 s1;
//|         return #1:1 s0;
//|     }
//| }
fn main.left() -> void {
    let _0: void;
    let _1: void;

    bb0: {
        _1 = call fn main.right() #1:1 s1;
        return #1:1 s0;
    }
}

fn main.right() -> void {
    let _0: void;
    let _1: void;

    bb0: {
        _1 = call fn main.left() #1:1 s1;
        return #1:1 s0;
    }
}
