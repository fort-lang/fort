//! pass: verify
//! panic: fir.verify: V4 an aggregate with padding and no zeroed into a temporary or _0: main.f bb1 statement 0 (s2)
//! prelude:
//| struct holed {
//|     u8 a;
//|     u64 b;
//| }
// V4: `_0` of an aggregate type is the destination of the caller in the translation and new
// storage in the interpreter. `aggregate T(a, ...)` with no `zeroed` leaves each padding byte of
// its destination as it was (spec/fir.md 6), so the two would give the caller different padding.
// The store in `bb0` has the mark and is legal; the store in `bb1` has no mark.
fn main.f(_1 (b): u64) -> holed {
    bb0: {
        _0 = aggregate zeroed holed(const u8 1, copy _1) #2:12 s1;
        goto bb1 #2:5 s1;
    }
    bb1: {
        _0 = aggregate holed(const u8 2, copy _1) #3:12 s2;
        return #3:5 s2;
    }
}
