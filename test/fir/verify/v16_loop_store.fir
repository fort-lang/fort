//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! panic: fir.verify: V16 a store over a live scratch local: main.f bb1 statement 0 (s2)
// V16: the back edge of bb1 brings the allocation of `_2` to the store of the next iteration.
// The path from bb0 reaches the store with `_2` empty.
fn main.f(_1 (c): bool) -> void {
    let _2: u8 mut* own;

    bb0: {
        goto bb1 #2:5 s1;
    }
    bb1: {
        _2 = call fn main.grab() #3:9 s2;
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:12 s1;
    }
    bb2: {
        del(move _2) #5:5 s3;
        return #6:1 s4;
    }
}
