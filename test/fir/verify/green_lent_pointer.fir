//! pass: verify
//! prelude:
//| fn view(u8 mut* own* p, i32 x) void {
//| }
//| fn bump() i32 {
//|     return 0;
//| }
//! expect:
//| fn main.f(_1 (a): u8 mut* own* own) -> void {
//|     let _0: void;
//|     let _2: u8 mut* own*;
//|     let _3: i32;
//|     let _4: void;
//|
//|     bb0: {
//|         _2 = copy _1 #2:10 s1;
//|         _3 = call fn main.bump() #2:13 s1;
//|         _4 = call fn main.view(copy _2, copy _3) #2:5 s1;
//|         return #3:1 s0;
//|     }
//| }
// The materialization rule of spec/fir.md 9.4 on `view(a, bump())` with `a: u8 mut* own* own`.
// `_2` takes the lent type of `a`: the copy drops the `own` of the top level, and the `own` behind
// the pointer stays, so `u8 mut* own* own` gives `u8 mut* own*` (D17.4), the type of the
// parameter.
fn main.f(_1 (a): u8 mut* own* own) -> void {
    let _2: u8 mut* own*;
    let _3: i32;
    let _4: void;

    bb0: {
        _2 = copy _1 #2:10 s1;
        _3 = call fn main.bump() #2:13 s1;
        _4 = call fn main.view(copy _2, copy _3) #2:5 s1;
        return #3:1 s0;
    }
}
