//! pass: verify
//! panic: fir.verify: V6 an owning place takes a copy or a view: main.f bb0 statement 0 (s1)
// V6: a `bytes` constant is the address of a private copy, a view that owns nothing (D17.9).
fn main.f() -> void {
    let _1: char* own;

    bb0: {
        _1 = const bytes "abc" #2:5 s1;
        return #3:1 s0;
    }
}
