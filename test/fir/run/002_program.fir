//! pass: run
//! prelude:
//| fn main(string@ args) i32 {
//|     println("args ", args.len);
//|     return 300;
//| }
//! expect:
//| stdout "args 1\x0A"
//| exit 44
// With no function, the pass runs the program of the prelude as the `main` that a build emits
// starts it: its one argument is the path of the test, and its status is 300 & 0xFF.
