//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! panic: fir.verify: V16 a store over a live scratch local: main.f bb0 statement 4 (s0)
// V16: a store of the zero value into the length of the span `_1` leaves the pointer of `_1`
// as it was, so `_1` stays live, and the second whole store goes over its allocation. Only a
// zero store into the whole local empties it.
fn main.f() -> void {
    let _1: u8 mut@ own;
    let _2: u8 mut* own;
    let _3: u8 mut* own;

    bb0: {
        _2 = call fn main.grab();
        _1 = aggregate u8 mut@ own(move _2, const u64 1);
        _1.1 = const zero u64;
        _3 = call fn main.grab();
        _1 = aggregate u8 mut@ own(move _3, const u64 1);
        del(move _1);
        return;
    }
}
