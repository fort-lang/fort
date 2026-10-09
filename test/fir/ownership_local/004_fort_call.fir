//! pass: ownership-local
//! prelude:
//| fn keep(i32 mut* p) void {
//| }
//! error: ownership_local/004_fort_call.fir:20:9: error: lost ownership of 'p'
//! error: ownership_local/004_fort_call.fir:20:9: note: storage ends with a live owner
// D17.9, D3.14: the parameter of keep has no own, so the call cannot take the allocation.
// The local walk takes the normal return that an unknown fort outcome keeps (D17.14), and
// the owner at the dead marker is a lost owner. 007_owning_call.fir holds the callee with an
// owning parameter, whose call stays a barrier.
fn main.caller() -> void {
    let _0: void;
    let _1 (p): i32 mut* own #13:5;
    let _2: void;

    bb0: {
        live(_1) #17:9 s0;
        _1 = alloc<i32>(const u64 1) #18:9 s0;
        _2 = call fn main.keep(copy _1) #19:9 s1;
        dead(_1) #20:9 s2;
        return #21:9 s2;
    }
}
