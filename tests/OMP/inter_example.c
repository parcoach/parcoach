// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: warning: __kmpc_barrier line 9 possibly not called by all processes because of conditional(s) line(s)  17
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

void g() {
#pragma omp barrier
}

void f() {
  int r;
#pragma omp parallel private(r)
  {
    r = omp_get_thread_num() % 2;
    if (r == 0)
      g();
  }
}

int main(int argc, char **argv) {
  f();
  return 0;
}
