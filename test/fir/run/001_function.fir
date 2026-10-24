//! pass: verify
//! pass: run main.f 40 2
//! pass: run main.f 2147483647 1
//! expect:
//| return i32 42
//| stderr "run/001_function.fir:9:14: runtime error: integer overflow\x0A"
//| abort
// The pass `run` runs `f` with the values of its parameters. The second run overflows: the check
// calls `std.rt.fail_overflow`, which writes the message of D11.4 and calls `abort`.
fn main.f(_1 (a): i32, _2 (b): i32) -> i32 {
    let _3: bool;

    bb0: {
        _3 = add_overflows(copy _1, copy _2) #9:14;
        check(copy _3, overflow) -> bb1 #9:14;
    }
    bb1: {
        _0 = add(copy _1, copy _2) #9:14;
        return #9:5;
    }
}
