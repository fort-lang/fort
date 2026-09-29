//! pass: verify
//! panic: fir.verify: V6 a move where no owning position takes it: main.f bb0 statement 0 (s1)
// V6: `eq` reads its operands and keeps neither, so a `move` there empties an owning place into
// nothing (D17.8).
fn main.f(_1 (p): u8 mut* own) -> bool {
    bb0: {
        _0 = eq(move _1, const null u8 mut* own) #2:12 s1;
        return #2:5 s1;
    }
}
