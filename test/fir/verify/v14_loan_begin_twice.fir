//! pass: verify
//! panic: fir.verify: V14 a loan has two begin statements
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    bb0: { loan_begin 1 _1; loan_end 1; loan_begin 1 _1; loan_end 1; return; }
}
