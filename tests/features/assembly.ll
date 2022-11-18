; RUN: %parcoach -S %s | %filecheck %s --check-prefix=CHECK-ASM
; RUN: %parcoach -disable-output -o /dev/null %s 2>&1 | %filecheck %s --check-prefix=CHECK-WARNING
; RUN: touch %t.ll
; RUN: chmod 000 %t.ll
; RUN: %not %parcoach -S %s -o %t.ll 2>&1 | %filecheck %s --check-prefix=CHECK-DENIED
; RUN: rm %t.ll
; CHECK-ASM: define i32 @foo
; CHECK-WARNING: option is ignored
; CHECK-DENIED: Permission denied
define i32 @foo(i32 %a) {
  ret i32 %a
}
