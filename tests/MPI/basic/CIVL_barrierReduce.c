/* An example shows the misusage of MPI collective routines. */
#include<mpi.h>
#include<stdio.h>

int main(int argc, char* argv[]){
    int rank;
    int procs;
    int localsum, sum;
    
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &procs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    localsum = 0;
    for(int i=0; i<=rank; i++){
        localsum += i;
    }
    printf("process %d has local sum of %d\n", rank, localsum);
    if(rank%2){
        printf("process %d enters barrier\n", rank);
        MPI_Barrier(MPI_COMM_WORLD);
        printf("process %d exits barrier\n", rank);
        MPI_Reduce(&localsum, &sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    }else{
        MPI_Reduce(&localsum, &sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
        printf("process %d enters barrier\n", rank);
        MPI_Barrier(MPI_COMM_WORLD);
        printf("process %d exits barrier\n", rank);
    }
    if(rank == 0)
        printf("total sum is %d\n", sum);
    MPI_Finalize();
    return 0;
}
