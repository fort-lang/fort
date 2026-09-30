//! pass: verify
//! prelude:
//| fn view2(u8 mut*[2] a, i32 x) void {
//| }
//| fn bump() i32 {
//|     return 0;
//| }
//! expect:
//| fn main.f(_1 (a): u8 mut* own[2]) -> void {
//|     let _0: void;
//|     let _2: u8 mut*[2];
//|     let _3: i32;
//|     let _4: void;
//|
//|     bb0: {
//|         _2 = copy _1 #2:10 s1;
//|         _3 = call fn main.bump() #2:13 s1;
//|         _4 = call fn main.view2(copy _2, copy _3) #2:5 s1;
//|         return #3:1 s0;
//|     }
//| }
// The materialization rule of spec/fir.md 9.4 on `view2(a, bump())`: `bump` is a writer, so the
// lowering copies `a` into `_2` before the call. `_2` takes the lent type of `a`, its type with
// `own` removed at the top level and at each element of a fixed array (D17.4), so `u8 mut* own[2]`
// gives `u8 mut*[2]`. A `_2` of the type of `a` would own the allocations of `a` a second time
// (V6).
fn main.f(_1 (a): u8 mut* own[2]) -> void {
    let _2: u8 mut*[2];
    let _3: i32;
    let _4: void;

    bb0: {
        _2 = copy _1 #2:10 s1;
        _3 = call fn main.bump() #2:13 s1;
        _4 = call fn main.view2(copy _2, copy _3) #2:5 s1;
        return #3:1 s0;
    }
}
