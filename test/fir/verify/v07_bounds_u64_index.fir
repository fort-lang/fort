//! pass: verify
//! panic: fir.verify: V7 a reported operand that does not agree: main.f bb0 statement 2 (s1)
// V7: the reported operands of a check agree with the parameters of the runtime entry of its
// kind. std.rt.fail_bounds takes the index as an i64, so that a negative index prints as one
// (spec/fir.md 8). The lowering casts the index to i64 before it reports it (9.4).
fn main.f(_1 (a): i32[4], _2 (i): u64) -> i32 {
    let _3: u64 #2:12;
    let _4: bool #2:12;

    bb0: {
        _3 = const u64 4 #2:12 s1;
        _4 = ge(copy _2, copy _3) #2:12 s1;
        check(copy _4, bounds, copy _2, copy _3) -> bb1 #2:12 s1;
    }
    bb1: {
        _0 = copy _1[_2] #2:12 s1;
        return #2:5 s1;
    }
}
