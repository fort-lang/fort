//! pass: verify
//! panic: fir.verify: V3 a write into a void return place: main.f bb0 statement 0 (s1)
//! prelude:
//| fn nothing() void {
//| }
// V3: `_0` of a `void` function has the type `void`, and nothing writes it (spec/fir.md 5.3).
// A call of a `void` function writes a `void` temporary instead.
fn main.f() -> void {
    bb0: {
        _0 = call fn main.nothing() #2:5 s1;
        return #3:1 s0;
    }
}
