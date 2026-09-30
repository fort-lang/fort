//! pass: build-mode --release --no-bounds-check
//! pass: verify
//! expect:
//| fn main.us(_1 (c): bool) -> void {
//|     let _0: void;
//|
//|     bb0: {
//|         check(copy _1, user, const bytes "c") -> bb1 #24:5 s1;
//|     }
//|     bb1: {
//|         return #25:1 s0;
//|     }
//| }
// The lowering of `assert(c);`. No build mode removes the check of the kind `user`, so bb1 stays
// a block of its own.
fn main.us(_1 (c): bool) -> void {
    let _0: void;

    bb0: {
        check(copy _1, user, const bytes "c") -> bb1 #24:5 s1;
    }
    bb1: {
        return #25:1 s0;
    }
}
