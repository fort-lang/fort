//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32 mut* own[2]) -> i32 {
//|     let _0: i32;
//|     range complete 1 1 1 1 1 5 0;
//|     range declaration 1 #1:1;
//|     range expansion 10 declaration 1 context 0 exit 0 #1:1;
//|     range loan 20 expansion 10 ordinal 0 whole_body;
//|     range binding 20 owning_fixed_array_place captured _1 original _1 #1:1;
//|     range boundary 20 capture bb0 0 order 0 #1:1;
//|     range boundary 20 body_entry bb0 0 order 1 #2:1;
//|     range boundary 20 body_cleanup bb0 1 order 0 #3:1;
//|     range boundary 20 loan_exit bb0 1 order 1 #3:2;
//|     range boundary 20 outer_cleanup bb0 1 order 2 #3:3;
//|
//|     bb0: {
//|         _0 = const i32 1;
//|         return;
//|     }
//| }
fn main.f(_1 (a): i32 mut* own[2]) -> i32 {
    range complete 1 1 1 1 1 5 0;
    range declaration 1 #1:1;
    range expansion 10 declaration 1 context 0 exit 0 #1:1;
    range loan 20 expansion 10 ordinal 0 whole_body;
    range binding 20 owning_fixed_array_place captured _1 original _1 #1:1;
    range boundary 20 capture bb0 0 order 0 #1:1;
    range boundary 20 body_entry bb0 0 order 1 #2:1;
    range boundary 20 body_cleanup bb0 1 order 0 #3:1;
    range boundary 20 loan_exit bb0 1 order 1 #3:2;
    range boundary 20 outer_cleanup bb0 1 order 2 #3:3;

    bb0: {
        _0 = const i32 1;
        return;
    }
}
