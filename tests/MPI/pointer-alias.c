#include <stdio.h>
#include "mpi.h"

int main(int argc, char **argv) {
  MPI_Init(&argc,&argv);

  int a = 1;
  int *b = &a;

  if (a > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Comm_rank(MPI_COMM_WORLD,b);

  if (a > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
}
