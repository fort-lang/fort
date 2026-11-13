//! pass: verify
//! pass: run main.f
//! pass: run main.flag
//! pass: run main.small
//! pass: run main.pick false
//! pass: run main.pick true
//! expect:
//| return u64 0
//| return false
//| return i8 0
//| return i32 0
//| return i32 5
// The result `_0` of a scalar type holds the zero value at the entry of its function
// (spec/fir.md 12.1, D10.1). No rule of the verifier asks for an assignment of `_0` before a
// `return`, so FIR text can reach a `return` with none. `f`, `flag` and `small` assign nothing.
// `pick` assigns `_0` on one path of two. The passes of this test run the interpreter only.
// `the_entry_block_stores_zero_into_a_scalar_result` of test/fort/fir_llvm_test.ft holds the
// store that the translation writes.
fn main.f() -> u64 {
    bb0: {
        return;
    }
}
fn main.flag() -> bool {
    bb0: {
        return;
    }
}
fn main.small() -> i8 {
    bb0: {
        return;
    }
}
fn main.pick(_1 (c): bool) -> i32 {
    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb2];
    }
    bb1: {
        _0 = const i32 5;
        goto bb2;
    }
    bb2: {
        return;
    }
}
