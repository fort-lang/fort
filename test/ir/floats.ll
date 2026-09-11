target triple = "x86_64-unknown-linux-gnu"

%fort.span = type { ptr, i64 }
%fort.enum_member = type { i32, ptr }

define dso_local i32 @"main.main"() #0 {
entry:
  call void @fort_rt_print_f64(i32 1, double 0x3FB999999999999A)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @fort_rt_print_f64(i32 1, double 0x4376345785D8A000)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @fort_rt_print_f64(i32 1, double 0x8000000000000000)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @fort_rt_print_f64(i32 1, double 0x7FF0000000000000)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @fort_rt_print_f64(i32 1, double 0x7FF8000000000000)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @fort_rt_print_f32(i32 1, float 0x3FB99999A0000000)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  call void @fort_rt_print_f32(i32 1, float 0x4170000000000000)
  call void @fort_rt_print_char(i32 1, i8 zeroext 10)
  ret i32 0
}

define dso_local i32 @fort_entry(ptr %args.in) #0 {
entry:
  %t0 = call i32 @"main.main"()
  ret i32 %t0
}

declare void @fort_rt_print_f32(i32, float)
declare void @fort_rt_print_f64(i32, double)
declare void @fort_rt_print_char(i32, i8 zeroext)

attributes #0 = { nounwind "frame-pointer"="all" "probe-stack"="inline-asm" }
