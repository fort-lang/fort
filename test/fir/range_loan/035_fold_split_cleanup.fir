//! pass: verify
//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[2]) -> i32 {
//|     let _0: i32;
//|     let _2: i32[2];
//|     let _3: i32 mut* own;
//|     let _4: i32 mut* own;
//|     let _5: u64;
//|     range complete 1 1 0 1 1 5 0;
//|     range declaration 1 #1:1;
//|     range expansion 10 declaration 1 context 0 exit 0 #1:1;
//|     range loan 20 expansion 10 ordinal 0 whole_body;
//|     range binding 20 non_owning_array_copy captured _2 #1:1;
//|     range boundary 20 capture bb0 1 order 0 #1:1;
//|     range boundary 20 body_entry bb0 2 order 0 #2:1;
//|     range boundary 20 body_cleanup bb0 2 order 1 #3:1;
//|     range boundary 20 loan_exit bb0 2 order 2 #3:2;
//|     range boundary 20 outer_cleanup bb0 2 order 3 #3:3;
//|
//|     bb0: {
//|         _2 = copy _1;
//|         _3 = const zero i32 mut* own;
//|         _3 = alloc<i32>(const u64 1);
//|         _5 = const u64 0;
//|         _0 = copy _2[_5];
//|         del(move _3);
//|         return;
//|     }
//| }
fn main.f(_1 (a): i32[2]) -> i32 {
    let _2: i32[2];
    let _3: i32 mut* own;
    let _4: i32 mut* own;
    let _5: u64;
    range complete 1 1 0 1 1 5 0;
    range declaration 1 #1:1;
    range expansion 10 declaration 1 context 0 exit 0 #1:1;
    range loan 20 expansion 10 ordinal 0 whole_body;
    range binding 20 non_owning_array_copy captured _2 #1:1;
    range boundary 20 capture bb0 1 order 0 #1:1;
    range boundary 20 body_entry bb0 2 order 0 #2:1;
    range boundary 20 body_cleanup bb0 3 order 0 #3:1;
    range boundary 20 loan_exit bb0 3 order 1 #3:2;
    range boundary 20 outer_cleanup bb1 0 order 2 #3:3;

    bb0: {
        _2 = copy _1;
        _3 = const zero i32 mut* own;
        _4 = alloc<i32>(const u64 1);
        check(overwrite: _3) -> bb1;
    }
    bb1: {
        _3 = move _4;
        _5 = const u64 0;
        _0 = copy _2[_5];
        del(move _3);
        return;
    }
}
