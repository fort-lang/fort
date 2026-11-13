//! pass: verify
//! prelude:
//| struct holed {
//|     u8 a;
//|     u64 b;
//| }
//| struct outer {
//|     holed h;
//|     u8 t;
//| }
//| struct full {
//|     u64 a;
//|     u64 b;
//| }
//| holed mut kept = holed{1, 2};
//! expect:
//| fn main.places(_1 (p): holed mut*, _2 (b): u64) -> void {
//|     let _0: void;
//|     let _3 (h): holed #2:11;
//|     let _4: outer;
//|
//|     bb0: {
//|         live(_3) #2:11 s1;
//|         _3 = aggregate holed(const u8 1, copy _2) #2:15 s1;
//|         (*_1) = aggregate holed(const u8 2, copy _2) #3:10 s2;
//|         global main.kept = aggregate holed(const u8 3, copy _2) #4:12 s3;
//|         _4 = const zero outer #5:5 s4;
//|         _4.0 = aggregate holed(const u8 4, copy _2) #5:5 s4;
//|         dead(_3) #6:1 s0;
//|         return #6:1 s0;
//|     }
//| }
//|
//| fn main.zeroed_result(_1 (b): u64) -> holed {
//|     let _0: holed;
//|     let _2: holed;
//|
//|     bb0: {
//|         _2 = aggregate zeroed holed(const u8 1, copy _1) #2:12 s1;
//|         _0 = aggregate zeroed holed(const u8 2, copy _1) #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
//|
//| fn main.no_padding(_1 (b): u64) -> full {
//|     let _0: full;
//|     let _2: full;
//|     let _3: u8@;
//|
//|     bb0: {
//|         _2 = aggregate full(const u64 1, copy _1) #2:12 s1;
//|         _3 = aggregate u8@(const null u8*, const u64 0) #2:12 s1;
//|         _0 = aggregate full(const u64 2, copy _1) #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// The edges of the padding clause of V4. `aggregate T(a, ...)` with no `zeroed` is legal into a
// named local, whose `live` gives each byte the value 0 (D10.1), through a pointer, into a global
// and into a part of a temporary that a whole store wrote. `aggregate zeroed` is legal into a
// whole temporary and into the whole `_0`. A type with no padding, a span header among them, needs
// no mark in any place.
fn main.places(_1 (p): holed mut*, _2 (b): u64) -> void {
    let _3 (h): holed #2:11;
    let _4: outer;

    bb0: {
        live(_3) #2:11 s1;
        _3 = aggregate holed(const u8 1, copy _2) #2:15 s1;
        (*_1) = aggregate holed(const u8 2, copy _2) #3:10 s2;
        global main.kept = aggregate holed(const u8 3, copy _2) #4:12 s3;
        _4 = const zero outer #5:5 s4;
        _4.0 = aggregate holed(const u8 4, copy _2) #5:5 s4;
        dead(_3) #6:1 s0;
        return #6:1 s0;
    }
}

fn main.zeroed_result(_1 (b): u64) -> holed {
    let _2: holed;

    bb0: {
        _2 = aggregate zeroed holed(const u8 1, copy _1) #2:12 s1;
        _0 = aggregate zeroed holed(const u8 2, copy _1) #2:12 s1;
        return #2:5 s1;
    }
}

fn main.no_padding(_1 (b): u64) -> full {
    let _2: full;
    let _3: u8@;

    bb0: {
        _2 = aggregate full(const u64 1, copy _1) #2:12 s1;
        _3 = aggregate u8@(const null u8*, const u64 0) #2:12 s1;
        _0 = aggregate full(const u64 2, copy _1) #2:12 s1;
        return #2:5 s1;
    }
}
