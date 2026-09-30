//! pass: verify
//! panic: fir.verify: V6 a cast adds own: main.f bb0 statement 2 (s1)
// V6: `u8 mut* own p = cast(s.ptr, u8 mut* own);` with `s: u8 mut@ own`. Field 0 of the span
// carries `own` so that an `aggregate` builds an owning span, but a copy of it is the `.ptr` of
// the source, a view (spec/fir.md 5.5, D17.3). A cast never adds `own` (D3.14), so the checker
// refuses this program, and the verifier refuses its FIR.
fn main.f(_1 (s): u8 mut@ own) -> void {
    let _2 (p): u8 mut* own #2:14;
    let _3: u8 mut* own #2:18;

    bb0: {
        _2 = const zero u8 mut* own #2:14 s0;
        live(_2) #2:14 s1;
        _3 = cast<u8 mut* own>(copy _1.0) #2:18 s1;
        check(overwrite: _2) -> bb1 #2:14 s1;
    }
    bb1: {
        _2 = move _3 #2:14 s1;
        dead(_2) #3:1 s0;
        return #3:1 s0;
    }
}
