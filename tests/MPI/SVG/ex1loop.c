#include <stdio.h>
#include "mpi.h"


#define MAXSIZE 100


void g(){

		int i=0, s=0, r=0, j=0;

		MPI_Comm_size(MPI_COMM_WORLD,&s);
		MPI_Comm_rank(MPI_COMM_WORLD,&r);

		for(i=0; i<5; i++){
				for(j=0; j<5; j++){
						MPI_Barrier(MPI_COMM_WORLD);
				}
		}
		for(i=0; i<5; i++){
				if(r%2){
						MPI_Barrier(MPI_COMM_WORLD);
				}
		}
		MPI_Barrier(MPI_COMM_WORLD);

}


void f(){

		int i=0, s=0, j=0;

		MPI_Comm_size(MPI_COMM_WORLD,&s);

		if(s>256){
				for(i=0; i<5; i++){
						j+=i;
				}
				i=j;
				MPI_Barrier(MPI_COMM_WORLD);
		}else{
				j=53;
				if(s-j>34){
						MPI_Barrier(MPI_COMM_WORLD);
				}else{
						MPI_Barrier(MPI_COMM_WORLD);
				}
		}
}


int main(int argc, char **argv){

		MPI_Init(&argc,&argv);
		f();
		g();
		MPI_Finalize();
		return 0;
}
