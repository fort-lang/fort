//! pass: ownership-local
//! prelude:
//| fn keep(i32 mut* p) void {
//| }
//! error: ownership_local/004_fort_call.fir:18:9: error: ownership proof is incomplete (local: missing_path)
// D17.18: the local analysis has no summary of a fort call. The call can end the process or
// take the allocation, so the owner at the dead marker is only a possible loss. That is an
// incomplete proof, which is an error with exit 1, and no violation.
fn main.caller() -> void {
    let _0: void;
    let _1 (p): i32 mut* own #11:5;
    let _2: void;

    bb0: {
        live(_1) #15:9 s0;
        _1 = alloc<i32>(const u64 1) #16:9 s0;
        _2 = call fn main.keep(copy _1) #17:9 s1;
        dead(_1) #18:9 s2;
        return #19:9 s2;
    }
}
