//! pass: verify
//! panic: fir.verify: V4 a regrow count that is not an i64 or a u64: main.f bb1 statement 2 (s0)
// V4: the count of a `regrow` is an `i64`, which the entry reads as a signed count, or a `u64`
// (spec/fir.md 6). The lowering casts each other integer type first.
fn main.f(_1 (n): u64) -> u64 {
    let _2 (s): u8 mut@ own;
    let _3: u8 mut* own;
    let _4: u8 mut@ own;
    let _5: u8 mut@ own;
    let _6: u8 mut* own;
    let _7: u8 mut@ own;

    bb0: {
        _2 = const zero u8 mut@ own #9:1;
        live(_2) #9:1;
        _3 = alloc<u8>(const u64 2) #10:1;
        _4 = aggregate u8 mut@ own(move _3, const u64 2) #10:1;
        check(overwrite: _2) -> bb1 #10:2;
    }
    bb1: {
        _2 = move _4 #10:2;
        _5 = move _2 #11:1;
        _6 = regrow<u8>(move _5, const u32 4) #11:2;
        _7 = aggregate u8 mut@ own(move _6, copy _1) #11:2;
        check(overwrite: _2) -> bb2 #11:3;
    }
    bb2: {
        _2 = move _7 #11:3;
        _0 = copy _2.1 #12:1;
        del(move _2) #13:1;
        dead(_2) #14:1;
        return #14:1;
    }
}
