//! pass: verify
//! panic: V11 the continuation of a check has another predecessor: main.f bb1 statement 1 (s2)
// V11: the continuation of the overflow check of `a + b` has one predecessor, the check. bb3 jumps
// into it, so the translator could not name the sum of the check in bb2.
fn main.f(_1 (c): bool, _2 (a): i32, _3 (b): i32) -> i32 {
    let _4: bool;
    let _5: i32;

    bb0: {
        switch(copy _1) -> [true: bb1, otherwise: bb3] #2:5 s1;
    }
    bb1: {
        _4 = add_overflows(copy _2, copy _3) #3:18 s2;
        check(copy _4, overflow) -> bb2 #3:18 s2;
    }
    bb2: {
        _5 = add(copy _2, copy _3) #3:18 s2;
        _0 = copy _5 #3:16 s2;
        return #3:9 s2;
    }
    bb3: {
        goto bb2 #5:5 s3;
    }
}
