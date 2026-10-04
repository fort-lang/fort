//! pass: verify
//! panic: fir.verify: V14 a return with an open loan
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    bb0: { _2 = copy _1; loan_begin 1 _1 _2; return; }
}
