//! pass: verify
//! pass: build-mode
//! pass: verify
//! expect:
//| fn main.f(_1: i32@, _2: i32) -> i32 {
//|     let _0: i32;
//|     let _3: i32@;
//|     let _4: i32;
//|     let _5: i32;
//|
//|     bb0: {
//|         loan_begin 1 _1 _3;
//|         loan_end 1;
//|         _0 = copy _2;
//|         return;
//|     }
//| }
fn main.f(_1: i32@, _2: i32) -> i32 {
    let _3: i32@;
    let _4: i32;
    let _5: i32;
    bb0: {
        _4 = copy _2;
        _3 = copy _1;
        loan_begin 1 _1 _3;
        _5 = copy _2;
        loan_end 1;
        _0 = copy _2;
        return;
    }
}
