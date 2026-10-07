//! pass: ownership-local
//! error: ownership_local/002_invalid_use.fir:17:9: error: invalid use of 'q'
//! error: ownership_local/002_invalid_use.fir:17:9: note: reference source is no longer live
// D17.14: a release ends the source of each borrow of the allocation. A later read through
// the borrow is an invalid use.
fn main.reuse() -> i32 {
    let _0: i32;
    let _1 (p): i32 mut* own #8:5;
    let _2 (q): i32*;

    bb0: {
        live(_1) #12:9 s0;
        _1 = alloc<i32>(const u64 1) #13:9 s0;
        live(_2) #14:9 s1;
        _2 = copy _1 #15:9 s1;
        del(move _1) #16:9 s2;
        _0 = copy (*_2) #17:9 s3;
        dead(_2) #18:9 s4;
        dead(_1) #19:9 s4;
        return #20:9 s4;
    }
}
