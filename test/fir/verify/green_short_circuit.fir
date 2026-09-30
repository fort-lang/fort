//! pass: verify
//! expect:
//| fn main.f(_1 (a): bool, _2 (b): bool) -> bool {
//|     let _0: bool;
//|     let _3: bool;
//|
//|     bb0: {
//|         _3 = copy _1 #2:12 s1;
//|         switch(copy _3) -> [true: bb1, otherwise: bb2] #2:14 s1;
//|     }
//|     bb1: {
//|         _3 = copy _2 #2:17 s1;
//|         goto bb2 #2:14 s1;
//|     }
//|     bb2: {
//|         _0 = copy _3 #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// V13: `return a && b;` as spec/fir.md 9.4 lowers it. The slot `_3` takes `a`, the right side
// assigns it again, and the join reads it on both paths.
fn main.f(_1 (a): bool, _2 (b): bool) -> bool {
    let _3: bool;

    bb0: {
        _3 = copy _1 #2:12 s1;
        switch(copy _3) -> [true: bb1, otherwise: bb2] #2:14 s1;
    }
    bb1: {
        _3 = copy _2 #2:17 s1;
        goto bb2 #2:14 s1;
    }
    bb2: {
        _0 = copy _3 #2:12 s1;
        return #2:5 s1;
    }
}
