//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//| fn take(u8 mut* own p) void {
//|     del(p);
//| }
//| fn look(u8 mut* p) void {
//| }
//! expect:
//| fn main.f(_1 (q): u8 mut* own, _2 (b): u8 mut@ own, _3 (r): u8 mut*) -> holder {
//|     let _0: holder;
//|     let _4 (p): u8 mut* own #2:5;
//|     let _5: string own;
//|     let _6: u8 mut*;
//|     let _7: void* own;
//|     let _8: void;
//|
//|     bb0: {
//|         _4 = const null u8 mut* own #1:1 s0;
//|         live(_4) #2:5 s1;
//|         _4 = call fn main.grab() #2:5 s1;
//|         _6 = copy _4 #3:1 s2;
//|         _8 = call fn main.look(copy _4) #4:1 s3;
//|         _8 = call fn main.take(move _4) #5:1 s4;
//|         _4 = alloc<u8>(const u64 1) #6:1 s5;
//|         _0 = aggregate holder(move _4, const u64 1) #7:1 s6;
//|         _4 = cast<u8 mut* own>(copy _3) #8:1 s7;
//|         _5 = cast<string own>(move _2) #9:1 s8;
//|         _7 = const null void* own #10:1 s9;
//|         del(move _4) #11:1 s10;
//|         del(move _5) #12:1 s11;
//|         del(move _7) #13:1 s12;
//|         del(move _1) #14:1 s13;
//|         dead(_4) #15:1 s14;
//|         return #15:1 s14;
//|     }
//| }
// V5 and V6 on the edges: each form that gives an owning place its value, a lend of an owning
// place to a place and to a parameter that do not own, an adopting cast, and a del of each kind
// of own reference.
fn main.f(_1 (q): u8 mut* own, _2 (b): u8 mut@ own, _3 (r): u8 mut*) -> holder {
    let _4 (p): u8 mut* own #2:5;
    let _5: string own;
    let _6: u8 mut*;
    let _7: void* own;
    let _8: void;

    bb0: {
        _4 = const null u8 mut* own #1:1 s0;
        live(_4) #2:5 s1;
        _4 = call fn main.grab() #2:5 s1;
        _6 = copy _4 #3:1 s2;
        _8 = call fn main.look(copy _4) #4:1 s3;
        _8 = call fn main.take(move _4) #5:1 s4;
        _4 = alloc<u8>(const u64 1) #6:1 s5;
        _0 = aggregate holder(move _4, const u64 1) #7:1 s6;
        _4 = cast<u8 mut* own>(copy _3) #8:1 s7;
        _5 = cast<string own>(move _2) #9:1 s8;
        _7 = const null void* own #10:1 s9;
        del(move _4) #11:1 s10;
        del(move _5) #12:1 s11;
        del(move _7) #13:1 s12;
        del(move _1) #14:1 s13;
        dead(_4) #15:1 s14;
        return #15:1 s14;
    }
}
