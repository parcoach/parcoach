; RUN: %parcoach -S %s -o - | %filecheck %s --check-prefix=CHECK-ASM
; RUN: touch %t.ll
; RUN: chmod 000 %t.ll
; RUN: %not %parcoach -S %s -o %t.ll 2>&1 | %filecheck %s --check-prefix=CHECK-DENIED
; RUN: rm %t.ll
; CHECK-ASM: define i32 @foo
; CHECK-DENIED: Permission denied
define i32 @foo(i32 %a) {
  ret i32 %a
}
