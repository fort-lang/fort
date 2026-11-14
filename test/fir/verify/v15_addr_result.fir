//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| fn fill(holder mut* h) void {
//|     del(h->p);
//|     h->p = new(u8);
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 0 (s1)
// V15: the address of `_0` goes to `fill`, which stores an allocation into the result. That
// write is no store into `_0` of this function, so the rule cannot count it. owner.prover gives
// the store of statement 2 no proof, and the store writes over the allocation.
fn main.f() -> holder {
    let _1: holder mut*;
    let _2: void;
    bb0: {
        _1 = addr mut(_0) #2:5 s1;
        _2 = call fn main.fill(copy _1) #3:5 s2;
        _0 = aggregate holder(const null u8 mut* own, const u64 1) #4:5 s3;
        return #5:5 s4;
    }
}
