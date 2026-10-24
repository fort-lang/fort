//! pass: run main.f
//! expect:
//| fault null dereference at 9:1
// The first argument is a null from a vacated slot; the second argument loads through a plain
// null. The run faults at the second argument, before memcpy runs.
fn main.f() -> u8 mut* {
    let _1 (p): u8 mut* own;
    let _2 (q): u8 mut* own;
    let _3: u8 mut*;
    let _4: u8* mut*;
    bb0: {
        _1 = alloc<u8>(const u64 8) #2:1;
        _2 = move _1 #3:1;
        _3 = cast<u8 mut*>(copy _1) #4:1;
        _4 = const null u8* mut* #5:1;
        _0 = call extern memcpy(copy _3, copy (*_4), const u64 1) #9:1;
        del(move _2) #10:1;
        return #11:1;
    }
}
