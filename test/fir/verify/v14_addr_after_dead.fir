//! pass: verify
//! panic: fir.verify: V14 a return in the scope of a named owner local: main.f bb0 statement 4 (s3)
// V14: an `addr` of `p` after its `dead` opens the scope again. A store through `_2` can give `p`
// an allocation after the `dead`.
fn main.f() -> void {
    let _1 (p): u8 mut* own #2:5;
    let _2: u8 mut* own mut*;

    bb0: {
        _1 = const null u8 mut* own #1:1 s0;
        live(_1) #2:5 s1;
        dead(_1) #3:1 s2;
        _2 = addr mut(_1) #3:5 s2;
        return #4:1 s3;
    }
}
