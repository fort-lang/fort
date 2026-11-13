//! pass: verify
//! pass: run main.f
//! prelude:
//| struct two {
//|     u64 k;
//|     i32 mut* own p;
//| }
//! expect:
//| event overwrite in main.f at 22:9
//| return i32 0
// The zero bytes of a `live` are a store over the whole local for the shadow state of the run
// (spec/fir.md 7, 11), and not over its first byte or its first word. The second `live` of `t`
// runs while the second word of `t` holds its allocation, so the store of 0 goes over a live
// owner there: the report names the `overwrite` event at that `live`, and the `dead` reports no
// `leak`. `run/012_live_overwrite.fir` holds the same for a local of one word.
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
