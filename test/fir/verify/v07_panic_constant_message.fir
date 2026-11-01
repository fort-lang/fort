//! pass: verify
//! panic: fir.verify: V7 an aggregate argument that is not a place: main.pa bb0 statement 0 (s1)
// V7: the message of `fail(panic, ...)` is a `string`, an aggregate, and the entry takes its
// address. A constant has no address, so the message is a place, as the lowering of `panic("stop")`
// writes it in build_mode/016_panic_stays.fir: a `string` temporary, then the `fail`.
fn main.pa() -> void {
    let _0: void;

    bb0: {
        fail(panic, const "stop") #27:5 s1;
    }
}
