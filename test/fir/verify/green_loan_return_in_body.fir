//! pass: verify
//! expect:
//| fn main.f(_1: i32@, _2: bool) -> void {
//|     let _0: void;
//|     let _3: i32@;
//|     let _4 (x): i32;
//|
//|     bb0: {
//|         _3 = copy _1;
//|         loan_begin 1 _1 _3;
//|         switch(copy _2) -> [true: bb1, otherwise: bb2];
//|     }
//|     bb1: {
//|         live(_4);
//|         _4 = const i32 0;
//|         dead(_4);
//|         loan_end 1;
//|         return;
//|     }
//|     bb2: {
//|         loan_end 1;
//|         return;
//|     }
//| }
fn main.f(_1: i32@, _2: bool) -> void {
    let _3: i32@; let _4 (x): i32;
    bb0: { _3 = copy _1; loan_begin 1 _1 _3;
        switch(copy _2) -> [true: bb1, otherwise: bb2]; }
    bb1: { live(_4); _4 = const i32 0; dead(_4); loan_end 1; return; }
    bb2: { loan_end 1; return; }
}
