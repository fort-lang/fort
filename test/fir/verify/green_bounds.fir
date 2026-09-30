//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[4], _2 (i): i32) -> i32 {
//|     let _0: i32;
//|     let _3: u64 #2:14;
//|     let _4: bool #2:14;
//|     let _5: i64 #2:14;
//|
//|     bb0: {
//|         _3 = cast<u64>(copy _2) #2:14 s1;
//|         _4 = ge(copy _3, const u64 4) #2:14 s1;
//|         _5 = cast<i64>(copy _3) #2:14 s1;
//|         check(copy _4, bounds, copy _5, const u64 4) -> bb1 #2:14 s1;
//|     }
//|     bb1: {
//|         _0 = copy _1[_3] #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// V11 and V13 on `return a[i];` of spec/fir.md 9.4: the index `_3` and the reported index `_5`
// are assigned before the check reads them, and the continuation bb1 reads the element through
// the index local `_3`.
fn main.f(_1 (a): i32[4], _2 (i): i32) -> i32 {
    let _3: u64 #2:14;
    let _4: bool #2:14;
    let _5: i64 #2:14;

    bb0: {
        _3 = cast<u64>(copy _2) #2:14 s1;
        _4 = ge(copy _3, const u64 4) #2:14 s1;
        _5 = cast<i64>(copy _3) #2:14 s1;
        check(copy _4, bounds, copy _5, const u64 4) -> bb1 #2:14 s1;
    }
    bb1: {
        _0 = copy _1[_3] #2:12 s1;
        return #2:5 s1;
    }
}
