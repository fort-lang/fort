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

define dso_local i32 @"my:app.main"() #0 {
entry:
  call void @"std.rt.print_str"(i32 1, ptr @.str.0, i64 5)
  call void @"std.rt.print_char"(i32 1, i8 zeroext 10)
  ret i32 0
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"my:app.main"()
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

@.str.0 = private unnamed_addr constant [6 x i8] c"colon\00", align 1

declare i64 @write(i32, ptr, i64, ...)

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
attributes #3 = { nobuiltin }
