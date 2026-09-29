//! pass: verify
//! prelude:
//| struct buf {
//|     u8 mut@ own data;
//| }
//| fn note() void {
//| }
//| fn get(buf* b) u8@ {
//|     defer note();
//|     return b->data;
//| }
//! expect:
//| fn main.get(_1 (b): buf*) -> u8@ {
//|     let _0: u8@;
//|     let _2: u8@ #3:12;
//|     let _3: void;
//|
//|     bb0: {
//|         _2 = copy (*_1).0 #3:12 s2;
//|         _3 = call fn main.note() #2:11 exit 3:5 s3;
//|         _0 = copy _2 #3:5 s2;
//|         return #3:5 s2;
//|     }
//| }
// `return b->data` of an owning span field into a `_0` that does not own, under an open defer
// (spec/fir.md 9.6). The temporary `_2` takes the type of `_0`, not the lent type of the field, and
// `_0 = copy _2` follows the deferred call, because `_0` does not own.
fn main.get(_1 (b): buf*) -> u8@ {
    let _2: u8@ #3:12;
    let _3: void;

    bb0: {
        _2 = copy (*_1).0 #3:12 s2;
        _3 = call fn main.note() #2:11 exit 3:5 s3;
        _0 = copy _2 #3:5 s2;
        return #3:5 s2;
    }
}
