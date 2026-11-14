//! pass: verify
//! prelude:
//| struct row {
//|     u64 n;
//|     i32[2] cells;
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 1 (s2)
// V15: a `slice` of a fixed array member of `_0` gives a span of the storage of `_0`.
fn main.f(_1 (r): row) -> row {
    let _2: i32 mut@;
    bb0: {
        _0 = copy _1 #2:5 s1;
        _2 = slice mut(_0.1, const u64 0, const u64 2) #3:5 s2;
        return #4:5 s3;
    }
}
