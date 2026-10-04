//! pass: verify
//! expect:
//| fn main.f(_1: i32@) -> void {
//|     let _0: void;
//|     let _2: i32@;
//|
//|     bb0: {
//|         _2 = copy _1;
//|         loan_begin 1 _1 _2;
//|         goto bb1;
//|     }
//|     bb1: {
//|         trap;
//|     }
//| }
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    bb0: { _2 = copy _1; loan_begin 1 _1 _2; goto bb1; }
    bb1: { trap; }
}
