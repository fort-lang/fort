//! pass: verify
//! panic: fir.verify: V6 a cast adds own: main.f bb0 statement 0 (s1)
// V6: `cast(q, u8 mut* own)` of the view `q: u8 mut*` adds `own`, which a cast never does
// (D3.14). Ownership enters a program only through `new` and through an `extern` that says
// `own`, so the cast would give the storage of `q` a second owner.
fn main.f(_1 (q): u8 mut*) -> void {
    let _2: u8 mut* own;

    bb0: {
        _2 = cast<u8 mut* own>(copy _1) #2:5 s1;
        del(move _2) #2:5 s1;
        return #3:1 s0;
    }
}
