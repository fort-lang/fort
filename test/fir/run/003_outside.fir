//! pass: run main.f
//! prelude:
//| extern fn getpid() i32;
//! expect:
//| outside getpid
// A call of an extern that the interpreter does not run ends the run as `outside`.
fn main.f() -> i32 {
    bb0: {
        _0 = call extern getpid();
        return;
    }
}
