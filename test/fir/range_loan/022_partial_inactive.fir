//! pass: verify
//! expect:
//| fn main.f() -> i32 {
//|     let _0: i32;
//|     range declaration 1 #1:1;
//|
//|     bb0: {
//|         _0 = const i32 1;
//|         return;
//|     }
//| }
fn main.f() -> i32 {
    range declaration 1 #1:1;
    bb0: {
        _0 = const i32 1;
        return;
    }
}
