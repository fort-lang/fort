//! pass: verify
//! panic: fir.verify: V3 a write into a global constant: main.f bb0 statement 0 (s1)
//! prelude:
//| struct pair {
//|     i32 a;
//|     u64 b;
//| }
//| pair ORIGIN = pair{1, 2};
// V3: a module-level constant lives in read-only memory (spec/fir.md 5.5, D7.10), so no
// statement writes a place that names one before its first indirection. A field of the constant
// is the constant's own storage.
fn main.f() -> void {
    bb0: {
        global main.ORIGIN.0 = const i32 5 #2:5 s1;
        return #3:1 s0;
    }
}
