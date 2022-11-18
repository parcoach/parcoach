; RUN: %parcoach -disable-output -dump-ssa %s
; RUN: %filecheck %s < add-assa.ll
; CHECK: 01 = X(00
; CHECK: mu(01,
define dso_local i32 @add(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  store i32 %1, ptr %4, align 4
  %5 = load i32, ptr %3, align 4
  %6 = load i32, ptr %4, align 4
  %7 = add nsw i32 %5, %6
  ret i32 %7
}
