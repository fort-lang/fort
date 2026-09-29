//! pass: verify
//! error: verify/v02_target_out_of_range.fir:8:14: error: unknown block 'bb2'
// V2: every block that a terminator names is a block of the function. The parser refuses a name
// of a block that the function does not have, so the verifier never sees this text.
// test/fort/fir_verify_v02_panic_test.ft builds the terminator with the constructors.
fn main.f() -> void {
    bb0: {
        goto bb2;
    }
    bb1: {
        return;
    }
}
