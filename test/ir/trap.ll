target triple = "x86_64-unknown-linux-gnu"

define dso_local i32 @"my:app.main"() {
entry:
  %written = call i64 @write(i32 1, ptr @.line, i64 6)
  ret i32 0
}

define dso_local i32 @main(i32 %argc, ptr %argv) {
entry:
  %trap = icmp eq i32 %argc, 2
  br i1 %trap, label %trap_path, label %run_path

trap_path:
  call void @llvm.trap()
  unreachable

run_path:
  %status = call i32 @"my:app.main"()
  ret i32 %status
}

@.line = private unnamed_addr constant [6 x i8] c"colon\0A", align 1

declare i64 @write(i32, ptr, i64)
declare void @llvm.trap()
