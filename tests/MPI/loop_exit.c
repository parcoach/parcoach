#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


int main(int argc, char** argv){
  int rank, size, i=1, j=10, var=0;

  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);
  MPI_Comm_size(MPI_COMM_WORLD,&size);

  if(size<2){
    printf("This test needs at least 2 MPI processes\n");
    MPI_Finalize();
    return 1;
  }

  if(rank%2){
    while(i<10){
      i++;
    }
  }else{
    while(j<20){
      if (rank<10)
        exit(-1);
      j++;
    }
  }

  MPI_Barrier(MPI_COMM_WORLD);


  MPI_Finalize();
  return 0;
}
