//! pass: verify
//! panic: fir.verify: V6 an owning place takes a copy or a view: main.f bb0 statement 1 (s2)
// V6: an assignment into a place of an owning type takes a move, a constant or an rvalue that
// produces an owning value (D17.5). A copy of an owning place would give one allocation two
// owners (D17.4).
fn main.f(_1 (q): u8 mut* own) -> void {
    let _2 (p): u8 mut* own #2:5;

    bb0: {
        live(_2) #2:5 s1;
        _2 = copy _1 #2:22 s2;
        del(move _2) #3:5 s3;
        del(move _1) #4:5 s4;
        dead(_2) #5:1 s0;
        return #5:1 s0;
    }
}
