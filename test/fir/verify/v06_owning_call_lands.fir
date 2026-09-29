//! pass: verify
//! panic: fir.verify: V6 an owning value in a place that does not own: main.f bb0 statement 0 (s1)
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
// V6: an owning value lands only in an owning place, an `own` parameter, an owning member, the
// operand of a cast to an owning type, or a `del` (D17.8). Nothing frees what a place that does
// not own holds.
fn main.f() -> void {
    let _1: u8 mut*;

    bb0: {
        _1 = call fn main.grab() #2:5 s1;
        return #3:1 s0;
    }
}
