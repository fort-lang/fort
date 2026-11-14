//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! expect:
//| fn main.f() -> void {
//|     let _0: void;
//|     let _1: u8 mut@ own;
//|     let _2: u8 mut* own;
//|
//|     bb0: {
//|         _2 = call fn main.grab();
//|         _1 = aggregate u8 mut@ own(move _2, const u64 1);
//|         _1.1 = const u64 1;
//|         del(move _1);
//|         return;
//|     }
//| }
// V16: a store into a part of a live scratch local writes no whole local, so it goes over no
// allocation: the store into the length of the span `_1` keeps the rule.
fn main.f() -> void {
    let _1: u8 mut@ own;
    let _2: u8 mut* own;

    bb0: {
        _2 = call fn main.grab();
        _1 = aggregate u8 mut@ own(move _2, const u64 1);
        _1.1 = const u64 1;
        del(move _1);
        return;
    }
}
