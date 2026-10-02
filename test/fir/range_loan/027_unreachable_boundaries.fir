//! pass: verify
//! panic: fir.verify: V14 a complete range contract lacks a required record
fn main.f(_1 (a): i32@ own) -> u64 {
    let _2: i32@;
    range complete 1 1 1 1 1 5 0;
    range declaration 1 #1:1;
    range expansion 10 declaration 1 context 0 exit 0 #1:1;
    range loan 20 expansion 10 ordinal 0 whole_body;
    range binding 20 borrowed_header captured _2 original _1 #1:1;
    range boundary 20 capture bb1 1 order 0 #1:1;
    range boundary 20 body_entry bb1 1 order 1 #2:1;
    range boundary 20 body_cleanup bb1 2 order 0 #3:1;
    range boundary 20 loan_exit bb1 2 order 1 #3:2;
    range boundary 20 outer_cleanup bb1 2 order 2 #3:3;

    bb0: {
        _0 = const u64 0;
        return;
    }
    bb1: {
        _2 = copy _1;
        _0 = copy _2.1;
        return;
    }
}
