//! pass: verify
//! panic: fir.verify: V12 a read of a temporary after a move of it: main.f bb0 statement 2 (s2)
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
// V12: a read of a temporary after a move of it. The translator does not zero a temporary that a
// move empties, so the copy would lend a pointer that `_2` owns.
fn main.f() -> void {
    let _1: u8 mut* own;
    let _2: u8 mut* own;
    let _3: u8 mut*;

    bb0: {
        _1 = call fn main.grab() #2:5 s1;
        _2 = move _1 #2:5 s1;
        _3 = copy _1 #3:5 s2;
        del(move _2) #4:5 s3;
        return #5:1 s0;
    }
}
