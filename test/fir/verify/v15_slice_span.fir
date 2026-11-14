//! pass: verify
//! prelude:
//| struct view {
//|     u64 n;
//|     i32@ cells;
//| }
//! panic: fir.verify: V15 an addr or a slice of an aggregate result: main.f bb0 statement 1 (s2)
// V15: the rule refuses each `slice` whose place is fixed storage of `_0`. A `slice` of a span
// member reads the span and gives no address of `_0`; the rule asks no type of the place, and the
// lowering names `_0` in no `slice`.
fn main.f(_1 (v): view) -> view {
    let _2: i32@;
    bb0: {
        _0 = copy _1 #2:5 s1;
        _2 = slice(_0.1, const u64 0, const u64 0) #3:5 s2;
        return #4:5 s3;
    }
}
