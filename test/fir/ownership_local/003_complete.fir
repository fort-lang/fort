//! pass: ownership-local
//! expect:
//| fn main.release() -> void {
//|     let _0: void;
//|     let _1 (p): i32 mut* own #41:5;
//|
//|     bb0: {
//|         live(_1) #44:9 s0;
//|         _1 = alloc<i32>(const u64 1) #45:9 s0;
//|         del(move _1) #46:9 s1;
//|         dead(_1) #47:9 s2;
//|         return #48:9 s2;
//|     }
//| }
//|
//| fn main.both(_1 (c): bool) -> void {
//|     let _0: void;
//|     let _2 (p): i32 mut* own #53:5;
//|
//|     bb0: {
//|         live(_2) #56:9 s0;
//|         _2 = alloc<i32>(const u64 1) #57:9 s0;
//|         switch(copy _1) -> [false: bb1, otherwise: bb2] #58:9 s1;
//|     }
//|     bb1: {
//|         del(move _2) #61:9 s2;
//|         dead(_2) #62:9 s3;
//|         return #63:9 s3;
//|     }
//|     bb2: {
//|         del(move _2) #66:9 s4;
//|         dead(_2) #67:9 s5;
//|         return #68:9 s5;
//|     }
//| }
// D17.14: each function releases its owner on each path before the dead marker, so the local
// proof of each function is complete. The pass changes nothing and writes the module as `none`
// does, so a false error in either function makes the test fail. A complete local proof of one
// function is no proof of a closure (toolchain.md 1.1).
fn main.release() -> void {
    let _1 (p): i32 mut* own #41:5;

    bb0: {
        live(_1) #44:9 s0;
        _1 = alloc<i32>(const u64 1) #45:9 s0;
        del(move _1) #46:9 s1;
        dead(_1) #47:9 s2;
        return #48:9 s2;
    }
}

fn main.both(_1 (c): bool) -> void {
    let _2 (p): i32 mut* own #53:5;

    bb0: {
        live(_2) #56:9 s0;
        _2 = alloc<i32>(const u64 1) #57:9 s0;
        switch(copy _1) -> [false: bb1, otherwise: bb2] #58:9 s1;
    }
    bb1: {
        del(move _2) #61:9 s2;
        dead(_2) #62:9 s3;
        return #63:9 s3;
    }
    bb2: {
        del(move _2) #66:9 s4;
        dead(_2) #67:9 s5;
        return #68:9 s5;
    }
}
