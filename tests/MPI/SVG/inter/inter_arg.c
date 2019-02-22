#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

void bar(int a, int b) {
  // Rank dependent barrier
  if (b > 0)
    MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char** argv){
  int rank, size;

  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&size);

  printf("Hi from Process %d\n",rank);

  MPI_Barrier(MPI_COMM_WORLD);

  if (argc > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  printf("Test OK\n");

  bar(rank, 0);
  bar(0, rank);

  MPI_Finalize();
  return 0;
}
