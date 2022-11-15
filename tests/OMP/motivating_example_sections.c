// XFAIL: true
// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: 2 warning(s) issued
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

void c() {
#pragma omp barrier
}

int main(int argc, char **argv) {

#pragma omp parallel
  {
#pragma omp sections
    {
#pragma omp section
      { c(); }
#pragma omp section
      { c(); }
    }
  }
  return 0;
}
