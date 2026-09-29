//! pass: verify
//! panic: fir.verify: V4 an alloc of void: main.f bb0 statement 0 (s1)
// V4: `alloc` takes a type other than `void`, as `new` does (D10.2).
fn main.f() -> void {
    let _1: void* mut* own;

    bb0: {
        _1 = alloc<void>(const u64 1) #2:5 s1;
        return #3:1 s0;
    }
}
