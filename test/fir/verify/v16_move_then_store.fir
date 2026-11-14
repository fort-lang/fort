//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//| fn pass(u8 mut* own p) u8 mut* own {
//|     return p;
//| }
//! panic: fir.verify: V16 a return with a live scratch temporary: main.f bb1 statement 0 (s3)
// V16: statement 1 of bb0 moves `_1` and then stores into it. The `move` comes first, so the
// store leaves `_1` live at the end of bb0, and the `return` of bb1 leaves the allocation.
fn main.f() -> void {
    let _1: u8 mut* own;
    bb0: {
        _1 = call fn main.grab() #2:5 s1;
        _1 = call fn main.pass(move _1) #3:5 s2;
        goto bb1 #3:5 s2;
    }
    bb1: {
        return #4:5 s3;
    }
}
