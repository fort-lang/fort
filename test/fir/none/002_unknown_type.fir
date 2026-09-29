//! pass: none
//! error: none/002_unknown_type.fir:5:15: error: unknown type 'nosuch'
// A signature that names a type the prelude does not declare is one diagnostic at the
// type, and the compiler writes no FIR.
fn main.g(_1: nosuch) -> i32 {
    let _0: i32;

    bb0: {
        return;
    }
}
