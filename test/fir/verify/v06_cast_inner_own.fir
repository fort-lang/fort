//! pass: verify
//! panic: fir.verify: V6 a cast adds own: main.f bb0 statement 0 (s1)
// V6: the `move` carries the `own` of the outermost level of `u8 mut** own`, and the target
// marks the level behind it `own` as well, where the operand has none (D3.14). The place that
// the cast fills owns, so V6 refuses the cast itself and not the destination.
fn main.f(_1 (q): u8 mut** own) -> void {
    let _2: u8 mut* own* own;

    bb0: {
        _2 = cast<u8 mut* own* own>(move _1) #2:5 s1;
        del(move _2) #3:5 s2;
        return #4:1 s0;
    }
}
