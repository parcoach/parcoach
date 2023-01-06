; RUN: %parcoach -S %s -disable-output 2>&1 | %filecheck %s
; We simply check that parcoach runs fine even if there are some missing extinfo
; CHECK: Parcoach is missing some information about external functions: some_external_function, some_other_external_function
declare i32 @some_external_function(i32 %i);
declare i32 @some_other_external_function(i32 %i);

define i32 @foo(i32 %a) {
  %1 = call i32 @some_external_function(i32 %a)
  ret i32 %1
}
