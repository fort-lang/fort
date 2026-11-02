//! pass: verify
//! panic: fir.verify: V4 a cast that D3.14 refuses: main.f bb0 statement 0 (s1)
// V4: no cast makes a function pointer from a `void*` (D3.14), in FIR text as in fort source,
// where the checker reports the function pointer that the cast adds (spec/fir.md 4).
fn main.f(_1 (v): void*) -> fn () void {
    bb0: {
        _0 = cast<fn () void>(copy _1) #2:12 s1;
        return #2:5 s1;
    }
}
