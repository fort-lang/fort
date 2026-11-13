//! pass: verify
//! pass: run main.f 3
//! pass: run main.g 3
//! prelude:
//| struct pair {
//|     u8 a;
//|     u64 b;
//| }
//! expect:
//| return u64 0
//| return u64 0
// Each `live` gives each byte of the storage of its local the value 0 (spec/fir.md 7, D10.1).
// `f` runs its block `n` times. Each run adds the scalar `x` to the result right after `live(x)`
// and then stores 18446744073709551615 into `x`, which has a bit in each byte: run 2 and run 3
// read 0 and no byte of the run before. `g` does the same with the field `b` of an aggregate,
// which `live` writes whole. The field `b` holds the last byte of the aggregate.
fn main.f(_1 (n): u64) -> u64 {
    let _2 (x): u64;
    let _3 (i): u64;
    let _4: bool;

    bb0: {
        live(_3);
        _3 = const u64 0;
        _0 = const u64 0;
        goto bb1;
    }
    bb1: {
        _4 = lt(copy _3, copy _1);
        switch(copy _4) -> [true: bb2, otherwise: bb3];
    }
    bb2: {
        live(_2);
        _0 = add(copy _0, copy _2);
        _2 = const u64 18446744073709551615;
        dead(_2);
        _3 = add(copy _3, const u64 1);
        goto bb1;
    }
    bb3: {
        dead(_3);
        return;
    }
}
fn main.g(_1 (n): u64) -> u64 {
    let _2 (p): pair;
    let _3 (i): u64;
    let _4: bool;

    bb0: {
        live(_3);
        _3 = const u64 0;
        _0 = const u64 0;
        goto bb1;
    }
    bb1: {
        _4 = lt(copy _3, copy _1);
        switch(copy _4) -> [true: bb2, otherwise: bb3];
    }
    bb2: {
        live(_2);
        _0 = add(copy _0, copy _2.1);
        _2 = aggregate pair(const u8 1, const u64 18446744073709551615);
        dead(_2);
        _3 = add(copy _3, const u64 1);
        goto bb1;
    }
    bb3: {
        dead(_3);
        return;
    }
}
