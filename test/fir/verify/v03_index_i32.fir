//! pass: verify
//! panic: fir.verify: V3 an index that is not a local of type u64: main.f bb0 statement 0 (s1)
// V3: the index of an index projection is a local of type u64. The lowering casts a source
// index to u64 before it indexes (spec/fir.md 9.4).
fn main.f(_1 (a): i32[4], _2 (i): i32) -> i32 {
    bb0: {
        _0 = copy _1[_2] #2:12 s1;
        return #2:5 s1;
    }
}
