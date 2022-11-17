; RUN: %parcoach -parcoach-version -disable-output %s | grep "%version" | %filecheck %s
; CHECK: PARCOACH version
declare void @foo()
