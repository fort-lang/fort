//! pass: verify
//! panic: V13 a read of a temporary before an assignment of it: main.f bb3 statement 0 (s1)
// V13: the slot of `c ? 1 : x` with the false arm left out. The join reads `_2`, which the path
// through bb2 does not assign.
fn main.f(_1 (c): bool) -> i32 {
    let _2: i32;

    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb2] #2:12 s1;
    }
    bb1: {
        _2 = const i32 1 #2:16 s1;
        goto bb3 #2:12 s1;
    }
    bb2: {
        goto bb3 #2:12 s1;
    }
    bb3: {
        _0 = copy _2 #2:12 s1;
        return #2:5 s1;
    }
}
