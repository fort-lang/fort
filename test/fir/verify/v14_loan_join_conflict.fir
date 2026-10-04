//! pass: verify
//! panic: fir.verify: V14 incoming loan states conflict
fn main.f(_1: i32@, _2: bool) -> void {
    bb0: { switch(copy _2) -> [true: bb1, otherwise: bb2]; }
    bb1: { loan_begin 1 _1; goto bb3; }
    bb2: { goto bb3; }
    bb3: { trap; }
}
