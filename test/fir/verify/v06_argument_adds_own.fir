//! pass: verify
//! panic: fir.verify: V6 a copy or a move adds own: main.f bb0 statement 2 (s1)
//! prelude:
//| fn view(u8 mut* own* p, i32 x) void {
//| }
//| fn bump() i32 {
//|     return 0;
//| }
// V6 on the materialized form of `view(a, bump())` with `a: u8 mut**`. The parameter marks the
// level behind its pointer `own` and the argument does not, so the copy would add `own` there
// (D3.14, D17.4). green_lent_pointer.fir passes a `u8 mut* own*`, which carries that `own`.
fn main.f(_1 (a): u8 mut**) -> void {
    let _2: u8 mut**;
    let _3: i32;
    let _4: void;

    bb0: {
        _2 = copy _1 #2:10 s1;
        _3 = call fn main.bump() #2:13 s1;
        _4 = call fn main.view(copy _2, copy _3) #2:5 s1;
        return #3:1 s0;
    }
}
