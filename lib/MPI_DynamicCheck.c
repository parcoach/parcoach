#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "mpi.h"


/* the user-defined function for the new operator */
void areequals(int *in, int *inout, int *len, MPI_Datatype *type){
	int i;
	for(i=0;i<*len;i++){
		if(*inout != *in)
			*inout=~0;
		in++;
		inout++;
	}
}

/* Check Collective MPI Function   
 *
 *  color = type of collective (unique per collective)
 *  pcom = communicator
 *  OP_name = collective name 
 *  OP_line = line in the source code of the collective 
 *  warnings = warnings emitted at compile-time
 *  FILE_name = name of the file
 */
void check_collective_MPI(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
{
	int rank;
	int sizeComm;

	// make sure MPI_Init has been called
	int flag;
	MPI_Initialized(&flag);

	if(flag)
	{
		// Fortran programs are not handled
		MPI_Comm ini_comm = MPI_COMM_WORLD;

		MPI_Comm_rank(ini_comm, &rank);
		MPI_Comm_size(ini_comm, &sizeComm);

		int res=0;
		MPI_Op equalsop;
		int commutatif=1;
		MPI_Op_create((void *)areequals,commutatif,&equalsop);

		MPI_Reduce(&OP_color,&res,1,MPI_INT,equalsop,0,ini_comm);
		MPI_Op_free(&equalsop);

		if(rank==0 ){
#ifdef DEBUG
			printf("\033[0;36m CHECK CC OK\033[0;0m\n");
#endif
			if(res==~0){
#ifdef DEBUG
				printf("\033[0;36m CHECK CC NOK\033[0;0m\n");
#endif
				printf("\033[0;35m PARCOACH DYNAMIC-CHECK : Error detected on rank %d\n"
						  "PARCOACH DYNAMIC-CHECK : Abort is invoking line %d before calling %s in %s\n"
						  "PARCOACH DYNAMIC-CHECK : see warnings about conditionals line %s\035[0;0m\n",
						   rank, OP_line,OP_name, FILE_name,warnings);
				MPI_Abort(MPI_COMM_WORLD,0);
			}
		}
#ifdef DEBUG
				printf("\033[0;36m PARCOACH DYNAMIC-CHECK : OK\033[0;0m\n");
#endif
	}
}



