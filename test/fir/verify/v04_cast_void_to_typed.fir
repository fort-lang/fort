//! pass: verify
//! panic: fir.verify: V4 a cast that D3.14 refuses: main.f bb0 statement 0 (s1)
// V4: no cast gives a type to a `void*` (D3.14), and FIR text has no row for it: `cast<T>` covers
// the conversions of D3.14 and the row from `u64` alone (spec/fir.md 4).
fn main.f(_1 (v): void*) -> i64* {
    bb0: {
        _0 = cast<i64*>(copy _1) #2:12 s1;
        return #2:5 s1;
    }
}
