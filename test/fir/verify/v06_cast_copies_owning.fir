//! pass: verify
//! panic: fir.verify: V6 a cast adds own: main.f bb0 statement 0 (s1)
// V6: a copy of an owning place lends it (D17.4), so a cast of it to an owning type adds `own`
// (D3.14). D17.5 writes the transfer `cast(move(x), T)`. The cast is refused whatever place it
// fills.
fn main.f(_1 (b): u8 mut@ own) -> void {
    let _2: u8@;

    bb0: {
        _2 = cast<u8@ own>(copy _1) #2:5 s1;
        return #3:1 s0;
    }
}
