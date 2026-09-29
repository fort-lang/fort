//! pass: verify
//! panic: fir.verify: V4 the two operands of an operation do not agree: main.f bb0 statement 0 (s1)
// V4: the two operands of an arithmetic rvalue have one type (spec/fir.md 6). There is no
// promotion and no mixed signedness (D6.2).
fn main.f(_1 (a): i32, _2 (b): u32) -> i32 {
    bb0: {
        _0 = add(copy _1, copy _2) #2:14 s1;
        return #2:5 s1;
    }
}
