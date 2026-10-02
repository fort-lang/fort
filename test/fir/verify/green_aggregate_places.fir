//! pass: verify
//! prelude:
//| struct pair {
//|     u8 mut* own left;
//|     u8 mut* own right;
//| }
//! expect:
//| fn main.fields() -> void {
//|     let _0: void;
//|     let _1 (p): pair;
//|     let _2: u8 mut* own;
//|     let _3: u8 mut* own;
//|     let _4: u8 mut* own;
//|
//|     bb0: {
//|         _2 = alloc<u8>(const u64 1) #1:1 s0;
//|         _3 = alloc<u8>(const u64 1) #2:1 s1;
//|         _1 = aggregate pair(move _2, move _3) #3:1 s2;
//|         _4 = move _1.0 #4:1 s3;
//|         del(move _4) #5:1 s4;
//|         del(move _1.1) #6:1 s5;
//|         return #7:1 s6;
//|     }
//| }
//|
//| fn main.elements() -> void {
//|     let _0: void;
//|     let _1 (src): u8 mut* own[2];
//|     let _2 (dst): u8 mut* own[2];
//|     let _3: u8 mut* own;
//|     let _4: u8 mut* own;
//|     let _5: u64;
//|     let _6: u64;
//|
//|     bb0: {
//|         _3 = alloc<u8>(const u64 1) #1:1 s0;
//|         _4 = alloc<u8>(const u64 1) #2:1 s1;
//|         _1 = aggregate u8 mut* own[2](move _3, move _4) #3:1 s2;
//|         _2 = move _1 #4:1 s3;
//|         _5 = const u64 0 #5:1 s4;
//|         _6 = const u64 1 #6:1 s5;
//|         del(move _2[_5]) #7:1 s6;
//|         del(move _2[_6]) #8:1 s7;
//|         return #9:1 s8;
//|     }
//| }
// The verifier checks the FIR shape. The ownership module checks leaf obligations.
fn main.fields() -> void {
    let _1 (p): pair;
    let _2: u8 mut* own;
    let _3: u8 mut* own;
    let _4: u8 mut* own;

    bb0: {
        _2 = alloc<u8>(const u64 1) #1:1 s0;
        _3 = alloc<u8>(const u64 1) #2:1 s1;
        _1 = aggregate pair(move _2, move _3) #3:1 s2;
        _4 = move _1.0 #4:1 s3;
        del(move _4) #5:1 s4;
        del(move _1.1) #6:1 s5;
        return #7:1 s6;
    }
}

fn main.elements() -> void {
    let _1 (src): u8 mut* own[2];
    let _2 (dst): u8 mut* own[2];
    let _3: u8 mut* own;
    let _4: u8 mut* own;
    let _5: u64;
    let _6: u64;

    bb0: {
        _3 = alloc<u8>(const u64 1) #1:1 s0;
        _4 = alloc<u8>(const u64 1) #2:1 s1;
        _1 = aggregate u8 mut* own[2](move _3, move _4) #3:1 s2;
        _2 = move _1 #4:1 s3;
        _5 = const u64 0 #5:1 s4;
        _6 = const u64 1 #6:1 s5;
        del(move _2[_5]) #7:1 s6;
        del(move _2[_6]) #8:1 s7;
        return #9:1 s8;
    }
}
