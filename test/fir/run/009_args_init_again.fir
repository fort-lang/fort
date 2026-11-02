//! pass: verify
//! pass: run
//! expect:
//| stdout "1 0 0\x0A"
//| exit 0
// The pass `run` starts the program as the `main` that a build emits starts it: `std.rt.args_init`
// with the path of the test, then `std.rt.args`, then `main`. Fort source cannot name
// `std.rt.args_init` (D6.9), so this FIR text calls it twice more. The second call frees the span
// of the first, and its count of zero builds no span, so `args` gives an empty span. The third
// call, with a negative count, frees nothing and builds no span. A runtime without the first
// `del` of args_init keeps the span of the first call and prints `1 1 1` (measured).
fn main.main() -> i32 {
    let _1 (before): string@;
    let _2 (after): string@;
    let _3 (again): string@;
    let _4: void;
    let _5: char* mut*;

    bb0: {
        _1 = call fn std.rt.args() #2:1;
        _5 = const null char* mut* #3:1;
        _4 = call fn std.rt.args_init(const i32 0, copy _5) #3:1;
        _2 = call fn std.rt.args() #4:1;
        _4 = call fn std.rt.args_init(const i32 -1, copy _5) #5:1;
        _3 = call fn std.rt.args() #6:1;
        _4 = call fn std.rt.print_u64(const i32 1, copy _1.1) #7:1;
        _4 = call fn std.rt.print_char(const i32 1, const char 32) #7:1;
        _4 = call fn std.rt.print_u64(const i32 1, copy _2.1) #7:1;
        _4 = call fn std.rt.print_char(const i32 1, const char 32) #7:1;
        _4 = call fn std.rt.print_u64(const i32 1, copy _3.1) #7:1;
        _4 = call fn std.rt.print_char(const i32 1, const char 10) #7:1;
        _0 = const i32 0 #8:1;
        return #8:1;
    }
}
