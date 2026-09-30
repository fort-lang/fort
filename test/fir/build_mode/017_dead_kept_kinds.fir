//! pass: build-mode
//! pass: verify
//! prelude:
//| struct pair {
//|     i32 a;
//|     i32 b;
//| }
//| fn five() i32 {
//|     return 5;
//| }
//| fn kept(u8 mut* own o, u8@ s) void {
//| }
//! expect:
//| fn main.kept(_1 (o): u8 mut* own, _2 (s): u8@) -> void {
//|     let _0: void;
//|     let _3: i32;
//|     let _4: u8 mut* own;
//|     let _5 (x): i32;
//|     let _6: pair;
//|     let _7: u8 mut* own;
//|     let _8: pair;
//|     let _9: u8@;
//|     let _10: u8 mut* own;
//|     let _11: i32;
//|
//|     bb0: {
//|         _3 = call fn main.five();
//|         _4 = alloc<u8>(const u64 1);
//|         _5 = const i32 1;
//|         _6.0 = const i32 2;
//|         _7 = move _1;
//|         _8 = aggregate pair(const i32 3, const i32 4);
//|         _9 = slice(_2, const u64 0, const u64 1);
//|         _10 = const null u8 mut* own;
//|         del(move _10);
//|         return;
//|     }
//| }
// The pass removes an assignment only when its place is a whole temporary that nothing reads and
// its rvalue is an operand, an arithmetic, overflow, bitwise, shift, comparison or `cast` rvalue,
// or `addr`, with no `move` operand. Nothing reads `_3`, `_4`, `_5`, `_6`, `_7`, `_8` or `_9`, and
// each assignment stays: a `call`, an `alloc`, a named local, a place with a projection, a `move`
// operand, an `aggregate` and a `slice`. A `del` is no assignment, and it reads `_10`. Only `_11 =
// add(copy _5, const i32 6)` goes. The mode is the default mode, which removes no check.
fn main.kept(_1 (o): u8 mut* own, _2 (s): u8@) -> void {
    let _3: i32;
    let _4: u8 mut* own;
    let _5 (x): i32;
    let _6: pair;
    let _7: u8 mut* own;
    let _8: pair;
    let _9: u8@;
    let _10: u8 mut* own;
    let _11: i32;

    bb0: {
        _3 = call fn main.five();
        _4 = alloc<u8>(const u64 1);
        _5 = const i32 1;
        _6.0 = const i32 2;
        _7 = move _1;
        _8 = aggregate pair(const i32 3, const i32 4);
        _9 = slice(_2, const u64 0, const u64 1);
        _10 = const null u8 mut* own;
        _11 = add(copy _5, const i32 6);
        del(move _10);
        return;
    }
}
