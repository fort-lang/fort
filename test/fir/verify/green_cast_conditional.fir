//! pass: verify
//! expect:
//| fn main.f(_1 (c): bool, _2 (a): u8 mut* own* own, _3 (b): u8 mut* own* own) -> void {
//|     let _0: void;
//|     let _4: u8 mut* own* #2:31;
//|     let _5 (p): u8 mut* own* #2:18;
//|
//|     bb0: {
//|         switch(copy _1) -> [true: bb1, otherwise: bb2] #2:27 s1;
//|     }
//|     bb1: {
//|         _4 = copy _2 #2:31 s1;
//|         goto bb3 #2:27 s1;
//|     }
//|     bb2: {
//|         _4 = copy _3 #2:35 s1;
//|         goto bb3 #2:27 s1;
//|     }
//|     bb3: {
//|         live(_5) #2:18 s1;
//|         _5 = cast<u8 mut* own*>(copy _4) #2:22 s1;
//|         dead(_5) #3:1 s0;
//|         return #3:1 s0;
//|     }
//| }
// V6 on `u8 mut* own* p = cast(c ? a : b, u8 mut* own*);` with `a` and `b` of type
// `u8 mut* own* own`. The operands of `?:` lend (D17.4), so the slot `_4` takes the lent type
// `u8 mut* own*` (spec/fir.md 9.4): the `own` behind the pointer stays. The cast carries that
// `own` and adds none (D3.14).
fn main.f(_1 (c): bool, _2 (a): u8 mut* own* own, _3 (b): u8 mut* own* own) -> void {
    let _4: u8 mut* own* #2:31;
    let _5 (p): u8 mut* own* #2:18;

    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:27 s1;
    }
    bb1: {
        _4 = copy _2 #2:31 s1;
        goto bb3 #2:27 s1;
    }
    bb2: {
        _4 = copy _3 #2:35 s1;
        goto bb3 #2:27 s1;
    }
    bb3: {
        live(_5) #2:18 s1;
        _5 = cast<u8 mut* own*>(copy _4) #2:22 s1;
        dead(_5) #3:1 s0;
        return #3:1 s0;
    }
}
