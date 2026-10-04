//! pass: verify
//! panic: fir.verify: V14 a loan statement out of order
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    bb0: { _2 = copy _1; loan_begin 1 _1; loan_begin 2 _1 _2; loan_end 1; return; }
}
