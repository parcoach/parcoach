#include <stdio.h>
#include "mpi.h"

// No deadlock

int main(int argc, char **argv){

	MPI_Init(&argc,&argv);
	int size;
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(size<256)
		MPI_Barrier(MPI_COMM_WORLD);

	MPI_Finalize();
	return 0;
}
