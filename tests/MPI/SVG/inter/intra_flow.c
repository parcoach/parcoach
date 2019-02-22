#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

int main(int argc, char** argv) {
  int rank, size;

  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&size);

  if (size < 2) {
    printf("This test needs at least 2 MPI processes, having only %d\n", size);
    MPI_Finalize();
    return 1;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  int var = 10;

  if (rank == 0) {
    var = 2;
  } else {
    var = 30;
  }

  if (argc == 5)
    MPI_Barrier(MPI_COMM_WORLD);

  // This barrier depends on the rank because var value depends on the
  // rank.
  if (var == 4) {
    MPI_Barrier(MPI_COMM_WORLD);
  }

  return 0;
}
