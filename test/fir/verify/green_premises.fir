//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! expect:
//| fn main.f(_1 (c): bool) -> holder {
//|     let _0: holder;
//|     let _2 (p): u8 mut* own #2:5;
//|     let _3: void;
//|     let _4: u8 mut* own;
//|     let _5: u8 mut* own;
//|     let _6: u8 mut* own;
//|     let _7: u8 mut* own mut*;
//|     let _8: u8 mut* own;
//|     let _9 (n): i32 #2:5;
//|
//|     bb0: {
//|         _2 = const null u8 mut* own #1:1 s0;
//|         live(_9) #2:5 s0;
//|         _9 = const i32 1 #2:5 s0;
//|         _5 = const null u8 mut* own #1:1 s0;
//|         _5 = const null u8 mut* own #1:1 s0;
//|         _7 = addr mut(_6) #1:1 s0;
//|         _6 = call fn main.grab() #1:1 s0;
//|         _8 = move (*_7) #1:1 s0;
//|         _6 = call fn main.grab() #1:1 s0;
//|         del(move _8) #1:1 s0;
//|         goto bb1 #2:1 s1;
//|     }
//|     bb1: {
//|         live(_2) #2:5 s1;
//|         _4 = call fn main.grab() #2:5 s1;
//|         check(overwrite: _2) -> bb2 #2:5 s1;
//|     }
//|     bb2: {
//|         _2 = move _4 #2:5 s1;
//|         switch(copy _1) -> [true: bb3, otherwise: bb4] #3:9 s2;
//|     }
//|     bb3: {
//|         del(move _2) #4:9 s3;
//|         dead(_2) #4:9 s3;
//|         goto bb1 #4:9 s3;
//|     }
//|     bb4: {
//|         _0 = aggregate holder(move _2, const u64 1) #6:5 s4;
//|         dead(_2) #6:5 s4;
//|         del(move _6) #6:5 s4;
//|         return #6:5 s4;
//|     }
//| }
// V14 to V16 on the edges that keep them. Each `return` comes after the `dead` of `p`, whose
// entry zeroing in bb0 opens no scope. The named local `n` holds no owner, so it needs no `dead`.
// Each path stores the whole `_0` once. The loop fills the temporary `_4` again only after a
// `move` emptied it, and a store of `null` leaves `_5` empty. The address of `_6` makes it no
// scratch local: the `move` through `_7` empties it, and owner.prover proves the second store
// into it with a fact.
fn main.f(_1 (c): bool) -> holder {
    let _2 (p): u8 mut* own #2:5;
    let _3: void;
    let _4: u8 mut* own;
    let _5: u8 mut* own;
    let _6: u8 mut* own;
    let _7: u8 mut* own mut*;
    let _8: u8 mut* own;
    let _9 (n): i32 #2:5;

    bb0: {
        _2 = const null u8 mut* own #1:1 s0;
        live(_9) #2:5 s0;
        _9 = const i32 1 #2:5 s0;
        _5 = const null u8 mut* own #1:1 s0;
        _5 = const null u8 mut* own #1:1 s0;
        _7 = addr mut(_6) #1:1 s0;
        _6 = call fn main.grab() #1:1 s0;
        _8 = move (*_7) #1:1 s0;
        _6 = call fn main.grab() #1:1 s0;
        del(move _8) #1:1 s0;
        goto bb1 #2:1 s1;
    }
    bb1: {
        live(_2) #2:5 s1;
        _4 = call fn main.grab() #2:5 s1;
        check(overwrite: _2) -> bb2 #2:5 s1;
    }
    bb2: {
        _2 = move _4 #2:5 s1;
        switch(copy _1) -> [true: bb3, otherwise: bb4] #3:9 s2;
    }
    bb3: {
        del(move _2) #4:9 s3;
        dead(_2) #4:9 s3;
        goto bb1 #4:9 s3;
    }
    bb4: {
        _0 = aggregate holder(move _2, const u64 1) #6:5 s4;
        dead(_2) #6:5 s4;
        del(move _6) #6:5 s4;
        return #6:5 s4;
    }
}
