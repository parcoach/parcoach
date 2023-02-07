/*
 *
 * void upc all reduceT(shared void *dst, shared const void *src,
			upc op t op, size t nelems, size t blk size,
			TYPE (*func)(TYPE, TYPE), upc flag t sync mode);
 * T={C,UC,S,US,I,UI,L,UL,F,D,LD} depending on the type (signed char,unsigned char, signed short,...)
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <upc.h>
#include <upc_collective.h>

// UPC op 
signed int my_upc_op(unsigned int a, unsigned int b){
	int res=0;
	res= a=b? a:-1;
	return res;
}


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

		fprintf(stderr,"%d: in cc is going to call %s\n",(int)gasnet_mynode(), OP_name);

		upc_barrier;
		upc_all_reduceI(res, OP_color, UPC_FUNC, THREADS, sizeof(int) ,signed int (*my_upc_op)(unsigned int, unsigned int), UPC_IN_NOSYNC);
		upc_barrier;


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


void check_collective_return(int OP_color, const char* OP_name, int OP_line, char* warnings, char *FILE_name)
{

}
