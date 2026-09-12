target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local void @"std.rt.print_str"(i32 %fd, ptr %ptr, i64 %len) #0 {
entry:
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 %fd, ptr %ptr, i64 %len) #3
  ret void
}

define dso_local void @"std.rt.print_char"(i32 %fd, i8 zeroext %c) #0 {
entry:
  %byte.0 = alloca i8, align 1
  store i8 %c, ptr %byte.0, align 1
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 %fd, ptr %byte.0, i64 1) #3
  ret void
}

define dso_local void @"std.rt.args_init"(i32 %argc, ptr %argv) #0 {
entry:
  ret void
}

define dso_local void @"std.rt.args"(ptr sret(%fort.span) %ret.sret) #0 {
entry:
  %t0 = getelementptr inbounds %fort.span, ptr %ret.sret, i32 0, i32 0
  store ptr null, ptr %t0, align 8
  %t1 = getelementptr inbounds %fort.span, ptr %ret.sret, i32 0, i32 1
  store i64 0, ptr %t1, align 8
  ret void
}

define dso_local void @"std.rt.flush_all"() #0 {
entry:
  ret void
}

define dso_local void @"std.rt.fail_bounds"(i64 %i, i64 %n, ptr %f, i32 %l, i32 %c) #8 {
entry:
  %t0 = call i64 (i32, ptr, i64, ...) @write(i32 2, ptr @.str.1, i64 65) #3
  call void (...) @abort() #3
  call void @llvm.trap()
  unreachable
}

define dso_local i32 @"abort.main"() #0 {
entry:
  %a.0 = alloca [3 x i32], align 4
  %i.1 = alloca i64, align 8
  call void @"std.rt.print_str"(i32 1, ptr @.str.0, i64 6)
  call void @"std.rt.print_char"(i32 1, i8 zeroext 10)
  call void @llvm.memset.p0.i64(ptr align 4 %a.0, i8 0, i64 12, i1 false)
  store i64 5, ptr %i.1, align 8
  %t0 = load i64, ptr %i.1, align 8
  %t1 = icmp uge i64 %t0, 3
  br i1 %t1, label %L1, label %L0

L0:
  %t2 = getelementptr inbounds [3 x i32], ptr %a.0, i64 0, i64 %t0
  %t3 = load i32, ptr %t2, align 4
  ret i32 %t3

L1:
  call void @"std.rt.fail_bounds"(i64 %t0, i64 3, ptr @.file.0, i32 12, i32 13)
  unreachable
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"abort.main"()
  ret i32 %t0
}

define dso_local i32 @main(i32 %argc, ptr %argv) #0 {
entry:
  %args = alloca %fort.span, align 8
  call void @"std.rt.args_init"(i32 %argc, ptr %argv)
  call void @"std.rt.args"(ptr %args)
  %t0 = call i32 @fort_entry(ptr %args)
  call void @"std.rt.flush_all"()
  %t1 = and i32 %t0, 255
  ret i32 %t1
}

@.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
@.str.0 = private unnamed_addr constant [7 x i8] c"before\00", align 1
@.str.1 = private unnamed_addr constant [66 x i8] c"abort.ft:12:13: runtime error: index 5 out of range for length 3\0A\00", align 1

declare void @abort(...)
declare i64 @write(i32, ptr, i64, ...)

declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6
declare void @llvm.trap() #7

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #3 = { nobuiltin }
attributes #6 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #7 = { cold noreturn nounwind memory(inaccessiblemem: write) }
attributes #8 = { cold noreturn nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
