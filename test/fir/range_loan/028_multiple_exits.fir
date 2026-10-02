//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[2], _2 (left): bool) -> i32 {
//|     let _0: i32;
//|     let _3: i32[2];
//|     range complete 1 1 0 1 1 8 0;
//|     range declaration 1 #1:1;
//|     range expansion 10 declaration 1 context 0 exit 0 #1:1;
//|     range loan 20 expansion 10 ordinal 0 whole_body;
//|     range binding 20 non_owning_array_copy captured _3 #1:1;
//|     range boundary 20 capture bb0 1 order 0 #1:1;
//|     range boundary 20 body_entry bb0 1 order 1 #2:1;
//|     range boundary 20 body_cleanup bb1 1 order 0 #3:1;
//|     range boundary 20 loan_exit bb1 1 order 1 #3:2;
//|     range boundary 20 outer_cleanup bb1 1 order 2 #3:3;
//|     range boundary 20 body_cleanup bb2 1 order 0 #4:1;
//|     range boundary 20 loan_exit bb2 1 order 1 #4:2;
//|     range boundary 20 outer_cleanup bb2 1 order 2 #4:3;
//|
//|     bb0: {
//|         _3 = copy _1;
//|         switch(copy _2) -> [true: bb1, otherwise: bb2];
//|     }
//|     bb1: {
//|         _0 = const i32 1;
//|         return;
//|     }
//|     bb2: {
//|         _0 = const i32 2;
//|         return;
//|     }
//| }
fn main.f(_1 (a): i32[2], _2 (left): bool) -> i32 {
    let _3: i32[2];
    range complete 1 1 0 1 1 8 0;
    range declaration 1 #1:1;
    range expansion 10 declaration 1 context 0 exit 0 #1:1;
    range loan 20 expansion 10 ordinal 0 whole_body;
    range binding 20 non_owning_array_copy captured _3 #1:1;
    range boundary 20 capture bb0 1 order 0 #1:1;
    range boundary 20 body_entry bb0 1 order 1 #2:1;
    range boundary 20 body_cleanup bb1 1 order 0 #3:1;
    range boundary 20 loan_exit bb1 1 order 1 #3:2;
    range boundary 20 outer_cleanup bb1 1 order 2 #3:3;
    range boundary 20 body_cleanup bb2 1 order 0 #4:1;
    range boundary 20 loan_exit bb2 1 order 1 #4:2;
    range boundary 20 outer_cleanup bb2 1 order 2 #4:3;

    bb0: {
        _3 = copy _1;
        switch(copy _2) -> [true: bb1, otherwise: bb2];
    }
    bb1: {
        _0 = const i32 1;
        return;
    }
    bb2: {
        _0 = const i32 2;
        return;
    }
}
