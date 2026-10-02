//! pass: verify
//! expect:
//| fn main.release_zero() -> void {
//|     let _0: void;
//|     let _1 (buf): u8 mut@ own;
//|     let _2: u8 mut* own;
//|     let _3: u64;
//|
//|     bb0: {
//|         _3 = const u64 0 #1:1 s0;
//|         _2 = alloc<u8>(copy _3) #2:1 s1;
//|         _1 = aggregate u8 mut@ own(move _2, copy _3) #3:1 s2;
//|         del(move _1) #4:1 s3;
//|         dead(_1) #5:1 s4;
//|         return #6:1 s5;
//|     }
//| }
//|
//| fn main.move_zero() -> void {
//|     let _0: void;
//|     let _1 (src): u8 mut@ own;
//|     let _2 (dst): u8 mut@ own;
//|     let _3: u8 mut@;
//|     let _4: u8 mut* own;
//|     let _5: u64;
//|
//|     bb0: {
//|         _5 = const u64 0 #1:1 s0;
//|         _4 = alloc<u8>(copy _5) #2:1 s1;
//|         _1 = aggregate u8 mut@ own(move _4, copy _5) #3:1 s2;
//|         _3 = copy _1 #4:1 s3;
//|         _2 = move _1 #5:1 s4;
//|         del(move _2) #6:1 s5;
//|         dead(_2) #7:1 s6;
//|         dead(_1) #8:1 s7;
//|         return #9:1 s8;
//|     }
//| }
//|
//| fn main.dead_borrow_replacement() -> void {
//|     let _0: void;
//|     let _1 (owner): u8 mut* own;
//|     let _2 (view): u8 mut*;
//|
//|     bb0: {
//|         _1 = alloc<u8>(const u64 1) #1:1 s0;
//|         _2 = copy _1 #2:1 s1;
//|         del(move _1) #3:1 s2;
//|         _2 = const null u8 mut* #4:1 s3;
//|         dead(_2) #5:1 s4;
//|         dead(_1) #6:1 s5;
//|         return #7:1 s6;
//|     }
//| }
// A zero-element allocation has an owner until del or move.
// The verifier checks FIR structure. The ownership transfer test checks proof states.
fn main.release_zero() -> void {
    let _1 (buf): u8 mut@ own;
    let _2: u8 mut* own;
    let _3: u64;

    bb0: {
        _3 = const u64 0 #1:1 s0;
        _2 = alloc<u8>(copy _3) #2:1 s1;
        _1 = aggregate u8 mut@ own(move _2, copy _3) #3:1 s2;
        del(move _1) #4:1 s3;
        dead(_1) #5:1 s4;
        return #6:1 s5;
    }
}

fn main.move_zero() -> void {
    let _1 (src): u8 mut@ own;
    let _2 (dst): u8 mut@ own;
    let _3: u8 mut@;
    let _4: u8 mut* own;
    let _5: u64;

    bb0: {
        _5 = const u64 0 #1:1 s0;
        _4 = alloc<u8>(copy _5) #2:1 s1;
        _1 = aggregate u8 mut@ own(move _4, copy _5) #3:1 s2;
        _3 = copy _1 #4:1 s3;
        _2 = move _1 #5:1 s4;
        del(move _2) #6:1 s5;
        dead(_2) #7:1 s6;
        dead(_1) #8:1 s7;
        return #9:1 s8;
    }
}

fn main.dead_borrow_replacement() -> void {
    let _1 (owner): u8 mut* own;
    let _2 (view): u8 mut*;

    bb0: {
        _1 = alloc<u8>(const u64 1) #1:1 s0;
        _2 = copy _1 #2:1 s1;
        del(move _1) #3:1 s2;
        _2 = const null u8 mut* #4:1 s3;
        dead(_2) #5:1 s4;
        dead(_1) #6:1 s5;
        return #7:1 s6;
    }
}
