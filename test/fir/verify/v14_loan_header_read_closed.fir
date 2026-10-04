//! pass: verify
//! panic: fir.verify: V14 a read of a loan header while the loan is closed
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    let _3: u64;
    bb0: { _2 = copy _1; loan_begin 1 _1 _2; loan_end 1; _3 = copy _2.1; return; }
}
