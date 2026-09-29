//! pass: verify
//! panic: fir.verify: V7 a call with the wrong count of arguments: main.f bb0 statement 0 (s1)
//! prelude:
//| extern fn printf(char* format, ...) i32;
// V7: a call of a variadic extern has at least the count of its fixed parameters.
fn main.f() -> i32 {
    bb0: {
        _0 = call extern printf() #2:12 s1;
        return #2:5 s1;
    }
}
