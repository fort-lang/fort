//! pass: verify
//! panic: fir.verify: V6 a cast adds own: main.f bb0 statement 0 (s1)
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
// V6: a cast to the owning struct `holder` takes a `move`. A constant owns nothing (D17.3), so
// `cast<holder>(const zero holder)` adds `own` (D3.14); spec/fir.md 9.4 puts the constant into a
// temporary first, as green_cast_constant.fir shows.
fn main.f() -> holder {
    bb0: {
        _0 = cast<holder>(const zero holder) #2:12 s1;
        return #2:5 s1;
    }
}
