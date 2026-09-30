//! pass: none
//! pass: verify
//! prelude:
//| struct pair {
//|     u8 a;
//|     u64 b;
//| }
//| struct zeroed {
//|     i32 x;
//| }
//! expect:
//| fn main.make(_1 (b): u64) -> pair {
//|     let _0: pair;
//|
//|     bb0: {
//|         _0 = aggregate zeroed pair(const zero u8, copy _1) #5:3 s1;
//|         return #6:1 s1;
//|     }
//| }
//|
//| fn main.plain(_1 (b): u64) -> pair {
//|     let _0: pair;
//|
//|     bb0: {
//|         _0 = aggregate pair(const u8 0, copy _1) #5:3 s1;
//|         return #6:1 s1;
//|     }
//| }
//|
//| fn main.named() -> zeroed {
//|     let _0: zeroed;
//|
//|     bb0: {
//|         _0 = aggregate zeroed(const i32 1) #5:3 s1;
//|         return #6:1 s1;
//|     }
//| }
//|
//| fn main.both() -> zeroed {
//|     let _0: zeroed;
//|
//|     bb0: {
//|         _0 = aggregate zeroed zeroed(const zero i32) #5:3 s1;
//|         return #6:1 s1;
//|     }
//| }
// `aggregate zeroed T(...)` is the value of a designated literal: the translator zeroes the whole
// destination, padding included (spec/fir.md 6). The parser reads `zeroed` as the mark only when
// a type follows it, so `aggregate zeroed(...)` names a struct of that name, and the verifier
// takes both forms.
fn main.make(_1 (b): u64) -> pair {
    bb0: {
        _0 = aggregate zeroed pair(const zero u8, copy _1) #5:3 s1;
        return #6:1 s1;
    }
}

fn main.plain(_1 (b): u64) -> pair {
    bb0: {
        _0 = aggregate pair(const u8 0, copy _1) #5:3 s1;
        return #6:1 s1;
    }
}

fn main.named() -> zeroed {
    bb0: {
        _0 = aggregate zeroed(const i32 1) #5:3 s1;
        return #6:1 s1;
    }
}

fn main.both() -> zeroed {
    bb0: {
        _0 = aggregate zeroed zeroed(const zero i32) #5:3 s1;
        return #6:1 s1;
    }
}
