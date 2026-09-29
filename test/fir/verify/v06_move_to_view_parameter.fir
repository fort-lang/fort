//! pass: verify
//! panic: fir.verify: V6 a move where no owning position takes it: main.f bb0 statement 0 (s1)
//! prelude:
//| fn look(u8 mut* p) void {
//| }
// V6: a parameter that does not own takes a lend, a `copy`, and not a `move` (D17.4, D17.8).
fn main.f(_1 (p): u8 mut* own) -> void {
    let _2: void;

    bb0: {
        _2 = call fn main.look(move _1) #2:5 s1;
        return #3:1 s0;
    }
}
