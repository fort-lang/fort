//! pass: ownership-local
//! prelude:
//| i32 mut* own mut KEEP = null;
//! expect:
//| fn main.keep() -> void {
//|     let _0: void;
//|     let _1: i32 mut* own mut* #26:5;
//|     let _2: i32 mut* own #27:5;
//|
//|     bb0: {
//|         _1 = addr mut(global main.KEEP) #30:9 s1;
//|         _2 = alloc<i32>(const u64 1) #31:9 s1;
//|         check(overwrite: (*_1)) -> bb1 #32:9 s1;
//|     }
//|     bb1: {
//|         (*_1) = move _2 #35:9 s1;
//|         return #36:9 s2;
//|     }
//| }
// The pass gives no closure proof. The function stores a new allocation in an owning global
// and returns, and the local analysis has no fact about globals, so the pass writes the module
// and exits 0. Only --ownership-check on a program holds the obligation that the global is
// empty at normal exit (D17.19); this pass does not.
fn main.keep() -> void {
    let _0: void;
    let _1: i32 mut* own mut* #26:5;
    let _2: i32 mut* own #27:5;

    bb0: {
        _1 = addr mut(global main.KEEP) #30:9 s1;
        _2 = alloc<i32>(const u64 1) #31:9 s1;
        check(overwrite: (*_1)) -> bb1 #32:9 s1;
    }
    bb1: {
        (*_1) = move _2 #35:9 s1;
        return #36:9 s2;
    }
}
