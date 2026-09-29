//! pass: verify
//! panic: fir.verify: V3 a write into a global constant: main.f bb0 statement 0 (s1)
//! prelude:
//| u8 mut* own KEPT = null;
// V3: a `move` empties its place, so a `move` or a `del` of a global constant writes read-only
// memory (D17.6, D7.10).
fn main.f() -> void {
    bb0: {
        del(move global main.KEPT) #2:5 s1;
        return #3:1 s0;
    }
}
