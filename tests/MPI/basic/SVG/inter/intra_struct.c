#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

struct test {
  int a;
  char c;
  double d;
  int u;
};

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

  struct test s;
  s.a = argc * 2;
  s.c = argc / 2;
  s.d = s.c * s.a;
  s.u = rank;

  // Rank dependent barrier.
  if (!s.u%2 )
    MPI_Barrier(MPI_COMM_WORLD);

  if (argc > 2) {
    MPI_Barrier(MPI_COMM_WORLD);
  }

  // Rank dependent barrier.
  if (s.c % 5 == 0)
    MPI_Barrier(MPI_COMM_WORLD);

  return 0;
}
