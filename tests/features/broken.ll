; RUN: %not %parcoach -disable-output %s 2>&1 | %filecheck %s

; CHECK: error: input module is broken!
define i32 @foo(i32 %a) {
  %toto = add i32 %a, %titi
  %titi = add i32 %a, 3
  ret i32 %titi
}
