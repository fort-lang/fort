//! pass: verify
//! panic: fir.verify: V6 an owning parameter takes a copy or a view: main.f bb0 statement 0 (s1)
//! prelude:
//| fn take(u8 mut* own p) void {
//|     del(p);
//| }
// V6: an argument for a parameter of an owning type is a move or a constant (D17.5).
fn main.f(_1 (q): u8 mut* own) -> void {
    let _2: void;

    bb0: {
        _2 = call fn main.take(copy _1) #2:5 s1;
        return #3:1 s0;
    }
}
