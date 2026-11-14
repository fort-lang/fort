//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| fn put(u8 mut* own mut* p) void {
//|     del(*p);
//|     *p = new(u8);
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 0 (s1)
// V15: the address of a member of `_0` is an address of fixed storage of `_0`. `put` stores an
// allocation through it, and the store of statement 2 writes over that allocation.
fn main.f() -> holder {
    let _1: u8 mut* own mut*;
    let _2: void;
    bb0: {
        _1 = addr mut(_0.0) #2:5 s1;
        _2 = call fn main.put(copy _1) #3:5 s2;
        _0 = aggregate holder(const null u8 mut* own, const u64 1) #4:5 s3;
        return #5:5 s4;
    }
}
