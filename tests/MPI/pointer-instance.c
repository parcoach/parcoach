#include <stdio.h>
#include "mpi.h"

int main(int argc, char **argv) {
  MPI_Init(&argc,&argv);

  f(argc);
}



void f(int a) {
  if (a > 0)
    MPI_Barrier(com);

  MPI_Comm_rank(com,&a);

  if (a > 0)
    MPI_Barrier(com);
}
