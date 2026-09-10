target triple = "x86_64-unknown-linux-gnu"

%fort.slice = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local i32 @"main.main"() #0 {
entry:
  call void @fort_rt_print_str(i32 1, ptr @.str.0, i64 13)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  ret i32 0
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"main.main"()
  ret i32 %t0
}

@.str.0 = private unnamed_addr constant [14 x i8] c"hello, world!\00", align 1

declare void @fort_rt_print_char(i32, i8 zeroext)
declare void @fort_rt_print_str(i32, ptr, i64)

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
