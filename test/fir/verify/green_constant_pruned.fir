//! pass: verify
//! expect:
//| fn main.f() -> i32 {
//|     let _0: i32;
//|
//|     bb0: {
//|         goto bb1 #2:5 s1;
//|     }
//|     bb1: {
//|         switch(const true) -> [true: bb2, otherwise: bb3] #2:12 s1;
//|     }
//|     bb2: {
//|         goto bb1 #2:5 s1;
//|     }
//|     bb3: {
//|         unreachable #4:1 s0;
//|     }
//| }
// V9: `while (true) { }` in a function that returns a value. The switch on the constant takes
// only its true edge, so no path from bb0 reaches the unreachable of the end (D8.4).
fn main.f() -> i32 {
    bb0: {
        goto bb1 #2:5 s1;
    }
    bb1: {
        switch(const true) -> [true: bb2, otherwise: bb3] #2:12 s1;
    }
    bb2: {
        goto bb1 #2:5 s1;
    }
    bb3: {
        unreachable #4:1 s0;
    }
}
