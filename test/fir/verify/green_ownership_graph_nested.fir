//! pass: verify
//! prelude:
//| struct handler { fn () void cb; }
//| struct wrapper { handler value; }
//| fn first() void {}
//| fn second() void {}
//| fn nested() void {}
//| fn casted() void {}
//! expect:
//| fn main.nested() -> void {
//|     let _0: void;
//|     let _1: handler;
//|     let _2: wrapper;
//|     let _3: handler;
//|     let _4: void;
//|
//|     bb0: {
//|         _1 = aggregate handler(const fn main.second) #1:1 s1;
//|         _2 = aggregate wrapper(copy _1) #1:1 s2;
//|         _3 = aggregate handler(const fn main.first) #1:1 s3;
//|         _3 = copy _2.0 #1:1 s4;
//|         _4 = call copy _3.0() #1:1 s5;
//|         return #1:1 s0;
//|     }
//| }
//|
//| fn main.casted() -> void {
//|     let _0: void;
//|     let _1: handler;
//|     let _2: handler;
//|     let _3: void;
//|
//|     bb0: {
//|         _1 = aggregate handler(const fn main.second) #1:1 s1;
//|         _2 = aggregate handler(const fn main.first) #1:1 s2;
//|         _2 = cast<handler>(copy _1) #1:1 s3;
//|         _3 = call copy _2.0() #1:1 s4;
//|         return #1:1 s0;
//|     }
//| }
fn main.nested() -> void {
    let _0: void;
    let _1: handler;
    let _2: wrapper;
    let _3: handler;
    let _4: void;

    bb0: {
        _1 = aggregate handler(const fn main.second) #1:1 s1;
        _2 = aggregate wrapper(copy _1) #1:1 s2;
        _3 = aggregate handler(const fn main.first) #1:1 s3;
        _3 = copy _2.0 #1:1 s4;
        _4 = call copy _3.0() #1:1 s5;
        return #1:1 s0;
    }
}

fn main.casted() -> void {
    let _0: void;
    let _1: handler;
    let _2: handler;
    let _3: void;

    bb0: {
        _1 = aggregate handler(const fn main.second) #1:1 s1;
        _2 = aggregate handler(const fn main.first) #1:1 s2;
        _2 = cast<handler>(copy _1) #1:1 s3;
        _3 = call copy _2.0() #1:1 s4;
        return #1:1 s0;
    }
}
