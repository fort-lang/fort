//! pass: none
//! prelude:
//| struct pair {
//|     i32 a;
//|     u64 b;
//| }
//! expect:
//| fn main.swap(_1 (p): pair) -> pair {
//|     let _0: pair;
//|     let _2: u64 #3:1;
//|
//|     bb0: {
//|         _2 = copy _1.1 #4:2 s0;
//|         _0 = aggregate pair(const i32 -5, copy _2) #5:3 exit 6:4 s0;
//|         return;
//|     }
//| }
// The printer writes a text in its own form: `let _0` and the blank line before the first
// block, one space between the tokens, `s0` for a location with no index, and no comment.
// `--bless` wrote the expectation.
fn main.swap(_1 (p):pair)->pair{
    let _2:u64 #3:1; // a temporary
    bb0:{
        _2=copy _1.1  #4:2;
        _0=aggregate pair(const i32 -5,copy _2)#5:3 exit 6:4;
        return;
    }
}
