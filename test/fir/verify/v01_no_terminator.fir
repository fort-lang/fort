//! pass: verify
//! error: verify/v01_no_terminator.fir:12:5: error: block 'bb1' has no terminator
// V1: every block ends in a terminator. The grammar of spec/fir.md 13 gives every block a
// terminator, so the parser stops this text before the verifier runs, and the textual form
// cannot hold a V1 violation. test/fort/fir_verify_v01_panic_test.ft builds the block with the
// constructors and holds the panic.
fn main.f() -> void {
    bb0: {
        goto bb1;
    }
    bb1: {
    }
}
