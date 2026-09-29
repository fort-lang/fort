//! pass: verify
//! panic: fir.verify: V8 a noreturn call is not the last statement: main.f bb0 statement 0 (s1)
//! prelude:
//| fn stop() noreturn {
//|     while (true) {
//|     }
//| }
// V8: a call of a noreturn function is the last statement of its block, and the block ends in
// trap (D8.5).
fn main.f(_1 (x): i32) -> void {
    let _2 (y): i32 #3:5;
    let _3: void;

    bb0: {
        _3 = call fn main.stop() #2:5 s1;
        live(_2) #3:5 s2;
        trap #4:1 s0;
    }
}
