//! pass: verify
//! panic: fir.verify: V14 a live in the scope of a named owner local: main.f bb0 statement 2 (s0)
// V14: the second `live` of `p` comes while `p` holds its allocation. A `live` writes the zero
// value into each byte of its local (spec/fir.md 12.2), so it writes over the owner, and the
// allocation has no owner left. owner.prover reads a `live` as no store and asks it no proof, so
// the verifier refuses the text. The interpreter suite holds the `overwrite` event of the run
// (test/fort/fir/interpreter_test.ft, `live_over`).
fn main.f() -> i32 {
    let _1 (p): i32 mut* own;

    bb0: {
        live(_1) #14:9;
        _1 = alloc<i32>(const u64 1) #15:9;
        live(_1) #16:9;
        _0 = const i32 0 #17:9;
        dead(_1) #18:9;
        return #19:9;
    }
}
