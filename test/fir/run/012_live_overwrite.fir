//! pass: verify
//! pass: run main.f
//! expect:
//| event overwrite in main.f at 16:9
//| return i32 0
// The zero bytes of a `live` are a store for the shadow state of the run (spec/fir.md 7, 11). The
// second `live` of `p` runs while `p` holds its allocation, so the store of 0 goes over a live
// owner: the report names the `overwrite` event at that `live`. The allocation then has no owner
// left, and the `dead` reports no `leak` after it, as in `run/005_overwrite.fir`.
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
