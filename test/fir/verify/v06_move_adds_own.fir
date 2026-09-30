//! pass: verify
//! panic: fir.verify: V6 a copy or a move adds own: main.f bb0 statement 0 (s1)
// V6: the `move` carries the `own` of the outermost level of `u8 mut** own`, and `_2` marks the
// level behind it `own` as well, where the operand has none. A move carries what its place owns
// and adds nothing (D3.14, D17.5), as a cast does in v06_cast_inner_own.fir.
fn main.f(_1 (q): u8 mut** own) -> void {
    let _2: u8 mut* own* own;

    bb0: {
        _2 = move _1 #2:5 s1;
        del(move _2) #3:5 s2;
        return #4:1 s0;
    }
}
