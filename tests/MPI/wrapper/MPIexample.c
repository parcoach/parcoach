// RUN: %wrapper %mpicc -c %s -o /dev/null 2>&1 | %filecheck %s
// RUN: %wrapper -check=mpi --args %mpicc -c %s -o /dev/null 2>&1 | %filecheck %s
// CHECK: possibly not called by all processes
// RUN: %wrapper %mpicc %s -o /dev/null 2>&1 | %filecheck --check-prefix=CHECK-LINK %s
// CHECK-LINK: this is a linker invocation, not running parcoach

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

void g(int s) {
  int res = 0;
  int i = 12;

  if (s > 256)
    MPI_Reduce(&i, &res, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
}

void f() {
  int s, r, n;

  MPI_Comm_rank(MPI_COMM_WORLD, &r);
  MPI_Comm_size(MPI_COMM_WORLD, &s);

  if (r % 2)
    n = 1;
  else
    n = 2;

  if (n == 1)
    g(s);

  MPI_Barrier(MPI_COMM_WORLD);
}

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);
  f();
  MPI_Finalize();

  return 0;
}
