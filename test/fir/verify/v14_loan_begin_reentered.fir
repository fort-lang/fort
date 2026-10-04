//! pass: verify
//! panic: fir.verify: V14 incoming loan states conflict
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    bb0: { _2 = copy _1; goto bb1; }
    bb1: { loan_begin 1 _1 _2; goto bb1; }
}
