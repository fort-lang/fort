//! pass: verify
//! expect:
//| fn main.f(_1 (a): i32[2]) -> i32 {
//|     let _0: i32;
//|     let _2: i32[2] #2:1;
//|     let _3: i32[2] #3:1;
//|
//|     bb0: {
//|         _2 = copy _1 #2:1 s1;
//|         _3 = copy _2 #3:1 s2;
//|         _0 = const i32 1 #4:1 s3;
//|         return #5:1 s4;
//|     }
//| }
fn main.f(_1 (a): i32[2]) -> i32 {
    let _2: i32[2] #2:1;
    let _3: i32[2] #3:1;

    bb0: {
        _2 = copy _1 #2:1 s1;
        _3 = copy _2 #3:1 s2;
        _0 = const i32 1 #4:1 s3;
        return #5:1 s4;
    }
}
