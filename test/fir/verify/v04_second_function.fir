//! pass: verify
//! panic: fir.verify: V4 the two sides of an assignment do not agree: main.g bb0 statement 0 (s1)
// The pass verifies each function of the module in text order. The first function keeps every
// rule, and the second breaks V4.
fn main.f() -> void {
    bb0: {
        return;
    }
}

fn main.g(_1 (b): bool) -> i32 {
    bb0: {
        _0 = copy _1 #2:12 s1;
        return #2:5 s1;
    }
}
