#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

int getRank() {
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int z = 12;

  if (rank > 0)
    z = 5;
  else z = 0;

  return z;
}

int retAny(int a) {
  return a * 54;
}

int main(int argc, char** argv) {
  MPI_Init(&argc,&argv);

  int z = argc;

  z += getRank();

  // Rank dependent barrier.
  if (z > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Barrier(MPI_COMM_WORLD);

  if (argc > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  if (retAny(argc) > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();

  return 0;
}
