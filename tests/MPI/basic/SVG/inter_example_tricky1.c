#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


/*
 * DESCRIPTION: mismatch error
 */

void f(){
	MPI_Barrier(MPI_COMM_WORLD);

}

int main(int argc, char** argv){
	int rank, size, var;
	
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	var=rank+1;
	
	if(size<2){
		printf("This test needs at least 2 MPI processes\n");
		MPI_Finalize();
		return 1;
	}

	if(rank%2){
		if(var%2){
			MPI_Barrier(MPI_COMM_WORLD);
		}else{
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}else{
		if(var%2)
			f();
	}


	printf("Bye from proc %d\n", rank);
	MPI_Finalize();
	return 0;
}
