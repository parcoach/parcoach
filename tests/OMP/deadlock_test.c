// XFAIL: true
// FIXME: parcoach detects nothing statically.
// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: 1 warning(s) issued
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

void mysingle() {

#pragma omp single
  {
    printf("Thread %d is in the second single region\n", omp_get_thread_num());
  }
}

int main(int argc, char **argv) {

#pragma omp parallel
  {
#pragma omp single
    {
      printf("Thread %d is in the first single region\n", omp_get_thread_num());
      mysingle();
    }
  }

  printf("Test OK\n");
  return 0;
}
