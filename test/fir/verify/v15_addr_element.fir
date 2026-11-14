//! pass: verify
//! prelude:
//| fn put(u8 mut* own mut* p) void {
//|     del(*p);
//|     *p = new(u8);
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 1 (s2)
// V15: the address of an element of a fixed array `_0` is an address of fixed storage of `_0`.
fn main.f() -> u8 mut* own[2] {
    let _1: u64;
    let _2: u8 mut* own mut*;
    let _3: void;
    bb0: {
        _1 = const u64 1 #2:5 s1;
        _2 = addr mut(_0[_1]) #3:5 s2;
        _3 = call fn main.put(copy _2) #4:5 s3;
        _0 = aggregate u8 mut* own[2](const null u8 mut* own, const null u8 mut* own) #5:5 s4;
        return #6:5 s5;
    }
}
