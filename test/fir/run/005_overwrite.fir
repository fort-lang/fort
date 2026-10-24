//! pass: run main.f
//! expect:
//| return i32 6
// The run comes before the emptiness pass: `check(overwrite: p)` continues over a live owner,
// whose allocation then has no owner left.
fn main.f() -> i32 {
    let _1 (p): i32 mut* own;
    let _2: i32 mut* own;

    bb0: {
        _1 = alloc<i32>(const u64 1);
        _2 = alloc<i32>(const u64 1);
        check(overwrite: _1) -> bb1;
    }
    bb1: {
        _1 = move _2;
        (*_1) = const i32 6;
        _0 = copy (*_1);
        del(move _1);
        return;
    }
}
