#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"



void g(int s){
	int res=0;
	int i=12;

	if(s>256)
		MPI_Reduce(&i,&res,1,MPI_INT,MPI_SUM,0,MPI_COMM_WORLD);
}

void f(){
	int s,r,n;

	MPI_Comm_rank(MPI_COMM_WORLD,&r);
	MPI_Comm_size(MPI_COMM_WORLD,&s);

	if(r%2)
		n=1;
	else
		n=2;

	if(n==1)
		g(s);

	MPI_Barrier(MPI_COMM_WORLD);
}


int main(int argc, char** argv){
	
	MPI_Init(&argc,&argv);
	f();
	MPI_Finalize();

	return 0;
}
