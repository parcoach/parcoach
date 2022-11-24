; RUN: %parcoach -parcoach-version | grep "%version" | %filecheck %s
; CHECK: PARCOACH version
declare void @foo()
