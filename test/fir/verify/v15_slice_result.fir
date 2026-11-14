//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 0 (s1)
// V15: a `slice` of a fixed array `_0` gives a span of the storage of `_0`. The store through
// the span puts an allocation into the result, and the store of statement 4 writes over it.
fn main.f() -> u8 mut* own[2] {
    let _1: u8 mut* own mut@;
    let _2: u64;
    let _3: u8 mut* own;
    bb0: {
        _1 = slice mut(_0, const u64 0, const u64 2) #2:5 s1;
        _2 = const u64 0 #3:5 s2;
        _3 = call fn main.grab() #4:5 s3;
        _1[_2] = move _3 #4:5 s3;
        _0 = aggregate u8 mut* own[2](const null u8 mut* own, const null u8 mut* own) #5:5 s4;
        return #6:5 s5;
    }
}
