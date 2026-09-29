//! pass: verify
//! panic: fir.verify: V10 live or dead of a local that is not named: main.f bb0 statement 0 (s0)
// V10: `live` and `dead` name a named local. A parameter has storage for the whole function and
// no scope marker.
fn main.f(_1 (c): bool) -> void {
    bb0: {
        dead(_1) #2:1 s0;
        return #2:1 s0;
    }
}
