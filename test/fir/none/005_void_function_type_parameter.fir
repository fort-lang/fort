//! pass: none
//! error: none/005_void_function_type_parameter.fir:5:18: error: 'void' is only a return type
// A function type in a signature does not take a `void` parameter (D3.1, D3.10). The parser
// reports the parameter type, and the compiler writes no FIR.
fn main.g(_1: fn(void) void) -> i32 {
    let _0: i32;

    bb0: {
        return;
    }
}
