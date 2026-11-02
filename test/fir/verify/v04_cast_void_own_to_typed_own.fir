//! pass: verify
//! panic: fir.verify: V4 a cast that D3.14 refuses: main.f bb0 statement 0 (s1)
// V4: a `void mut* own` takes no type either, with its marks kept (D3.14, spec/fir.md 4).
fn main.f(_1 (v): void mut* own) -> i64 mut* own {
    bb0: {
        _0 = cast<i64 mut* own>(move _1) #2:12 s1;
        return #2:5 s1;
    }
}
