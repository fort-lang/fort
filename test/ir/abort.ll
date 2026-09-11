target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local i32 @"main.main"() #0 {
entry:
  %a.0 = alloca [3 x i32], align 4
  %i.1 = alloca i64, align 8
  call void @fort_rt_print_str(i32 1, ptr @.str.0, i64 6)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
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
  call void @fort_rt_fail_bounds(i64 %t0, i64 3, ptr @.file.0, i32 12, i32 14)
  unreachable
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"main.main"()
  ret i32 %t0
}

@.file.0 = private unnamed_addr constant [9 x i8] c"abort.ft\00", align 1
@.str.0 = private unnamed_addr constant [7 x i8] c"before\00", align 1

declare void @fort_rt_fail_bounds(i64, i64, ptr, i32, i32) #2
declare void @fort_rt_print_char(i32, i8 zeroext)
declare void @fort_rt_print_str(i32, ptr, i64)

declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #6

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #2 = { cold noreturn nounwind }
attributes #6 = { nocallback nofree nounwind willreturn memory(argmem: write) }
