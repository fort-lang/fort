//! pass: verify
//! panic: V13 a read of a temporary before an assignment of it: main.f bb3 statement 0 (s1)
// V13 follows every edge, the false edge of a switch on a constant included, which V9 prunes.
// bb3 joins bb1, which assigns `_1`, and bb2, which does not.
fn main.f() -> i32 {
    let _1: i32;

    bb0: {
        switch(const true) -> [true: bb1, otherwise: bb2] #2:12 s1;
    }
    bb1: {
        _1 = const i32 1 #2:16 s1;
        goto bb3 #2:12 s1;
    }
    bb2: {
        goto bb3 #2:12 s1;
    }
    bb3: {
        _0 = copy _1 #2:12 s1;
        return #2:5 s1;
    }
}
