#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


void areequals(int *in, int *inout, int *len, MPI_Datatype *type){
        int i;
        for(i=0;i<*len;i++){
                if(*inout != *in)
                        *inout=~0;
                in++;
                inout++;
        }
}

void CC(int res){
	if(res==~0){
             printf("\033[0;36m CHECK CC NOK\033[0;0m\n");
             MPI_Abort(MPI_COMM_WORLD,0);
        }
}

int main(int argc, char** argv){
	int rank, size;
	
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	MPI_Request req1,req2,req3;
	MPI_Status status;

	int res1=0, res2=0, res3=0;
	int OP_color =42;
        MPI_Op equalsop;
        int commutatif=1;
        MPI_Op_create((void *)areequals,commutatif,&equalsop);
                
        printf("P%d: IR\n",rank);
        MPI_Ireduce(&OP_color,&res1,1,MPI_INT,equalsop,0,MPI_COMM_WORLD, &req1);

	if(rank==0){
		OP_color=1;
             	printf("P%d: IR\n",rank);
		MPI_Ireduce(&OP_color,&res2,1,MPI_INT,equalsop,0,MPI_COMM_WORLD, &req2);
		MPI_Wait(&req1,&status);
		CC(res1);
             	printf("P%d: B\n",rank);
		MPI_Barrier(MPI_COMM_WORLD);
	}

	if(1){
             	printf("P%d: B\n",rank);
		MPI_Barrier(MPI_COMM_WORLD);
 	}else{		
             	printf("P%d: B\n",rank);
		MPI_Barrier(MPI_COMM_WORLD);
	}

	if(rank!=0){
		OP_color=1;
             	printf("P%d: IR\n",rank);
		MPI_Ireduce(&OP_color,&res2,1,MPI_INT,equalsop,0,MPI_COMM_WORLD, &req2);
		MPI_Wait(&req1,&status);
		CC(res1);
             	printf("P%d: B\n",rank);
		MPI_Barrier(MPI_COMM_WORLD);
	}
	
	MPI_Wait(&req2,&status);
	CC(res2);


        MPI_Op_free(&equalsop);

	MPI_Finalize();
	return 0;
}
