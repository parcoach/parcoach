#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


/*
 * DESCRIPTION: MPI_barrier called with MPI_Ibarrier
 */

void c(){
	MPI_Request req;
	MPI_Status status;

	MPI_Ibarrier(MPI_COMM_WORLD,&req);
	MPI_Wait(&req, &status);

}

int main(int argc, char** argv){
	int rank, size;
	
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Comm_size(MPI_COMM_WORLD,&size);


	if(rank%2){
		MPI_Barrier(MPI_COMM_WORLD);
	}else{
		c();
	}

	MPI_Finalize();
	return 0;
}
