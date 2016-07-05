//// NOT WORKING YET..


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <upcr.h>

/* Check Collective UPC Function   
 *
 *  color = type of collective (unique per collective)
 *  OP_name = collective name 
 *  OP_line = line in the source code of the collective 
 *  warnings = warnings emitted at compile-time
 *  FILE_name = name of the file
 */
void check_collective_UPC(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
{
		int res=0;

		// Do a UPC reduce?
		//MPI_Reduce(&OP_color,&res,1,MPI_INT,equalsop,0,ini_comm);
		//MPI_Op_free(&equalsop);

		if((int)gasnet_mynode()==0){
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
						   (int)gasnet_mynode(), OP_line,OP_name, FILE_name,warnings);
				abort();
			}
		}
#ifdef DEBUG
		printf("\033[0;36m PARCOACH DYNAMIC-CHECK : OK\033[0;0m\n");
#endif
}



