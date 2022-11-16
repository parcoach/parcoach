#include "mpi.h"
#include <stdio.h>

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);
  int r;
  MPI_Comm_rank(MPI_COMM_WORLD, &r);

  if (r)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return 0;
}
