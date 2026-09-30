//! pass: build-mode --no-bounds-check
//! pass: verify
//! expect:
//| fn main.g(_1 (s): u8@, _2 (i): u64) -> u8@ {
//|     let _0: u8@;
//|     let _3: u64 #3:13;
//|     let _4: u64 #3:13;
//|     let _5: bool #3:13;
//|     let _6: bool #3:13;
//|     let _7: bool #3:13;
//|     let _8: i64 #3:13;
//|     let _9: i64 #3:13;
//|
//|     bb0: {
//|         _3 = cast<u64>(const i32 1) #3:13 s1;
//|         _4 = cast<u64>(copy _2) #3:13 s1;
//|         _0 = slice(_1, copy _3, copy _4) #3:13 s1;
//|         return #3:5 s1;
//|     }
//| }
// The lowering of `return s[1..i];`. --no-bounds-check turns the span check into `goto bb1`. The
// `or` and the two reported `cast`s go in the first round and the two `gt`s in the second. The
// bounds `_3` and `_4` stay for the `slice`, and bb1 merges into bb0.
fn main.g(_1 (s): u8@, _2 (i): u64) -> u8@ {
    let _3: u64 #3:13;
    let _4: u64 #3:13;
    let _5: bool #3:13;
    let _6: bool #3:13;
    let _7: bool #3:13;
    let _8: i64 #3:13;
    let _9: i64 #3:13;

    bb0: {
        _3 = cast<u64>(const i32 1) #3:13 s1;
        _4 = cast<u64>(copy _2) #3:13 s1;
        _5 = gt(copy _3, copy _4) #3:13 s1;
        _6 = gt(copy _4, copy _1.1) #3:13 s1;
        _7 = or(copy _5, copy _6) #3:13 s1;
        _8 = cast<i64>(copy _3) #3:13 s1;
        _9 = cast<i64>(copy _4) #3:13 s1;
        check(copy _7, span, copy _8, copy _9, copy _1.1) -> bb1 #3:13 s1;
    }
    bb1: {
        _0 = slice(_1, copy _3, copy _4) #3:13 s1;
        return #3:5 s1;
    }
}
