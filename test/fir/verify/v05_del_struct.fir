//! pass: verify
//! panic: fir.verify: V5 a del of a place that is not an own reference: main.f bb0 statement 0 (s1)
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
// V5: `del` takes a move of an own reference (D17.9). `holder` owns, but it is an aggregate, and
// `del` of an aggregate is an error: a struct frees its own fields (D17.7).
fn main.f(_1 (h): holder) -> void {
    bb0: {
        del(move _1) #2:5 s1;
        return #3:1 s0;
    }
}
