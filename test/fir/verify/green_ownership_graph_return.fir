//! pass: verify
//! prelude:
//| struct handler { fn () void cb; }
//| fn first() void {}
//| fn invoke(fn () handler maker) void {}
//! expect:
//| fn main.invoke(_1 (maker): fn () handler) -> void {
//|     let _0: void;
//|     let _2: handler;
//|     let _3: void;
//|
//|     bb0: {
//|         _2 = aggregate handler(const fn main.first) #1:1 s1;
//|         _2 = call copy _1() #1:1 s2;
//|         _3 = call copy _2.0() #1:1 s3;
//|         return #1:1 s0;
//|     }
//| }
fn main.invoke(_1 (maker): fn () handler) -> void {
    let _0: void;
    let _2: handler;
    let _3: void;

    bb0: {
        _2 = aggregate handler(const fn main.first) #1:1 s1;
        _2 = call copy _1() #1:1 s2;
        _3 = call copy _2.0() #1:1 s3;
        return #1:1 s0;
    }
}
