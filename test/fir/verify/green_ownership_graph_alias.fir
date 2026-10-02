//! pass: verify
//! prelude:
//| struct handler { fn () void cb; }
//| fn first() void {}
//| fn second() void {}
//| fn caller() void {}
//! expect:
//| fn main.caller() -> void {
//|     let _0: void;
//|     let _1: handler;
//|     let _2: handler mut*;
//|     let _3: void;
//|
//|     bb0: {
//|         _1 = aggregate handler(const fn main.first) #1:1 s1;
//|         _2 = addr mut(_1) #1:1 s2;
//|         (*_2).0 = const fn main.second #1:1 s3;
//|         _3 = call copy _1.0() #1:1 s4;
//|         return #1:1 s0;
//|     }
//| }
fn main.caller() -> void {
    let _0: void;
    let _1: handler;
    let _2: handler mut*;
    let _3: void;

    bb0: {
        _1 = aggregate handler(const fn main.first) #1:1 s1;
        _2 = addr mut(_1) #1:1 s2;
        (*_2).0 = const fn main.second #1:1 s3;
        _3 = call copy _1.0() #1:1 s4;
        return #1:1 s0;
    }
}
