//! pass: verify
//! panic: fir.verify: V3 an index of a type with no elements: main.f bb0 statement 0 (s1)
// V3: an index applies to a fixed array, a span or a string, and never to a pointer (D6.8).
fn main.f(_1 (p): i32*, _2 (i): u64) -> i32 {
    bb0: {
        _0 = copy _1[_2] #2:12 s1;
        return #2:5 s1;
    }
}
