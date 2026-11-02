//! pass: verify
//! expect:
//| fn main.f(_1 (v): void*) -> i64* {
//|     let _0: i64*;
//|     let _2: u64 #2:12;
//|
//|     bb0: {
//|         _2 = cast<u64>(copy _1) #2:12 s1;
//|         _0 = cast<i64*>(copy _2) #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// V4 admits a `void*` that goes to a typed pointer through `u64`: the row from `u64` stays for
// hand-written FIR text, which forges a pointer on purpose (spec/fir.md 4).
fn main.f(_1 (v): void*) -> i64* {
    let _2: u64 #2:12;

    bb0: {
        _2 = cast<u64>(copy _1) #2:12 s1;
        _0 = cast<i64*>(copy _2) #2:12 s1;
        return #2:5 s1;
    }
}
