#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


/*
 * DESCRIPTION:
 */

void myBarrier(){
	MPI_Barrier(MPI_COMM_WORLD);
}

void myRecursiveFunction(int i){
	while(i<3){
		//printf("i=%d\n",i);
		myRecursiveFunction(++i);
	}
}

void inloop(int i){
	myRecursiveFunction(i);
}

int main(int argc, char** argv){
	int rank, size;
	
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(size<2){
		printf("This test needs at least 2 MPI processes\n");
		MPI_Finalize();
		return 1;
	}

	printf("Hi from proc %d\n", rank);

	for(int i=0; i<2; i++){
		MPI_Barrier(MPI_COMM_WORLD);
		inloop(i);
	}

	myBarrier();

	if(rank%2)
		MPI_Barrier(MPI_COMM_WORLD);

	printf("Bye from proc %d\n", rank);
	MPI_Finalize();
	return 0;
}
