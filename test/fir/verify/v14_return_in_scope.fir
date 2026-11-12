//! pass: verify
//! panic: fir.verify: V14 a return in the scope of a named owner local: main.f bb1 statement 0 (s3)
// V14: bb1 returns in the scope of `p`, which `live` opened. bb2 writes the `dead` of `p` before
// its `return`, as the lowering does at each exit of a scope (spec/fir.md 9.5). The named local
// `n` holds no owner, so the rule does not read it.
fn main.f(_1 (c): bool) -> void {
    let _2 (p): u8 mut* own #2:9;
    let _3 (n): i32 #2:9;

    bb0: {
        _2 = const null u8 mut* own #1:1 s0;
        live(_2) #2:9 s1;
        live(_3) #2:9 s1;
        _3 = const i32 1 #2:9 s1;
        switch(copy _1) -> [true: bb1, otherwise: bb2] #3:9 s2;
    }
    bb1: {
        return #4:9 s3;
    }
    bb2: {
        dead(_2) #6:5 s4;
        return #6:5 s4;
    }
}
