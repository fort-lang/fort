//! pass: run main.f
//! expect:
//| fault null dereference at 8:9
// A load through null ends the run as a fault at the statement.
fn main.f() -> i32 {
    let _1: i32*;

    bb0: {
        _1 = const null i32*;
        _0 = copy (*_1) #8:9;
        return;
    }
}
