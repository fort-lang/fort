//! pass: run main.f 1
//! expect:
//| stderr "run/011_regrow_below_length.fir:11:2: runtime error: regrow count 1 below length 2\x0A"
//| abort
// A count below the length of the span is a runtime error at the position of the `regrow`
// (D12.2). The rvalue takes no `check`: the entry `std.rt.regrow` makes the test.
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
        _6 = regrow<u8>(move _5, copy _1) #11:2;
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
