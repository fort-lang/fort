//! pass: none
//! prelude:
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
//|         _3 = alloc<i32>(const u64 1) #2:22 s1;
//|         check(overwrite: _2) -> bb1 #2:18 s1;
//|     }
//|     bb1: {
//|         _2 = move _3 #2:18 s1;
//|         switch(copy _1) -> [true: bb2, otherwise: bb3] #4:5 s3;
//|     }
//|     bb2: {
//|         _0 = const i32 1 #5:16 s4;
//|         del(move _2) #3:11 exit 5:9 s5;
//|         dead(_2) #5:9 s4;
//|         return #5:9 s4;
//|     }
//|     bb3: {
//|         (*_2) = const i32 7 #7:8 s6;
//|         _0 = copy (*_2) #8:12 s7;
//|         del(move _2) #3:11 exit 8:5 s8;
//|         dead(_2) #8:5 s7;
//|         return #8:5 s7;
//|     }
//| }
// The worked example of spec/fir.md 13 reads and prints back byte for byte. The prelude
// declares `f`, so `_1` takes the symbol of its parameter `c`.
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
