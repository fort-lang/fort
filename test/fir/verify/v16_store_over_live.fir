//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! panic: fir.verify: V16 a store over a live scratch local: main.f bb0 statement 1 (s2)
// V16: the second call stores over the allocation that `_1` holds. No lvalue names a temporary,
// so owner.prover counts a store into one as proved past its work limit (D17.8).
fn main.f() -> void {
    let _1: u8 mut* own;

    bb0: {
        _1 = call fn main.grab() #2:5 s1;
        _1 = call fn main.grab() #3:5 s2;
        del(move _1) #4:5 s3;
        return #5:1 s4;
    }
}
