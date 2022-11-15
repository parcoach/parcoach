#include "mpi.h"
#include <stdio.h>

void foo(int *a) { (*a)--; }

void bar(int *a) { (*a)++; }

typedef void (*FuncTy)(int *);

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);
  int r;
  MPI_Comm_rank(MPI_COMM_WORLD, &r);

  FuncTy call = argc == 3 ? foo : bar;
  if (r) {
    call(&argc);
  }
  MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return 0;
}
