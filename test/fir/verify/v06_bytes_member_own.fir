//! pass: verify
//! panic: fir.verify: V6 an owning member takes a copy or a view: main.f bb0 statement 0 (s1)
//! prelude:
//| struct text_box {
//|     char* own p;
//| }
// V6: a member for an owning field is a move, `null T` or `zero T`, and a `bytes` constant is
// neither.
fn main.f() -> text_box {
    bb0: {
        _0 = aggregate text_box(const bytes "x") #2:12 s1;
        return #2:5 s1;
    }
}
