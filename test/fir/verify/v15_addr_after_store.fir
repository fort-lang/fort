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
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb1 statement 0 (s2)
// V15: an address of `_0` after its one store is an address of `_0`. The rule reads no order: a
// loop can bring a store after the write through the address. The first violation is the one of
// the first block that holds one, so the address of bb1 comes before the one of bb2.
fn main.f(_1 (c): bool) -> holder {
    let _2: holder mut*;
    let _3: void;
    bb0: {
        _0 = aggregate holder(const null u8 mut* own, const u64 1) #2:5 s1;
        switch(copy _1) -> [true: bb1, otherwise: bb2] #3:5 s1;
    }
    bb1: {
        _2 = addr mut(_0) #3:5 s2;
        _3 = call fn main.fill(copy _2) #4:5 s3;
        return #5:5 s4;
    }
    bb2: {
        _2 = addr mut(_0) #6:5 s5;
        return #7:5 s6;
    }
}
