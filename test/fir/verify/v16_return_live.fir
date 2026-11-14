//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! panic: fir.verify: V16 a return with a live scratch temporary: main.f bb0 statement 1 (s2)
// V16: the `return` leaves the allocation of `_1` in a temporary, which no lvalue names. An
// owning temporary lands before the statement ends (D17.8), so this is no lowering.
fn main.f() -> void {
    let _1: u8 mut* own;

    bb0: {
        _1 = call fn main.grab() #2:5 s1;
        return #3:1 s2;
    }
}
