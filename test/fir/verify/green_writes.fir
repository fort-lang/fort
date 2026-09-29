//! pass: verify
//! prelude:
//| i32 mut total = 0;
//| i32 mut* TARGET = &total;
//| u8 mut* own KEPT = null;
//| fn nothing() void {
//| }
//! expect:
//| fn main.f(_1 (x): i32) -> void {
//|     let _0: void;
//|     let _2: void;
//|     let _3: i32;
//|
//|     bb0: {
//|         global main.total = const i32 1 #2:5 s1;
//|         (*global main.TARGET) = const i32 2 #3:5 s2;
//|         _3 = copy global main.total #4:5 s3;
//|         _2 = call fn main.nothing() #5:5 s4;
//|         _2 = copy _0 #6:5 s5;
//|         check(overwrite: global main.KEPT) -> bb1 #7:5 s6;
//|     }
//|     bb1: {
//|         switch(copy _1) -> [i32 1: bb2, i32 -1: bb2, otherwise: bb2] #8:5 s7;
//|     }
//|     bb2: {
//|         return #9:1 s0;
//|     }
//| }
// The edges of the clauses on writes, on switch arms and on the overwrite check: a write into a
// `mut` global, a write behind the first indirection of a constant, a read of a global, the
// place form of the overwrite check on a constant, a `void` temporary for a `void` call, and
// switch arms that differ in sign alone. `_2 = copy _0` is no edge but a gap: 5.3 says that
// nothing reads `_0` of a `void` function, and no rule of section 10 tests the read.
fn main.f(_1 (x): i32) -> void {
    let _2: void;
    let _3: i32;

    bb0: {
        global main.total = const i32 1 #2:5 s1;
        (*global main.TARGET) = const i32 2 #3:5 s2;
        _3 = copy global main.total #4:5 s3;
        _2 = call fn main.nothing() #5:5 s4;
        _2 = copy _0 #6:5 s5;
        check(overwrite: global main.KEPT) -> bb1 #7:5 s6;
    }
    bb1: {
        switch(copy _1) -> [i32 1: bb2, i32 -1: bb2, otherwise: bb2] #8:5 s7;
    }
    bb2: {
        return #9:1 s0;
    }
}
