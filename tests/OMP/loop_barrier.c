// XFAIL: true
// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: 0 warning(s) issued
// FIXME: parcoach reports false-positive on line 22.
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */

#define N 5

int main(int argc, char **argv) {
  int x = 0;

#pragma omp parallel
  {
    while (x < N) {
      if (omp_get_thread_num() % 2) {
#pragma omp barrier
      } else {
#pragma omp barrier
      }
      x++;
    }
  }

  printf("Test OK\n");
  return 0;
}
