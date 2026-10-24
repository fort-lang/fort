//! pass: run main.f
//! prelude:
//| struct pair {
//|     i32 x;
//|     i32 y;
//| }
//! expect:
//| return pair, an aggregate of 8 bytes
// The report names the type and the size of a result of aggregate type, and gives no bytes.
fn main.f() -> pair {
    bb0: {
        _0 = aggregate pair(const i32 3, const i32 4);
        return;
    }
}
