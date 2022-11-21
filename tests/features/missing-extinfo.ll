; RUN: %not %parcoach -S %s -disable-output 2>&1 | %filecheck %s
; CHECK: missing info for external function some_external_function
; CHECK: you have to fill the funcModPairs
declare i32 @some_external_function(i32 %i);

define i32 @foo(i32 %a) {
  %1 = call i32 @some_external_function(i32 %a)
  ret i32 %1
}
