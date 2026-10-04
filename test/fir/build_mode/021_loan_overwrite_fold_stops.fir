//! pass: verify
//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1: i32@, _2: i32 mut* own) -> i32 mut* own {
//|     let _0: i32 mut* own;
//|     let _3: i32 mut* own;
//|     let _4: i32@;
//|
//|     bb0: {
//|         _3 = move _2;
//|         loan_begin 1 _1 _4;
//|         _0 = move _3;
//|         loan_end 1;
//|         return;
//|     }
//| }
fn main.f(_1: i32@, _2: i32 mut* own) -> i32 mut* own {
    let _3: i32 mut* own;
    let _4: i32@;
    bb0: {
        _4 = copy _1;
        _3 = move _2;
        loan_begin 1 _1 _4;
        check(overwrite: _0) -> bb1;
    }
    bb1: { _0 = move _3; loan_end 1; return; }
}
