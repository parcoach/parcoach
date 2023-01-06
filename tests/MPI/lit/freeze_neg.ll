; RUN: %parcoach -check=mpi %s 2>&1 | %filecheck %s
; CHECK: Warning: no main function in module

define dso_local i32 @a(i32 noundef %0, ptr noundef %1, double noundef %2) {
  %4 = freeze i32 %0
  %5 = fneg double %2
  ret i32 %4
}

