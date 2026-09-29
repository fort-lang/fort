//! pass: verify
//! panic: fir.verify: V2 a terminator names the entry block: main.f bb1 statement 0 (s2)
// V2: no terminator names bb0, the entry block. A loop goes back to the block after bb0.
fn main.f() -> void {
    bb0: {
        goto bb1 #1:1 s1;
    }
    bb1: {
        goto bb0 #2:1 s2;
    }
}
