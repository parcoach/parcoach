#include <stdio.h>
#include "mpi.h"


int main(int argc, char **argv){

	MPI_Init(&argc,&argv);
	int r,size,v=0;
	MPI_Comm_rank(MPI_COMM_WORLD,&r);
    MPI_Comm_size(MPI_COMM_WORLD,&size);

    if(size<2){
        printf("This test needs at least 2 MPI processes\n");
        MPI_Finalize();
        return 1;
    }


	if(!r)
		v=1;
	else
		v=2;

	if(v == 2)
		MPI_Barrier(MPI_COMM_WORLD);

	MPI_Finalize();
	return 0;
}
