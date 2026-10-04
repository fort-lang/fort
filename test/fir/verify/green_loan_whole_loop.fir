//! pass: verify
//! expect:
//| fn main.f(_1: i32@) -> void {
//|     let _0: void;
//|     let _2: i32@;
//|     let _3: u64;
//|     let _4: u64;
//|     let _5: bool;
//|     let _6: i32;
//|
//|     bb0: {
//|         _2 = copy _1;
//|         loan_begin 1 _1 _2;
//|         _3 = const u64 0;
//|         goto bb1;
//|     }
//|     bb1: {
//|         _4 = copy _2.1;
//|         _5 = lt(copy _3, copy _4);
//|         switch(copy _5) -> [true: bb2, otherwise: bb4];
//|     }
//|     bb2: {
//|         _6 = copy _2[_3];
//|         goto bb3;
//|     }
//|     bb3: {
//|         _3 = add(copy _3, const u64 1);
//|         goto bb1;
//|     }
//|     bb4: {
//|         loan_end 1;
//|         return;
//|     }
//| }
fn main.f(_1: i32@) -> void {
    let _2: i32@;
    let _3: u64;
    let _4: u64;
    let _5: bool;
    let _6: i32;
    bb0: { _2 = copy _1; loan_begin 1 _1 _2; _3 = const u64 0; goto bb1; }
    bb1: { _4 = copy _2.1; _5 = lt(copy _3, copy _4);
        switch(copy _5) -> [true: bb2, otherwise: bb4]; }
    bb2: { _6 = copy _2[_3]; goto bb3; }
    bb3: { _3 = add(copy _3, const u64 1); goto bb1; }
    bb4: { loan_end 1; return; }
}
