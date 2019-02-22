#include <stdio.h>
#include "mpi.h"



void g(int *a){
	*a = 1;
}


int main(int argc, char **argv){

  MPI_Init(&argc,&argv);
	int rank, val;
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);


	if(rank)
		g(&val);
	else
		g(&val);

	if(val)
  	MPI_Barrier(MPI_COMM_WORLD);

  MPI_Finalize();
  return 0;
}
