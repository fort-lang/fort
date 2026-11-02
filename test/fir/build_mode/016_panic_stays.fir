//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.pa() -> void {
//|     let _0: void;
//|     let _1: string;
//|
//|     bb0: {
//|         _1 = const "stop" #27:11 s1;
//|         fail(panic, copy _1) #27:5 s1;
//|     }
//| }
// The lowering of `panic("stop");`: the message in a `string` temporary, then the `fail`. No build
// mode removes `fail(panic, ...)`.
fn main.pa() -> void {
    let _0: void;
    let _1: string;

    bb0: {
        _1 = const "stop" #27:11 s1;
        fail(panic, copy _1) #27:5 s1;
    }
}
