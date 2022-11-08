#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


/*
 * DESCRIPTION: no error, all processes call a allreduce
 */


int main(int argc, char** argv){
	int rank, size, i=1, res=0; 
	
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(size<2){
		printf("This test needs at least 2 MPI processes\n");
		MPI_Finalize();
		return 1;
	}

	printf("Hi from proc %d\n", rank);

	if(rank%2)
		MPI_Allreduce(&i,&res,1,MPI_INT,MPI_SUM,0,MPI_COMM_WORLD);
	else
		MPI_Allreduce(&i,&res,1,MPI_INT,MPI_SUM,0,MPI_COMM_WORLD);
		

	printf("Bye from proc %d\n", rank);
	MPI_Finalize();
	return 0;
}
