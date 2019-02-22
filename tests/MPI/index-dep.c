#include <stdio.h>
#include "mpi.h"

void f(){
 int r,s;
 MPI_Comm_size(MPI_COMM_WORLD,&s);
 MPI_Comm_rank(MPI_COMM_WORLD,&r);

 int *A = malloc(s);
 for(int i=0; i<s; i++)
	A[i] = i;

 if(A[r] > 10)
	MPI_Barrier(MPI_COMM_WORLD);

}


int main(int argc, char **argv){

	MPI_Init(&argc,&argv);

	f();

	MPI_Finalize();
	return 0;
}
