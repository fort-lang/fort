//! pass: ownership-local
//! error: ownership_local/001_lost_owner.fir:13:9: error: lost ownership of 'p'
//! error: ownership_local/001_lost_owner.fir:13:9: note: storage ends with a live owner
// D17.14: a dead marker ends local storage, and its residual owned leaf needs a release or a
// transfer. The pass reports the loss as a source error and exits 1, not with a panic. Each
// location names its own line of this file.
fn main.leak() -> void {
    let _1 (p): i32 mut* own #8:5;

    bb0: {
        live(_1) #11:9 s0;
        _1 = alloc<i32>(const u64 1) #12:9 s0;
        dead(_1) #13:9 s1;
        return #14:9 s1;
    }
}
