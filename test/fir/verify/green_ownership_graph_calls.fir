//! pass: verify
//! prelude:
//| extern fn ext() void;
//| fn leaf() void {}
//| fn invoke(fn () void cb) void { cb(); }
//| fn caller() void { invoke(leaf); ext(); }
//! expect:
//| fn main.leaf() -> void {
//|     let _0: void;
//|
//|     bb0: {
//|         return #1:1 s0;
//|     }
//| }
//|
//| fn main.invoke(_1 (cb): fn () void) -> void {
//|     let _0: void;
//|     let _2: void;
//|
//|     bb0: {
//|         _2 = call copy _1() #1:1 s1;
//|         return #1:1 s0;
//|     }
//| }
//|
//| fn main.caller() -> void {
//|     let _0: void;
//|     let _1: void;
//|     let _2: void;
//|
//|     bb0: {
//|         _1 = call fn main.invoke(const fn main.leaf) #1:1 s1;
//|         _2 = call extern ext() #1:1 s2;
//|         return #1:1 s0;
//|     }
//| }
fn main.leaf() -> void {
    let _0: void;

    bb0: {
        return #1:1 s0;
    }
}

fn main.invoke(_1 (cb): fn () void) -> void {
    let _0: void;
    let _2: void;

    bb0: {
        _2 = call copy _1() #1:1 s1;
        return #1:1 s0;
    }
}

fn main.caller() -> void {
    let _0: void;
    let _1: void;
    let _2: void;

    bb0: {
        _1 = call fn main.invoke(const fn main.leaf) #1:1 s1;
        _2 = call extern ext() #1:1 s2;
        return #1:1 s0;
    }
}
