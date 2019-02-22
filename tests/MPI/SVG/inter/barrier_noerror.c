#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

/*
 * DESCRIPTION: test file, all processes call a barrier - No error
 */


int main(int argc, char** argv){
  int rank, size;

  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&size);

  printf("Hi from Process %d\n",rank);

  MPI_Barrier(MPI_COMM_WORLD);

  printf("Test OK\n");
  MPI_Finalize();
  return 0;
}
