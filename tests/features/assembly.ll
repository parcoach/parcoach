; RUN: %parcoach -S %s -o - | %filecheck %s --check-prefix=CHECK-ASM
; RUN: touch %t.ll %t.json
; RUN: chmod 000 %t.ll %t.json
; RUN: %not %parcoach -S %s -o %t.ll 2>&1 | %filecheck %s --check-prefix=CHECK-DENIED
; RUN: %parcoach %s -time-trace -time-trace-file=%t.json 2>&1 | %filecheck %s --check-prefix=CHECK-TRACE
; RUN: rm %t.ll %t.json
; CHECK-ASM: define i32 @foo
; CHECK-DENIED: Permission denied
; CHECK-TRACE: Could not open {{.*}}.json
define i32 @foo(i32 %a) {
  ret i32 %a
}
