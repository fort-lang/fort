//! pass: verify
//! panic: fir.verify: V12 a read of a temporary after a move of it: main.f bb1 statement 0 (s2)
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//| fn take(u8 mut* own p) void {
//|     del(p);
//| }
// V12: the loop moves `_2` in one iteration and reads it in the next. The back edge carries the
// move to the read.
fn main.f(_1 (c): bool) -> void {
    let _2: u8 mut* own;
    let _3: u8 mut*;
    let _4: void;

    bb0: {
        _2 = call fn main.grab() #2:5 s1;
        goto bb1 #3:5 s2;
    }
    bb1: {
        _3 = copy _2 #4:9 s2;
        _4 = call fn main.take(move _2) #5:9 s3;
        switch(copy _1) -> [true: bb1, otherwise: bb2] #3:5 s2;
    }
    bb2: {
        return #7:1 s0;
    }
}
