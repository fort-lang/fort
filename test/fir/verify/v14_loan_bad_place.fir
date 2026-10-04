//! pass: verify
//! panic: fir.verify: V14 a loan of a place that is not a collection
fn main.f(_1: i32) -> void {
    bb0: { loan_begin 1 _1; loan_end 1; return; }
}
