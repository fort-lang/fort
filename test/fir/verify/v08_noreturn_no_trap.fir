//! pass: verify
//! panic: fir.verify: V8 a noreturn call with no trap after it: main.f bb0 statement 0 (s1)
//! prelude:
//| fn stop() noreturn {
//|     while (true) {
//|     }
//| }
// V8: the block of a call of a noreturn function ends in trap, and in no other terminator.
fn main.f() -> void {
    let _1: void;

    bb0: {
        _1 = call fn main.stop() #2:5 s1;
        return #3:1 s0;
    }
}
