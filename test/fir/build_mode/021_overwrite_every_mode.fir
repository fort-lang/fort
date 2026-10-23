//! pass: build-mode
//! pass: verify
//! prelude:
//| struct box {
//|     i32 mut* own p;
//| }
//| fn f(bool c) i32 {
//|     return 0;
//| }
//! expect:
//| fn main.f(_1 (c): bool) -> i32 {
//|     let _0: i32;
//|     let _2 (p): i32 mut* own #2:18;
//|     let _3: i32 mut* own #2:22;
//|
//|     bb0: {
//|         _2 = const zero i32 mut* own #2:18 s0;
//|         live(_2) #2:18 s1;
//|         _2 = alloc<i32>(const u64 1) #2:22 s1;
//|         switch(copy _1) -> [true: bb1, otherwise: bb2] #4:5 s3;
//|     }
//|     bb1: {
//|         _0 = const i32 1 #5:16 s4;
//|         del(move _2) #3:11 exit 5:9 s5;
//|         dead(_2) #5:9 s4;
//|         return #5:9 s4;
//|     }
//|     bb2: {
//|         (*_2) = const i32 7 #7:8 s6;
//|         _0 = copy (*_2) #8:12 s7;
//|         del(move _2) #3:11 exit 8:5 s8;
//|         dead(_2) #8:5 s7;
//|         return #8:5 s7;
//|     }
//| }
//|
//| fn main.g(_1 (b): box mut*) -> void {
//|     let _0: void;
//|     let _2: i32 mut* own #3:14;
//|
//|     bb0: {
//|         del(move (*_1).0) #2:5 s1;
//|         (*_1).0 = alloc<i32>(const u64 1) #3:14 s1;
//|         return #4:1 s0;
//|     }
//| }
//|
//| fn main.h(_1 (b): box mut*) -> void {
//|     let _0: void;
//|     let _2: i32 mut* own #3:14;
//|
//|     bb0: {
//|         _2 = alloc<i32>(const u64 1) #3:14 s1;
//|         check(overwrite: (*_1).0) -> bb1 #3:12 s1;
//|     }
//|     bb1: {
//|         (*_1).0 = move _2 #3:12 s1;
//|         return #4:1 s0;
//|     }
//| }
// The default mode removes each overwrite check that the emptiness pass proves, as --release does
// (006), and keeps each other one (spec/fir.md 11). In `f` and `g` the two statements around the
// check fold and the continuation merges, so their text is the text of 006. `h` stores through
// its parameter with no `del`, so the pass does not prove its check, and the check stays.
fn main.f(_1 (c): bool) -> i32 {
    let _0: i32;
    let _2 (p): i32 mut* own #2:18;
    let _3: i32 mut* own #2:22;

    bb0: {
        _2 = const zero i32 mut* own #2:18 s0;
        live(_2) #2:18 s1;
        _3 = alloc<i32>(const u64 1) #2:22 s1;
        check(overwrite: _2) -> bb1 #2:18 s1;
    }
    bb1: {
        _2 = move _3 #2:18 s1;
        switch(copy _1) -> [true: bb2, otherwise: bb3] #4:5 s3;
    }
    bb2: {
        _0 = const i32 1 #5:16 s4;
        del(move _2) #3:11 exit 5:9 s5;
        dead(_2) #5:9 s4;
        return #5:9 s4;
    }
    bb3: {
        (*_2) = const i32 7 #7:8 s6;
        _0 = copy (*_2) #8:12 s7;
        del(move _2) #3:11 exit 8:5 s8;
        dead(_2) #8:5 s7;
        return #8:5 s7;
    }
}

fn main.g(_1 (b): box mut*) -> void {
    let _2: i32 mut* own #3:14;

    bb0: {
        del(move (*_1).0) #2:5 s1;
        _2 = alloc<i32>(const u64 1) #3:14 s1;
        check(overwrite: (*_1).0) -> bb1 #3:12 s1;
    }
    bb1: {
        (*_1).0 = move _2 #3:12 s1;
        return #4:1 s0;
    }
}

fn main.h(_1 (b): box mut*) -> void {
    let _2: i32 mut* own #3:14;

    bb0: {
        _2 = alloc<i32>(const u64 1) #3:14 s1;
        check(overwrite: (*_1).0) -> bb1 #3:12 s1;
    }
    bb1: {
        (*_1).0 = move _2 #3:12 s1;
        return #4:1 s0;
    }
}
