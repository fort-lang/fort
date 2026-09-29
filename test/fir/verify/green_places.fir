//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     u64 b;
//| }
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| enum color {
//|     red,
//|     green,
//| }
//| i32 LIMIT = 7;
//| pair mut origin = pair{1, 2};
//! expect:
//| fn main.f(_1 (p): pair*, _2 (s): string own, _3 (v): i32@) -> u64 {
//|     let _0: u64;
//|     let _4 (a): i32[4];
//|     let _5 (h): holder;
//|     let _6: u64;
//|     let _7: i32;
//|     let _8: char;
//|     let _9: u8;
//|     let _10: i8;
//|     let _11: color;
//|     let _12: char*;
//|
//|     bb0: {
//|         _7 = copy (*_1).0 #2:1 s1;
//|         _0 = copy (*_1).1 #3:1 s2;
//|         _12 = copy _2.0 #4:1 s3;
//|         _6 = copy _2.1 #5:1 s4;
//|         _8 = copy _2[_6] #6:1 s5;
//|         _0 = copy _3.1 #7:1 s6;
//|         _7 = copy _3[_6] #8:1 s7;
//|         _7 = copy _4[_6] #9:1 s8;
//|         _9 = copy (*_5.0) #10:1 s9;
//|         _0 = copy _5.1 #11:1 s10;
//|         _7 = copy global main.LIMIT #12:1 s11;
//|         _0 = copy global main.origin.1 #13:1 s12;
//|         switch(copy _9) -> [u8 0: bb1, u8 255: bb1, otherwise: bb1] #14:1 s13;
//|     }
//|     bb1: {
//|         switch(copy _10) -> [i8 -128: bb2, i8 127: bb2, otherwise: bb2] #15:1 s14;
//|     }
//|     bb2: {
//|         switch(copy _8) -> [char 0: bb3, char 255: bb3, otherwise: bb3] #16:1 s15;
//|     }
//|     bb3: {
//|         switch(copy _11) -> [color 0: bb4, color 1: bb4, otherwise: bb4] #17:1 s16;
//|     }
//|     bb4: {
//|         return #18:1 s17;
//|     }
//| }
// V3 and V4 on the edges: each projection on each type it applies to, the last field of a
// struct, a span and a string, a global and a field of a global, the u64 index, and the switch
// arms at the ends of the ranges of u8, i8, char and an enum.
fn main.f(_1 (p): pair*, _2 (s): string own, _3 (v): i32@) -> u64 {
    let _4 (a): i32[4];
    let _5 (h): holder;
    let _6: u64;
    let _7: i32;
    let _8: char;
    let _9: u8;
    let _10: i8;
    let _11: color;
    let _12: char*;

    bb0: {
        _7 = copy (*_1).0 #2:1 s1;
        _0 = copy (*_1).1 #3:1 s2;
        _12 = copy _2.0 #4:1 s3;
        _6 = copy _2.1 #5:1 s4;
        _8 = copy _2[_6] #6:1 s5;
        _0 = copy _3.1 #7:1 s6;
        _7 = copy _3[_6] #8:1 s7;
        _7 = copy _4[_6] #9:1 s8;
        _9 = copy (*_5.0) #10:1 s9;
        _0 = copy _5.1 #11:1 s10;
        _7 = copy global main.LIMIT #12:1 s11;
        _0 = copy global main.origin.1 #13:1 s12;
        switch(copy _9) -> [u8 0: bb1, u8 255: bb1, otherwise: bb1] #14:1 s13;
    }
    bb1: {
        switch(copy _10) -> [i8 -128: bb2, i8 127: bb2, otherwise: bb2] #15:1 s14;
    }
    bb2: {
        switch(copy _8) -> [char 0: bb3, char 255: bb3, otherwise: bb3] #16:1 s15;
    }
    bb3: {
        switch(copy _11) -> [color 0: bb4, color 1: bb4, otherwise: bb4] #17:1 s16;
    }
    bb4: {
        return #18:1 s17;
    }
}
