//! pass: verify
//! expect:
//| fn main.f(_1: i32@) -> void {
//|     let _0: void;
//|     let _2: i32@;
//|     let _3: u64;
//|
//|     bb0: {
//|         _2 = copy _1;
//|         switch(const true) -> [true: bb1, otherwise: bb2];
//|     }
//|     bb1: {
//|         loan_begin 1 _1 _2;
//|         _3 = copy _2.1;
//|         loan_end 1;
//|         return;
//|     }
//|     bb2: {
//|         loan_end 99;
//|         _3 = copy _2.1;
//|         return;
//|     }
//| }
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    let _3: u64;
    bb0: { _2 = copy _1; switch(const true) -> [true: bb1, otherwise: bb2]; }
    bb1: { loan_begin 1 _1 _2; _3 = copy _2.1; loan_end 1; return; }
    bb2: { loan_end 99; _3 = copy _2.1; return; }
}
