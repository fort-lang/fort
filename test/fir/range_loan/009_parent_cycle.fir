//! pass: verify
//! panic: fir.verify: V14 a range loan has an invalid parent relation
fn main.f(_1 (a): i32[2]) -> i32 {
    let _2: i32[2] #2:1;
    let _3: i32[2] #3:1;
    range complete 3 3 0 3 3 15 1;
    range declaration 1 #2:1;
    range declaration 2 #3:1;
    range declaration 3 #4:1;
    range expansion 10 declaration 1 context 0 exit 0 #2:1;
    range expansion 11 declaration 1 context 2 exit 1 #2:1 exit 8:3;
    range expansion 12 declaration 2 context 0 exit 0 #3:1;
    range loan 20 expansion 10 ordinal 0 parent 21 whole_body;
    range loan 21 expansion 12 ordinal 0 parent 20 whole_body;
    range loan 22 expansion 11 ordinal 0 whole_body;
    range binding 20 non_owning_array_copy captured _2 #2:1;
    range binding 21 non_owning_array_copy captured _3 #3:1;
    range binding 22 non_owning_array_copy captured _2 #8:3;
    range boundary 20 capture bb0 1 order 0 #2:1;
    range boundary 20 body_entry bb0 1 order 1 #2:1;
    range boundary 21 capture bb0 2 order 0 #3:1;
    range boundary 21 body_entry bb0 2 order 1 #3:1;
    range boundary 21 body_cleanup bb0 3 order 0 #3:5;
    range boundary 21 loan_exit bb0 3 order 1 #3:6;
    range boundary 21 outer_cleanup bb0 3 order 2 #3:7;
    range boundary 20 body_cleanup bb0 3 order 3 #4:1;
    range boundary 20 loan_exit bb0 3 order 4 #4:2;
    range boundary 20 outer_cleanup bb0 3 order 5 #4:3;
    range boundary 22 capture bb0 3 order 6 #8:3;
    range boundary 22 body_entry bb0 3 order 7 #8:3;
    range boundary 22 body_cleanup bb0 3 order 8 #8:4;
    range boundary 22 loan_exit bb0 3 order 9 #8:5;
    range boundary 22 outer_cleanup bb0 3 order 10 #8:6;
    range absence 3 no_lowered_operation #4:1;

    bb0: {
        _2 = copy _1 #2:1 s1;
        _3 = copy _2 #3:1 s2;
        _0 = const i32 1 #4:1 s3;
        return #5:1 s4;
    }
}
