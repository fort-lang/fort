//! pass: ownership-local
//! prelude:
//| fn take(i32 mut* own q) void {
//|     del(q);
//| }
//! error: ownership_local/007_owning_call.fir:20:9: error: ownership proof is incomplete (local: missing_path)
// D17.18: the parameter of take owns, so the call can take ownership, and the local analysis
// has no summary of it. The call is a barrier that the walk does not cross, so the owner at
// the dead marker is only a possible loss: incomplete proof, an error with exit 1.
fn main.caller() -> void {
    let _0: void;
    let _1 (p): i32 mut* own #12:5;
    let _2: void;

    bb0: {
        live(_1) #16:9 s0;
        _1 = alloc<i32>(const u64 1) #17:9 s0;
        _2 = call fn main.take(const null i32 mut* own) #18:9 s1;
        dead(_1) #20:9 s2;
        return #21:9 s2;
    }
}
