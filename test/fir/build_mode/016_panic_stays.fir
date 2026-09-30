//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.pa() -> void {
//|     let _0: void;
//|
//|     bb0: {
//|         fail(panic, const bytes "stop", const u64 4) #27:5 s1;
//|     }
//| }
// The lowering of `panic("stop");`. No build mode removes `fail(panic, ...)`.
fn main.pa() -> void {
    let _0: void;

    bb0: {
        fail(panic, const bytes "stop", const u64 4) #27:5 s1;
    }
}
