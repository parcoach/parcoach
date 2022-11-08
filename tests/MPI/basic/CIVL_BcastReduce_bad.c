/* A bad example shows how CIVL catches the misusage of MPI collective
   routines. In this example, even processes and odd processes calls
   different MPI collective routines. */
#include <mpi.h>
#include <assert.h>

int main(int argc, char * argv[]) {
  int nprocs, rank;
  int num;
  int recv;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if(rank == 0) num = 3;
  MPI_Bcast(&num, 1, MPI_INT, 0, MPI_COMM_WORLD);
  
  if(rank%2 ==  0)
    MPI_Reduce(&num, &recv, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  else
    MPI_Bcast(&num, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if(rank == 0) num = recv;
  if(rank%2 == 0)
    MPI_Bcast(&num, 1, MPI_INT, 0, MPI_COMM_WORLD);
  else
    MPI_Reduce(&num, &recv, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
  assert(num == 3 * nprocs);
  MPI_Finalize();
  return 0;
}
