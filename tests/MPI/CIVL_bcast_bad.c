/**
 * This example demonstrates the usage of MPI collective operations,
 * which should be called in the same orders for all MPI processes.
 * This example has an error when there are more than five MPI processes.
 */

#include<mpi.h>
#include<assert.h>

int main(int argc, char * argv[]) 
{ 
    int rank;
    int procs;
    int value;

    MPI_Init(&argc,&argv); 
    MPI_Comm_size(MPI_COMM_WORLD, &procs); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 

    if (rank == 0) 
      value = 123;

    if (rank != 5)
      MPI_Bcast(&value, 1, MPI_INT, 0, MPI_COMM_WORLD); 
    //assert(value == 123);
    MPI_Finalize(); 
    return 0; 
}
