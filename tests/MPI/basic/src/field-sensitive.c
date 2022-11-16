#include "mpi.h"
#include <stdio.h>

typedef struct _hydroparam {
  int mype;
  int nproc;
} hydroparam_t;

void f(hydroparam_t *H) {
  MPI_Comm_size(MPI_COMM_WORLD, &H->nproc);
  MPI_Comm_rank(MPI_COMM_WORLD, &H->mype);

  if (H->mype > 1)
    MPI_Barrier(MPI_COMM_WORLD);
}

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);

  hydroparam_t *H;
  f(H);

  MPI_Finalize();
  return 0;
}
