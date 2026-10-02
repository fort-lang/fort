//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[2]) -> i32 {
//|     let _0: i32;
//|     let _2: i32[2];
//|     range complete 1 1 0 1 1 2 0;
//|     range declaration 1 #1:1;
//|     range expansion 10 declaration 1 context 0 exit 0 #1:1;
//|     range loan 20 expansion 10 ordinal 0 whole_body;
//|     range binding 20 non_owning_array_copy captured _2 #1:1;
//|     range boundary 20 capture bb0 1 order 0 #1:1;
//|     range boundary 20 body_entry bb0 1 order 1 #2:1;
//|
//|     bb0: {
//|         _2 = copy _1;
//|         trap;
//|     }
//| }
fn main.f(_1 (a): i32[2]) -> i32 {
    let _2: i32[2];
    range complete 1 1 0 1 1 2 0;
    range declaration 1 #1:1;
    range expansion 10 declaration 1 context 0 exit 0 #1:1;
    range loan 20 expansion 10 ordinal 0 whole_body;
    range binding 20 non_owning_array_copy captured _2 #1:1;
    range boundary 20 capture bb0 1 order 0 #1:1;
    range boundary 20 body_entry bb0 1 order 1 #2:1;

    bb0: {
        _2 = copy _1;
        trap;
    }
}
