//! pass: verify
//! panic: fir.verify: V6 an owning place takes a copy or a view: main.f bb0 statement 0 (s1)
// V6: a `string` constant is a view of private data and owns nothing (D17.3, D17.9). Only
// `null T` and `zero T` fill an owning place as constants.
fn main.f() -> void {
    let _1: string own;

    bb0: {
        _1 = const "abc" #2:5 s1;
        return #3:1 s0;
    }
}
