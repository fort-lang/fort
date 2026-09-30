//! pass: verify
//! expect:
//| fn main.f(_1 (c): bool, _2 (d): bool) -> i32 {
//|     let _0: i32;
//|     let _3 (n): i32 #2:13;
//|     let _4 (p): u8 mut* own #4:21;
//|     let _5: u8 mut* own #4:25;
//|     let _6: bool #7:15;
//|     let _7: i32 #7:13;
//|     let _8: bool #8:15;
//|
//|     bb0: {
//|         _4 = const zero u8 mut* own #4:21 s0;
//|         live(_3) #2:13 s1;
//|         _3 = const i32 0 #2:17 s1;
//|         goto bb1 #3:5 s2;
//|     }
//|     bb1: {
//|         switch(copy _1) -> [true: bb2, otherwise: bb3] #3:12 s2;
//|     }
//|     bb2: {
//|         live(_4) #4:21 s3;
//|         _5 = alloc<u8>(const u64 1) #4:25 s3;
//|         check(overwrite: _4) -> bb4 #4:21 s3;
//|     }
//|     bb3: {
//|         _0 = copy _3 #11:12 s9;
//|         dead(_3) #11:5 s9;
//|         return #11:5 s9;
//|     }
//|     bb4: {
//|         _4 = move _5 #4:21 s3;
//|         switch(copy _2) -> [true: bb5, otherwise: bb6] #6:9 s5;
//|     }
//|     bb5: {
//|         del(move _4) #5:15 exit 6:19 s7;
//|         dead(_4) #6:19 s6;
//|         goto bb3 #6:19 s6;
//|     }
//|     bb6: {
//|         _6 = add_overflows(copy _3, const i32 1) #7:15 s8;
//|         check(copy _6, overflow) -> bb7 #7:15 s8;
//|     }
//|     bb7: {
//|         _7 = add(copy _3, const i32 1) #7:15 s8;
//|         _3 = copy _7 #7:9 s8;
//|         _8 = gt(copy _3, const i32 10) #8:15 s10;
//|         switch(copy _8) -> [true: bb8, otherwise: bb9] #8:9 s10;
//|     }
//|     bb8: {
//|         del(move _4) #5:15 exit 8:23 s12;
//|         dead(_4) #8:23 s11;
//|         goto bb1 #8:23 s11;
//|     }
//|     bb9: {
//|         del(move _4) #5:15 exit 9:5 s13;
//|         dead(_4) #9:5 s2;
//|         goto bb1 #9:5 s2;
//|     }
//| }
// V9 and V11 to V13 on the lowering of spec/fir.md 9.3 and 9.5 for this body:
//     i32 mut n = 0;
//     while (c) {
//         u8 mut* own p = new(u8);
//         defer del(p);
//         if (d) { break; }
//         n = n + 1;
//         if (n > 10) { continue; }
//     }
//     return n;
// The deferred `del` stands at each of the three exits of the loop body: the `break` in bb5, the
// `continue` in bb8 and the end of the body in bb9. Each check has one predecessor, and each
// temporary is assigned on every path to its reads.
fn main.f(_1 (c): bool, _2 (d): bool) -> i32 {
    let _3 (n): i32 #2:13;
    let _4 (p): u8 mut* own #4:21;
    let _5: u8 mut* own #4:25;
    let _6: bool #7:15;
    let _7: i32 #7:13;
    let _8: bool #8:15;

    bb0: {
        _4 = const zero u8 mut* own #4:21 s0;
        live(_3) #2:13 s1;
        _3 = const i32 0 #2:17 s1;
        goto bb1 #3:5 s2;
    }
    bb1: {
        switch(copy _1) -> [true: bb2, otherwise: bb3] #3:12 s2;
    }
    bb2: {
        live(_4) #4:21 s3;
        _5 = alloc<u8>(const u64 1) #4:25 s3;
        check(overwrite: _4) -> bb4 #4:21 s3;
    }
    bb3: {
        _0 = copy _3 #11:12 s9;
        dead(_3) #11:5 s9;
        return #11:5 s9;
    }
    bb4: {
        _4 = move _5 #4:21 s3;
        switch(copy _2) -> [true: bb5, otherwise: bb6] #6:9 s5;
    }
    bb5: {
        del(move _4) #5:15 exit 6:19 s7;
        dead(_4) #6:19 s6;
        goto bb3 #6:19 s6;
    }
    bb6: {
        _6 = add_overflows(copy _3, const i32 1) #7:15 s8;
        check(copy _6, overflow) -> bb7 #7:15 s8;
    }
    bb7: {
        _7 = add(copy _3, const i32 1) #7:15 s8;
        _3 = copy _7 #7:9 s8;
        _8 = gt(copy _3, const i32 10) #8:15 s10;
        switch(copy _8) -> [true: bb8, otherwise: bb9] #8:9 s10;
    }
    bb8: {
        del(move _4) #5:15 exit 8:23 s12;
        dead(_4) #8:23 s11;
        goto bb1 #8:23 s11;
    }
    bb9: {
        del(move _4) #5:15 exit 9:5 s13;
        dead(_4) #9:5 s2;
        goto bb1 #9:5 s2;
    }
}
