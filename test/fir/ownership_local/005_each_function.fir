//! pass: verify
//! pass: ownership-local
//! error: ownership_local/005_each_function.fir:21:9: error: lost ownership of 'p'
//! error: ownership_local/005_each_function.fir:21:9: note: storage ends with a live owner
//! error: ownership_local/005_each_function.fir:42:9: error: lost ownership of 'p'
//! error: ownership_local/005_each_function.fir:42:9: note: move would discard a live owner
// The verifier keeps every rule of these functions, and the pass `ownership-local` after it
// reports the losses. The pass analyzes each function in text order and continues after an
// error. The first function loses its owner on the path that skips the release. The second
// overwrites a live owner, which loses the old allocation (D17.11, D17.14). The test
// 003_complete.fir holds that a function with no loss gets no error.
fn main.one_path(_1 (c): bool) -> void {
    let _2 (p): i32 mut* own #13:5;

    bb0: {
        live(_2) #16:9 s0;
        _2 = alloc<i32>(const u64 1) #17:9 s0;
        switch(copy _1) -> [false: bb1, otherwise: bb2] #18:9 s1;
    }
    bb1: {
        dead(_2) #21:9 s2;
        return #22:9 s2;
    }
    bb2: {
        del(move _2) #25:9 s3;
        dead(_2) #26:9 s4;
        return #27:9 s4;
    }
}

fn main.replace() -> void {
    let _1 (p): i32 mut* own #32:5;
    let _2: i32 mut* own #33:5;

    bb0: {
        live(_1) #36:9 s0;
        _1 = alloc<i32>(const u64 1) #37:9 s0;
        _2 = alloc<i32>(const u64 1) #38:9 s1;
        check(overwrite: _1) -> bb1 #39:9 s1;
    }
    bb1: {
        _1 = move _2 #42:9 s1;
        del(move _1) #43:9 s2;
        dead(_1) #44:9 s3;
        return #45:9 s3;
    }
}
