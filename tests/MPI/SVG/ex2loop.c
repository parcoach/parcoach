#include <stdio.h>
#include "mpi.h"


int main(int argc, char **argv){

		int color=0, i=0, r=0, s=0;
		MPI_Init(&argc,&argv);
		MPI_Comm_size(MPI_COMM_WORLD,&s);
		MPI_Comm_rank(MPI_COMM_WORLD,&r);

		MPI_Comm newcom;
		MPI_Comm_split(MPI_COMM_WORLD,r%2,r,&newcom);

		for(i=0; i<5; i++){

				if(r%2){
						MPI_Barrier(MPI_COMM_WORLD);
				}else{
						printf("do nothing\n");
				}

				if(!r%2){
						MPI_Barrier(newcom);
				}else{
						printf("do nothing\n");
				}

		}


		MPI_Finalize();
		return 0;
}
