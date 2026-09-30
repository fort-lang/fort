//! pass: verify
//! panic: fir.verify: V6 a copy or a move adds own: main.f bb0 statement 1 (s2)
// V6: `_4` holds a pointer to pointers that own nothing, and `_5` marks the level behind its
// pointer `own`. A copy lends what its place owns and adds no `own` at any level (D3.14, D17.4).
// `_5` does not own at its outermost level, so only this rule refuses the copy.
fn main.f(_1 (a): u8 mut**, _2 (b): i32, _3 (c): i32) -> void {
    let _4: u8 mut**;
    let _5: u8 mut* own*;

    bb0: {
        _4 = copy _1 #2:5 s1;
        _5 = copy _4 #3:5 s2;
        return #4:1 s0;
    }
}
