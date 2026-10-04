//! pass: verify
//! expect:
//| fn main.f(_1: i32@, _2: string) -> void {
//|     let _0: void;
//|     let _3: i32@;
//|     let _4: string;
//|
//|     bb0: {
//|         _3 = copy _1;
//|         loan_begin 1 _1 _3;
//|         _4 = copy _2;
//|         loan_begin 2 _2 _4;
//|         loan_end 2;
//|         loan_end 1;
//|         return;
//|     }
//| }
fn main.f(_1: i32@, _2: string) -> void {
    let _3: i32@; let _4: string;
    bb0: { _3 = copy _1; loan_begin 1 _1 _3;
        _4 = copy _2; loan_begin 2 _2 _4; loan_end 2; loan_end 1; return; }
}
