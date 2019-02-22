#include <stdio.h>
#include "mpi.h"


void g(int s){
   if(s>256)
		MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char **argv){

	MPI_Init(&argc,&argv);
	int s=0, r=0;
	MPI_Comm_size(MPI_COMM_WORLD,&s);
	MPI_Comm_rank(MPI_COMM_WORLD,&r);

	MPI_Barrier(MPI_COMM_WORLD);

	if(r%2)
		g(s);  

	MPI_Finalize();
	return 0;
}
