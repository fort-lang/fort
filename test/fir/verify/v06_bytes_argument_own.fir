//! pass: verify
//! panic: fir.verify: V6 an owning parameter takes a copy or a view: main.f bb0 statement 0 (s1)
//! prelude:
//| fn take_text(char* own p) void {
//|     del(p);
//| }
// V6: an argument for an `own` parameter is a move, `null T` or `zero T`. The callee would free a
// `bytes` constant, which nothing allocated.
fn main.f() -> void {
    let _1: void;

    bb0: {
        _1 = call fn main.take_text(const bytes "x") #2:5 s1;
        return #3:1 s0;
    }
}
