// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach -disable-output %t.ll -check-omp -statistics 2>&1 | %filecheck %s
// CHECK: nb functions : 11
// CHECK: nb direct calls : 17
// CHECK: nb indirect calls : 1
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

void g() {
#pragma omp barrier
}

void h() {
}

void f() {
  int r;
  typedef void (*FuncTy)();
  FuncTy call;
#pragma omp parallel private(r)
  {
    r = omp_get_thread_num() % 2;
    call = r ? h : g;
    if (r == 0)
      call();
  }
}

int main(int argc, char **argv) {
  f();
  return 0;
}
