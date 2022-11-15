// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: 0 warning(s) issued
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {

#pragma omp parallel
  {

    int N = 32;
    int sum = 0;

    printf("Hi from thread %d\n", omp_get_thread_num());

    if (omp_get_thread_num() % 2) {
#pragma omp single
      { printf("Thread %d is in the single region\n", omp_get_thread_num()); }
    } else {
#pragma omp barrier
    }
  }

  printf("Test OK\n");
  return 0;
}
