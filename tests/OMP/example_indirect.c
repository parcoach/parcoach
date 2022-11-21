// RUN: %clang %openmp -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach --disable-output %t.ll -check-omp 2>&1 | %filecheck %s
// CHECK: warning: __kmpc_barrier line {{[0-9]+}} possibly not called by all processes because of conditional(s) line(s)  {{[0-9]+}}
#include "omp.h"
#include <stdio.h>
#include <stdlib.h>

typedef void * (*FuncTy)(const char*, ...);

void *parcoach_var_mod(const char *fmt, ...);

void foo(int t, char *s) {
printf("Thread %d in conditional\n", t);
#pragma omp barrier
FuncTy call;
call = t == 3 ? parcoach_var_mod : (FuncTy)printf;
call(s, s, s);

}

int main(int argc, char **argv) {

  int r, id;

#pragma omp parallel private(r)
  {
    id = omp_get_thread_num();
    r = omp_get_thread_num() % 2;
    printf("I am Thread %d / %d, r=%d\n", omp_get_thread_num(),
           omp_get_num_threads(), r);
    if (r == 0) {
      foo(omp_get_thread_num(), "coucou");
    }

    // f(r);
    printf("Thread %d out of f\n", omp_get_thread_num());
  }
  printf("Test OK\n");
  return 0;
}
