#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


/*
 * DESCRIPTION: only processes with rank%2=0 call a barrier
 */

void myBarrier(){
	MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char** argv){
	int rank, size;
	int z;

	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(size<2){
		printf("This test needs at least 2 MPI processes\n");
		printf("rank=%d size=%d\n", rank, size);
		MPI_Finalize();
		return 1;
	}

	printf("Hi from proc %d\n", rank);
	myBarrier();

	if(rank%2)
		MPI_Barrier(MPI_COMM_WORLD);

	printf("Bye from proc %d\n", rank);

	memcpy(&z, &rank, sizeof(int));

	if (z > 0)
	  MPI_Barrier(MPI_COMM_WORLD);

	int y = 0;

	memset(&y, rank, sizeof(int));

	if (y > 0)
	  MPI_Barrier(MPI_COMM_WORLD);

	int h = 0;

	if (rank % 2)
	  memset(&h, 3, sizeof(int));

	if( h > 0)
	  MPI_Barrier(MPI_COMM_WORLD);

	MPI_Finalize();
	return 0;
}
