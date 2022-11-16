#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

void g(int rank, int size) {
  int res = 0;
  int i = 12;

  if (rank % 2)
    MPI_Reduce(&i, &res, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  else
    MPI_Reduce(&i, &res, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

  MPI_Barrier(MPI_COMM_WORLD);
}

int main(int argc, char **argv) {
  int rank, size, i = 1, res = 0;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (size < 2) {
    printf("This test needs at least 2 MPI processes\n");
    MPI_Finalize();
    return 1;
  }

  if (rank == 0) {
    int res = 0;
    int i = 12;
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Reduce(&i, &res, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  } else {
    g(rank, size);
  }

  MPI_Finalize();
  return 0;
}
