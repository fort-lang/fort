//! pass: verify
//! expect:
//| fn main.f() -> void {
//|     let _0: void;
//|
//|     bb0: {
//|         return;
//|     }
//| }
// The smallest function: one block that returns. The verifier writes nothing about it, and the
// pass writes the module.
fn main.f() -> void {
    bb0: {
        return;
    }
}
