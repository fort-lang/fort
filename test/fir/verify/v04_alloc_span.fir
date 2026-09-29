//! pass: verify
//! panic: fir.verify: V4 an alloc of a span: main.f bb0 statement 0 (s1)
// V4: `alloc` takes no span type, as `new(T@)` is an error (D10.2). `new(T, n)` allocates the
// elements and builds the span with `aggregate` (spec/fir.md 9.4).
fn main.f() -> void {
    let _1: u8@ mut* own;

    bb0: {
        _1 = alloc<u8@>(const u64 1) #2:5 s1;
        return #3:1 s0;
    }
}
