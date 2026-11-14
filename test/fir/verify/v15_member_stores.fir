//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     i32 b;
//| }
//! panic: fir.verify: V15 a second store into an aggregate result: main.f bb0 statement 1 (s2)
// V15: a store into a member of `_0` is a store into fixed storage of `_0`, so the second member
// store is a second store. The lowering stores the whole value once.
fn main.f() -> pair {
    bb0: {
        _0.0 = const i32 1 #2:5 s1;
        _0.1 = const i32 2 #3:5 s2;
        return #4:5 s3;
    }
}
