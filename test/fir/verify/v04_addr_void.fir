//! pass: verify
//! panic: fir.verify: V4 an addr of a place of type void: main.f bb0 statement 0 (s1)
// V4: `addr` takes a place whose type is not `void`: `void` has no storage (spec/fir.md 5.3), and
// no pointer type has the element `void`.
fn main.f() -> void {
    let _1: i32*;

    bb0: {
        _1 = addr(_0) #2:5 s1;
        return #3:1 s0;
    }
}
