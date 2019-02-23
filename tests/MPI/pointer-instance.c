#include <stdio.h>
#include "mpi.h"

void f(int a) {
  if (a > 0)
    MPI_Barrier(MPI_COMM_WORLD);

  MPI_Comm_rank(MPI_COMM_WORLD,&a);

  if (a > 0)
    MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char **argv) {
  MPI_Init(&argc,&argv);

  f(argc);
}



