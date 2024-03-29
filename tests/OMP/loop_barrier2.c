// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: warning: __kmpc_barrier line 25 possibly not called by all processes because of conditional(s) line(s)  24
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */

#define N 5
#define M 5

int main(int argc, char **argv) {
  int x = 0, y = 0;

#pragma omp parallel
  {
    int r = omp_get_thread_num();

    while (x < N) {
      while (y < M) {
        if (r % 2) {
#pragma omp barrier
        }
        y++;
      }
      x++;
    }
  }

  printf("Test OK\n");
  return 0;
}
