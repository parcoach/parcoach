/**
 * This program has an error because not all processes 
 * execute MPI_Barrier and MPI_Scatter in the same order.
 */

#include<mpi.h>
#include<stdio.h>
#include<stdlib.h>

int main(int argc, char* argv[]){
    int rank;
    int procs;
    int* sendbuf, *rcvbuf;
    
    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &procs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    sendbuf = (int*)malloc(sizeof(int)*procs);
    rcvbuf = (int*)malloc(sizeof(int));
    if(rank == 0)
    {
        for(int i=0; i<procs; i++){
            sendbuf[i] = procs - i;
        }
        MPI_Scatter(sendbuf, 1, MPI_INT, rcvbuf, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);
    }else{
        MPI_Barrier(MPI_COMM_WORLD);
        MPI_Scatter(sendbuf, 1, MPI_INT, rcvbuf, 1, MPI_INT, 0, MPI_COMM_WORLD);
        printf("process %d receivs %d\n", rank, *rcvbuf);
    }
    free(sendbuf);
    free(rcvbuf);
    MPI_Finalize();
    return 0;
}
