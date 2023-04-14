// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp -instrum-inter
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// RUN: %parcoach --disable-output %t.ll -check-omp -no-dataflow 2>&1 | %filecheck %s
// CHECK: warning: __kmpc_barrier line 25 possibly not called by all processes because of conditional(s) line(s)  23
// For this test, also generate some dots
// RUN: %parcoach --disable-output %t.ll -check-omp -dot-depgraph
// RUN: %parcoach --disable-output %t.ll -check-omp -dot-depgraph -dot-taint-paths -context-insensitive=true
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {

  int r, id;

#pragma omp parallel private(r)
  {
    id = omp_get_thread_num();
    r = omp_get_thread_num() % 2;
    printf("I am Thread %d / %d, r=%d\n", omp_get_thread_num(),
           omp_get_num_threads(), r);
    if (r == 0) {
      printf("Thread %d in conditional\n", omp_get_thread_num());
#pragma omp barrier
    }

    // f(r);
    printf("Thread %d out of f\n", omp_get_thread_num());
  }
  printf("Test OK\n");
  return 0;
}
