//! pass: verify
//! prelude:
//| struct holder {
//|     u8 mut* own p;
//|     u64 n;
//| }
//| fn make() holder mut* own {
//|     holder mut* own h = new(holder);
//|     h->p = new(u8);
//|     return h;
//| }
//! panic: fir.verify: V16 a return with a live scratch temporary: main.f bb0 statement 3 (s4)
// V16: a `move` of a place through `_1` takes a member of the allocation and leaves `_1` live.
// Only a `move` of the whole local empties it.
fn main.f() -> void {
    let _1: holder mut* own;
    let _2: u8 mut* own;
    bb0: {
        _1 = call fn main.make() #2:5 s1;
        _2 = move (*_1).0 #3:5 s2;
        del(move _2) #3:5 s3;
        return #4:5 s4;
    }
}
