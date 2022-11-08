#include <stdio.h>
#include "mpi.h"


void g(){
   int r;
   MPI_Comm_rank(MPI_COMM_WORLD,&r);
   if(r){
		MPI_Barrier(MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);
   }else{
		MPI_Barrier(MPI_COMM_WORLD);
   }
}


int main(int argc, char **argv){

	MPI_Init(&argc,&argv);
	int r,size;
	MPI_Comm_rank(MPI_COMM_WORLD,&r);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	if(size>1)
	  g();  

	MPI_Finalize();
	return 0;
}
