//! pass: verify
//! panic: fir.verify: V6 a cast copies an owning place: main.f bb0 statement 0 (s1)
// V6: a cast of an owning place to an owning type is a copy, which D17.5 writes
// `cast(move(x), T)` (D3.14). The cast is refused whatever place it fills.
fn main.f(_1 (b): u8 mut@ own) -> void {
    let _2: u8@;

    bb0: {
        _2 = cast<u8@ own>(copy _1) #2:5 s1;
        return #3:1 s0;
    }
}
