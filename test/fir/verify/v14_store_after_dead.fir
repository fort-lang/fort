//! pass: verify
//! prelude:
//| fn grab() u8 mut* own {
//|     return new(u8);
//| }
//! panic: fir.verify: V14 a return in the scope of a named owner local: main.f bb0 statement 4 (s3)
// V14: a store of a value into `p` after its `dead` opens the scope again, as the `ended`
// dataflow of owner.prover reads it. The `return` leaves an allocation in `p` that no `dead`
// answers for.
fn main.f() -> void {
    let _1 (p): u8 mut* own #2:5;

    bb0: {
        _1 = const null u8 mut* own #1:1 s0;
        live(_1) #2:5 s1;
        dead(_1) #3:1 s2;
        _1 = call fn main.grab() #3:5 s2;
        return #4:1 s3;
    }
}
