#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  int rank, size, j = 10;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (size < 2) {
    printf("This test needs at least 2 MPI processes\n");
    MPI_Finalize();
    return 1;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  while (j < 20) {
    if (rank == j) {
      MPI_Finalize();
      exit(-1);
    }
    j++;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return 0;
}
