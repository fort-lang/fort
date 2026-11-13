//! pass: verify
//! panic: fir.verify: V4 an aggregate with padding and no zeroed into a temporary or _0: main.f bb0 statement 0 (s1)
//! prelude:
//| struct holed {
//|     u8 a;
//|     u64 b;
//| }
//| fn show(holed p) u64 {
//|     return p.b;
//| }
// V4: `aggregate T(a, ...)` with no `zeroed` leaves each padding byte of its destination as it
// was (spec/fir.md 6). No statement gives the padding of the temporary `_2` a value before this
// store, and `show` can read each byte of its argument through a view. The lowering writes
// `aggregate zeroed` here (D10.1).
fn main.f(_1 (b): u64) -> u64 {
    let _2: holed;

    bb0: {
        _2 = aggregate holed(const u8 1, copy _1) #2:12 s1;
        _0 = call fn main.show(copy _2) #2:12 s1;
        return #2:5 s1;
    }
}
