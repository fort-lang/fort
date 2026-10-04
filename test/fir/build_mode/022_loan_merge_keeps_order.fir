//! pass: verify
//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1: i32@, _2: i32) -> i32 {
//|     let _0: i32;
//|     let _3: i32@;
//|     let _4: bool;
//|
//|     bb0: {
//|         loan_begin 1 _1 _3;
//|         _0 = copy _2;
//|         loan_end 1;
//|         return;
//|     }
//| }
fn main.f(_1: i32@, _2: i32) -> i32 {
    let _3: i32@;
    let _4: bool;
    bb0: {
        _3 = copy _1;
        loan_begin 1 _1 _3;
        _4 = add_overflows(copy _2, const i32 1);
        check(copy _4, overflow) -> bb1;
    }
    bb1: { _0 = copy _2; loan_end 1; return; }
}
