//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//! expect:
//| fn main.f() -> holder {
//|     let _0: holder;
//|     let _1: holder #2:17;
//|
//|     bb0: {
//|         _1 = const zero holder #2:17 s1;
//|         _0 = cast<holder>(move _1) #2:12 s1;
//|         return #2:5 s1;
//|     }
//| }
// V6 and V13 on `return cast(holder{}, holder);` as spec/fir.md 9.4 lowers it. A cast never adds
// `own` (D3.14), so the constant `holder{}` goes into the temporary `_1` first, and the cast
// takes `move _1`, which carries the owner of the struct into `_0`.
fn main.f() -> holder {
    let _1: holder #2:17;

    bb0: {
        _1 = const zero holder #2:17 s1;
        _0 = cast<holder>(move _1) #2:12 s1;
        return #2:5 s1;
    }
}
