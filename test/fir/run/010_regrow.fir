//! pass: run main.f 5
//! expect:
//| return u64 5
// A `regrow` of a span of 2 elements to 5 gives a span of 5 elements (D12.2). The run makes no
// event: the `move` empties `s` before the store, and the `del` releases the new storage.
// The text is the lowering of `u8 mut@ own mut s = new(u8, 2); s = regrow(move(s), n);` then
// `u64 r = s.len; del(s); return r;` (spec/fir.md 9.4): the span moves into `_5`, `regrow` gives
// the new storage, and `aggregate` builds the span of `n` elements.
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
