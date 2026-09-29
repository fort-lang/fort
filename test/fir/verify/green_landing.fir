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
//| fn keep(u8 mut* own p) u8 mut* own {
//|     return p;
//| }
//! expect:
//| fn main.keep(_1 (p): u8 mut* own) -> u8 mut* own {
//|     let _0: u8 mut* own;
//|     let _2: u8 mut* own;
//|     let _3: void;
//|     let _4: holder;
//|     let _5: u8 mut@ own;
//|     let _6: string own;
//|     let _7: string;
//|
//|     bb0: {
//|         _2 = call fn main.grab() #2:5 s1;
//|         del(move _2) #3:5 s2;
//|         _2 = alloc<u8>(const u64 1) #4:5 s3;
//|         _3 = call fn main.take(move _2) #5:5 s4;
//|         _2 = call fn main.grab() #6:5 s5;
//|         _4 = aggregate holder(move _2, const u64 0) #7:5 s6;
//|         _5 = const zero u8 mut@ own #8:5 s7;
//|         _6 = cast<string own>(move _5) #9:5 s8;
//|         del(move _6) #10:5 s9;
//|         _7 = const "abc" #11:5 s10;
//|         _2 = const null u8 mut* own #12:5 s11;
//|         _0 = move _1 #13:5 s12;
//|         return #13:5 s12;
//|     }
//| }
// The positions where an owning value lands (D17.8): a `del`, an owning place and the return
// place, an `own` parameter, an owning member, and the operand of a cast to an owning type. A
// `string` constant fills a place that does not own, and `null` and `zero` fill an owning one.
fn main.keep(_1 (p): u8 mut* own) -> u8 mut* own {
    let _2: u8 mut* own;
    let _3: void;
    let _4: holder;
    let _5: u8 mut@ own;
    let _6: string own;
    let _7: string;

    bb0: {
        _2 = call fn main.grab() #2:5 s1;
        del(move _2) #3:5 s2;
        _2 = alloc<u8>(const u64 1) #4:5 s3;
        _3 = call fn main.take(move _2) #5:5 s4;
        _2 = call fn main.grab() #6:5 s5;
        _4 = aggregate holder(move _2, const u64 0) #7:5 s6;
        _5 = const zero u8 mut@ own #8:5 s7;
        _6 = cast<string own>(move _5) #9:5 s8;
        del(move _6) #10:5 s9;
        _7 = const "abc" #11:5 s10;
        _2 = const null u8 mut* own #12:5 s11;
        _0 = move _1 #13:5 s12;
        return #13:5 s12;
    }
}
