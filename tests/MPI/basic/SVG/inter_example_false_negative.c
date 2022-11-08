#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


/*
 * DESCRIPTION: mismatch error - false negative with an intraprocedural analysis
 * rank %2 : MPI_Barrier
 * others: nothing
 */

void f(){
	MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char** argv){
	int rank, size,i=1,res=0;
	
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
		f();

	printf("Bye from proc %d\n", rank);
	MPI_Finalize();
	return 0;
}
