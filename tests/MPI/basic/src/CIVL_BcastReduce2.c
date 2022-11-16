/**
 * This example demonstrates the usage of MPI collective operations,
 * which should be called in the same orders for all MPI processes.
 * This example has an error when there are more than five MPI processes.
 */

#include <assert.h>
#include <mpi.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  int nprocs, rank;
  int num;
  int recv;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if (rank == 0)
    num = 3;
  MPI_Bcast(&num, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Allreduce(&num, &recv, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (rank != 5)
    MPI_Reduce(&recv, &num, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Allreduce(&num, &recv, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  assert(recv == (3 * nprocs * nprocs + 3 * (nprocs - 1)));
  MPI_Finalize();
  return 0;
}
