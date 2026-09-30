//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//| fn take(u8 mut* own p) void {
//|     del(p);
//| }
//! expect:
//| fn main.f(_1 (c): bool) -> void {
//|     let _0: void;
//|     let _2: u8 mut* own;
//|     let _3: u8 mut*;
//|     let _4: void;
//|
//|     bb0: {
//|         goto bb1 #2:5 s1;
//|     }
//|     bb1: {
//|         _2 = call fn main.grab() #3:9 s2;
//|         _3 = copy _2 #4:9 s3;
//|         _4 = call fn main.take(move _2) #5:9 s4;
//|         switch(copy _1) -> [true: bb1, otherwise: bb2] #2:5 s1;
//|     }
//|     bb2: {
//|         return #7:1 s0;
//|     }
//| }
// V12: the loop assigns `_2` again before it reads it, so no read follows the move of the last
// iteration.
fn main.f(_1 (c): bool) -> void {
    let _2: u8 mut* own;
    let _3: u8 mut*;
    let _4: void;

    bb0: {
        goto bb1 #2:5 s1;
    }
    bb1: {
        _2 = call fn main.grab() #3:9 s2;
        _3 = copy _2 #4:9 s3;
        _4 = call fn main.take(move _2) #5:9 s4;
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:5 s1;
    }
    bb2: {
        return #7:1 s0;
    }
}
