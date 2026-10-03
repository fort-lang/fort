//! pass: verify
//! expect:
//| fn main.held_index(_1 (items): u8 mut* own mut@, _2 (index): u64) -> void {
//|     let _0: void;
//|     let _3: u64;
//|     let _4: u8 mut* own;
//|
//|     bb0: {
//|         _3 = copy _2 #1:1 s0;
//|         _2 = const u64 1 #2:1 s1;
//|         _4 = move _1[_3] #3:1 s2;
//|         del(move _4) #4:1 s3;
//|         return #5:1 s4;
//|     }
//| }
//|
//| fn main.aliases(_1 (items): u8 mut* own mut@, _2 (left): u64, _3 (right): u64) -> void {
//|     let _0: void;
//|     let _4: u8 mut*;
//|     let _5: u8 mut*;
//|
//|     bb0: {
//|         _4 = copy _1[_2] #1:1 s0;
//|         _5 = copy _1[_3] #2:1 s1;
//|         del(move _1[_2]) #3:1 s2;
//|         (*_5) = const u8 1 #4:1 s3;
//|         return #5:1 s4;
//|     }
//| }
//|
//| fn main.conditional(_1 (items): u8 mut* own mut@, _2 (index): u64) -> void {
//|     let _0: void;
//|     let _3: u8 mut* own;
//|
//|     bb0: {
//|         _3 = move _1[_2] #1:1 s0;
//|         del(move _3) #2:1 s1;
//|         return #3:1 s2;
//|     }
//| }
// The FIR verifier checks shape. Dynamic analysis must preserve possible source aliasing.
fn main.held_index(_1 (items): u8 mut* own mut@, _2 (index): u64) -> void {
    let _3: u64;
    let _4: u8 mut* own;

    bb0: {
        _3 = copy _2 #1:1 s0;
        _2 = const u64 1 #2:1 s1;
        _4 = move _1[_3] #3:1 s2;
        del(move _4) #4:1 s3;
        return #5:1 s4;
    }
}

fn main.aliases(_1 (items): u8 mut* own mut@, _2 (left): u64, _3 (right): u64) -> void {
    let _4: u8 mut*;
    let _5: u8 mut*;

    bb0: {
        _4 = copy _1[_2] #1:1 s0;
        _5 = copy _1[_3] #2:1 s1;
        del(move _1[_2]) #3:1 s2;
        (*_5) = const u8 1 #4:1 s3;
        return #5:1 s4;
    }
}

fn main.conditional(_1 (items): u8 mut* own mut@, _2 (index): u64) -> void {
    let _3: u8 mut* own;

    bb0: {
        _3 = move _1[_2] #1:1 s0;
        del(move _3) #2:1 s1;
        return #3:1 s2;
    }
}
