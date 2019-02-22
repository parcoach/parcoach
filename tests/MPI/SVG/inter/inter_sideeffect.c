#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

void sideEffect(int *a) {
  int r;

  MPI_Comm_rank(MPI_COMM_WORLD, &r);

  *a = r;
}

void NoSideEffect(int a) {
  int r = MPI_Comm_rank(MPI_COMM_WORLD, &r);
  a = r;
}

void falseSideEffect(int *a) {
  int r;

  MPI_Comm_rank(MPI_COMM_WORLD, &r);

  *a = 0;
}

int main(int argc, char** argv){
  int rank, size;

  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&size);

  MPI_Barrier(MPI_COMM_WORLD);

  if (argc > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  int a = 0;
  sideEffect(&a);

  // Rank dependent barrier.
  if (a > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  int b = argc;
  NoSideEffect(b);
  if (b > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  // False positive.
  int c = 0;
  falseSideEffect(&c);
  if (c > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return 0;
}

