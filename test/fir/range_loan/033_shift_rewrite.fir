//! pass: verify
//! pass: build-mode --release
//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[2], _2 (v): i32, _3 (count): u8) -> i32 {
//|     let _0: i32;
//|     let _4: i32[2];
//|     let _5: i64;
//|     let _6: u64;
//|     let _7: bool;
//|     let _8: i32;
//|     let _9: i32;
//|     let _10: u64;
//|     let _11: i32;
//|     let _12: i64;
//|     range complete 1 1 0 1 1 5 0;
//|     range declaration 1 #1:1;
//|     range expansion 10 declaration 1 context 0 exit 0 #1:1;
//|     range loan 20 expansion 10 ordinal 0 whole_body;
//|     range binding 20 non_owning_array_copy captured _4 #1:1;
//|     range boundary 20 capture bb0 1 order 0 #1:1;
//|     range boundary 20 body_entry bb0 3 order 0 #2:1;
//|     range boundary 20 body_cleanup bb0 8 order 0 #3:1;
//|     range boundary 20 loan_exit bb0 8 order 1 #3:2;
//|     range boundary 20 outer_cleanup bb0 8 order 2 #3:3;
//|
//|     bb0: {
//|         _4 = copy _1;
//|         _5 = cast<i64>(copy _3);
//|         _12 = and(copy _5, const i64 31);
//|         _8 = cast<i32>(copy _12);
//|         _9 = shl(copy _2, copy _8);
//|         _10 = const u64 0;
//|         _11 = copy _4[_10];
//|         _0 = add(copy _9, copy _11);
//|         return;
//|     }
//| }
fn main.f(_1 (a): i32[2], _2 (v): i32, _3 (count): u8) -> i32 {
    let _4: i32[2];
    let _5: i64;
    let _6: u64;
    let _7: bool;
    let _8: i32;
    let _9: i32;
    let _10: u64;
    let _11: i32;
    range complete 1 1 0 1 1 5 0;
    range declaration 1 #1:1;
    range expansion 10 declaration 1 context 0 exit 0 #1:1;
    range loan 20 expansion 10 ordinal 0 whole_body;
    range binding 20 non_owning_array_copy captured _4 #1:1;
    range boundary 20 capture bb0 1 order 0 #1:1;
    range boundary 20 body_entry bb1 0 order 0 #2:1;
    range boundary 20 body_cleanup bb1 5 order 0 #3:1;
    range boundary 20 loan_exit bb1 5 order 1 #3:2;
    range boundary 20 outer_cleanup bb1 5 order 2 #3:3;

    bb0: {
        _4 = copy _1;
        _5 = cast<i64>(copy _3);
        _6 = cast<u64>(copy _5);
        _7 = ge(copy _6, const u64 32);
        check(copy _7, shift, copy _5, const bytes "i32") -> bb1;
    }
    bb1: {
        _8 = cast<i32>(copy _5);
        _9 = shl(copy _2, copy _8);
        _10 = const u64 0;
        _11 = copy _4[_10];
        _0 = add(copy _9, copy _11);
        return;
    }
}
