//! pass: none
//! error: none/006_void_function_type_result.fir:5:26: error: 'void' is only a return type
// A function type as the result of a signature takes no `void` parameter either (D3.1, D3.10). The
// parser reports the parameter type, and the compiler writes no FIR.
fn main.g(_1: i32) -> fn(void) void {
    let _0: fn(i32) void;

    bb0: {
        return;
    }
}
