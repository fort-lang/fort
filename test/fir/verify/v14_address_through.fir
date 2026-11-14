//! pass: verify
//! prelude:
//| fn put(u8 mut* own mut* p) void {
//|     del(*p);
//|     *p = new(u8);
//| }
//! panic: fir.verify: V14 a return in the scope of a named owner local: main.f bb0 statement 5 (s0)
// V14: `addr mut(_1)` opens the scope of `p`, although the store that keeps the address goes
// through a pointer and names no fixed storage. The call puts an allocation into `p` through
// that address, and the `return` comes with no `dead` of `p`.
fn main.f() -> void {
    let _1 (p): u8 mut* own;
    let _2 (q): u8 mut* own mut*;
    let _3: u8 mut* own mut* mut*;
    let _4: void;

    bb0: {
        _1 = const null u8 mut* own;
        _2 = const null u8 mut* own mut*;
        _3 = addr mut(_2);
        (*_3) = addr mut(_1);
        _4 = call fn main.put(copy _2);
        return;
    }
}
