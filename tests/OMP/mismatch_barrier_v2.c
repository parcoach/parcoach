// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: warning: __kmpc_barrier line 17 possibly not called by all processes because of conditional(s) line(s)  16
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */

int main(int argc, char **argv) {

#pragma omp parallel
  {
    if (omp_get_thread_num() % 2) {
#pragma omp barrier
    }
  }

  printf("Test OK\n");
  return 0;
}
