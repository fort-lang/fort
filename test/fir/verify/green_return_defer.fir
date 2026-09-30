//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     i32 b;
//| }
//| fn note() void {
//| }
//! expect:
//| fn main.f(_1 (p): pair) -> pair {
//|     let _0: pair;
//|     let _2: void #2:11;
//|     let _3: pair #3:12;
//|
//|     bb0: {
//|         _3 = copy _1 #3:12 s2;
//|         _2 = call fn main.note() #2:11 exit 3:5 s3;
//|         _0 = copy _3 #3:5 s2;
//|         return #3:5 s2;
//|     }
//| }
// V12 and V13 on `return p;` of spec/fir.md 9.6 with an open `defer note();`. `_0` is the
// caller's storage, so the value goes into the temporary `_3` first; the deferred call runs,
// and `_0` takes a copy of `_3` after it.
fn main.f(_1 (p): pair) -> pair {
    let _2: void #2:11;
    let _3: pair #3:12;

    bb0: {
        _3 = copy _1 #3:12 s2;
        _2 = call fn main.note() #2:11 exit 3:5 s3;
        _0 = copy _3 #3:5 s2;
        return #3:5 s2;
    }
}
