//! pass: verify
//! prelude:
//| extern fn puts(char* s) i32;
//| extern fn printf(char* format, ...) i32;
//| fn add(i32 x, i64 y) i64 {
//|     return 0;
//| }
//| fn show(string s) void {
//| }
//! expect:
//| fn main.f(_1 (x): i32, _2 (w): i64, _3 (s): string own) -> i64 {
//|     let _0: i64;
//|     let _4: fn (i32, i64) i64;
//|     let _5: i32;
//|     let _6: bool;
//|     let _7: u64;
//|     let _8: char*;
//|     let _9: void;
//|     let _10: f64;
//|
//|     bb0: {
//|         _0 = call fn main.add(copy _1, copy _2) #2:1 s1;
//|         _8 = const bytes "%d %f\x0A" #3:1 s2;
//|         _5 = call extern puts(copy _8) #3:1 s2;
//|         _5 = call extern printf(copy _8) #4:1 s3;
//|         _10 = const f64 0x3FF0000000000000 #5:1 s4;
//|         _5 = call extern printf(copy _8, copy _1, copy _10, copy _8) #5:1 s4;
//|         _4 = const fn main.add #6:1 s5;
//|         _0 = call copy _4(copy _1, copy _2) #6:1 s5;
//|         _9 = call fn main.show(copy _3) #7:1 s6;
//|         _6 = const false #8:1 s7;
//|         _7 = copy _3.1 #8:1 s7;
//|         check(copy _6, overflow) -> bb1 #8:1 s7;
//|     }
//|     bb1: {
//|         check(copy _6, shift, copy _2, const bytes "i32") -> bb2 #9:1 s8;
//|     }
//|     bb2: {
//|         check(copy _6, div_zero) -> bb3 #10:1 s9;
//|     }
//|     bb3: {
//|         check(copy _6, div_overflow) -> bb4 #11:1 s10;
//|     }
//|     bb4: {
//|         check(copy _6, bounds, copy _2, copy _7) -> bb5 #12:1 s11;
//|     }
//|     bb5: {
//|         check(copy _6, span, copy _2, copy _2, copy _7) -> bb6 #13:1 s12;
//|     }
//|     bb6: {
//|         check(copy _6, alloc_count, copy _2) -> bb7 #14:1 s13;
//|     }
//|     bb7: {
//|         check(copy _6, enum, copy _2, const bytes "color") -> bb8 #15:1 s14;
//|     }
//|     bb8: {
//|         check(copy _6, user, const bytes "x > 0") -> bb9 #16:1 s15;
//|     }
//|     bb9: {
//|         switch(copy _6) -> [true: bb10, otherwise: bb11] #17:1 s16;
//|     }
//|     bb10: {
//|         fail(panic, copy _3.0, copy _3.1) #18:1 s17;
//|     }
//|     bb11: {
//|         _9 = call fn std.rt.fail_overflow(copy _8, const u32 19, const u32 1) #19:1 s18;
//|         trap #19:1 s18;
//|     }
//| }
// V7 on the edges: a fort call, a fixed and a variadic extern call, a call through a value, an
// aggregate argument from a place, and a check of each kind with the operands that the runtime
// entry of std/rt.ft takes. The runtime entry `std.rt.fail_overflow` is a noreturn function, so a
// call of it ends its block in trap (V8). A call of a `void` function writes a `void` temporary,
// because a statement of FIR that calls always writes a place.
fn main.f(_1 (x): i32, _2 (w): i64, _3 (s): string own) -> i64 {
    let _4: fn (i32, i64) i64;
    let _5: i32;
    let _6: bool;
    let _7: u64;
    let _8: char*;
    let _9: void;
    let _10: f64;

    bb0: {
        _0 = call fn main.add(copy _1, copy _2) #2:1 s1;
        _8 = const bytes "%d %f\n" #3:1 s2;
        _5 = call extern puts(copy _8) #3:1 s2;
        _5 = call extern printf(copy _8) #4:1 s3;
        _10 = const f64 0x3FF0000000000000 #5:1 s4;
        _5 = call extern printf(copy _8, copy _1, copy _10, copy _8) #5:1 s4;
        _4 = const fn main.add #6:1 s5;
        _0 = call copy _4(copy _1, copy _2) #6:1 s5;
        _9 = call fn main.show(copy _3) #7:1 s6;
        _6 = const false #8:1 s7;
        _7 = copy _3.1 #8:1 s7;
        check(copy _6, overflow) -> bb1 #8:1 s7;
    }
    bb1: {
        check(copy _6, shift, copy _2, const bytes "i32") -> bb2 #9:1 s8;
    }
    bb2: {
        check(copy _6, div_zero) -> bb3 #10:1 s9;
    }
    bb3: {
        check(copy _6, div_overflow) -> bb4 #11:1 s10;
    }
    bb4: {
        check(copy _6, bounds, copy _2, copy _7) -> bb5 #12:1 s11;
    }
    bb5: {
        check(copy _6, span, copy _2, copy _2, copy _7) -> bb6 #13:1 s12;
    }
    bb6: {
        check(copy _6, alloc_count, copy _2) -> bb7 #14:1 s13;
    }
    bb7: {
        check(copy _6, enum, copy _2, const bytes "color") -> bb8 #15:1 s14;
    }
    bb8: {
        check(copy _6, user, const bytes "x > 0") -> bb9 #16:1 s15;
    }
    bb9: {
        switch(copy _6) -> [true: bb10, otherwise: bb11] #17:1 s16;
    }
    bb10: {
        fail(panic, copy _3.0, copy _3.1) #18:1 s17;
    }
    bb11: {
        _9 = call fn std.rt.fail_overflow(copy _8, const u32 19, const u32 1) #19:1 s18;
        trap #19:1 s18;
    }
}
