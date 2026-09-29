//! pass: verify
//! panic: fir.verify: V6 an owning member takes a copy or a view: main.f bb0 statement 0 (s1)
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
// V6: a member of an aggregate for a field of an owning type is a move or a constant (D17.5).
// The rvalue itself produces an owning value, so the place of the assignment keeps V6.
fn main.f(_1 (q): u8 mut* own) -> holder {
    bb0: {
        _0 = aggregate holder(copy _1, const u64 0) #2:12 s1;
        return #2:5 s1;
    }
}
