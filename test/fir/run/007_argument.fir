//! pass: run main.f
//! prelude:
//| fn show(i64 v) void {
//|     println(v);
//| }
//! expect:
//| fault null dereference at 13:9
// The run reads each argument in the frame of the caller before it pushes the frame of the
// callee, so a fault in an argument stands at the call.
fn main.f() -> void {
    let _1: i64*;
    let _2: void;

    bb0: {
        _1 = const null i64*;
        _2 = call fn main.show(copy (*_1)) #13:9;
        return;
    }
}
