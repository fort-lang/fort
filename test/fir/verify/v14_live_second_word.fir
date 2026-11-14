//! pass: verify
//! prelude:
//| struct two {
//|     u64 k;
//|     i32 mut* own p;
//| }
//! panic: fir.verify: V14 a live in the scope of a named owner local: main.f bb0 statement 2 (s0)
// V14: the store into the second word of `t` opens the scope of `t`, and the second `live` of
// `t` writes the zero value over the whole local (spec/fir.md 12.2), the owner in that word
// included. `v14_live_over_owner.fir` holds the same for a local of one word.
fn main.f() -> i32 {
    let _1 (t): two;

    bb0: {
        live(_1) #20:9;
        _1.1 = alloc<i32>(const u64 1) #21:9;
        live(_1) #22:9;
        _0 = const i32 0 #23:9;
        dead(_1) #24:9;
        return #25:9;
    }
}
