/*
 *
 * void upc all reduceT(shared void *dst, shared const void *src,
			upc op t op, size t nelems, size t blk size,
			TYPE (*func)(TYPE, TYPE), upc flag t sync mode);
 * T={C,UC,S,US,I,UI,L,UL,F,D,LD} depending on the type (signed char,unsigned char, signed short,...)
 * 
 * Reference: UPC specification v1.3
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <upc.h>
#include <upc_collective.h>

// UPC op - Returns -1 if a != b
static int my_upc_op(int a, int b){
	//fprintf(stderr, "%d: a=%d, b=%d\n",(int)gasnet_mynode(), a,b);
	int res=0;
	res= a==b? a:-1;
	//fprintf(stderr, "%d: res=%d\n",(int)gasnet_mynode(), res);
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

	static shared     int mycolor[THREADS];
	static shared int res=0;
	mycolor[MYTHREAD]=OP_color;
#ifdef _DEBUG_INFO
	fprintf(stderr,"%d: in cc is going to call %s - color=%d\n",(int)gasnet_mynode(), OP_name, mycolor[MYTHREAD]);
#endif

		upc_barrier;
		upc_all_reduceI(&res, &mycolor, UPC_FUNC, THREADS, 1, &my_upc_op, UPC_IN_NOSYNC);
#ifdef _DEBUG_INFO
		fprintf(stderr,"%d: upc_all_reduceI OK\n",(int)gasnet_mynode());
#endif
		upc_barrier;
#ifdef _DEBUG_INFO
		fprintf(stderr,"%d: res=%d\n",(int)gasnet_mynode(), res);
#endif
		upc_barrier;


		if((int)gasnet_mynode()==0){
#ifdef _DEBUG_INFO
			printf("\033[0;36m CHECK CC OK\033[0;0m\n");
#endif
			if(res==-1){
#ifdef _DEBUG_INFO
				printf("\033[0;36m CHECK CC NOK\033[0;0m\n");
#endif
				printf("\033[0;35m PARCOACH DYNAMIC-CHECK : Error detected on rank %d\n"
						  "PARCOACH DYNAMIC-CHECK : Abort is invoking line %d before calling %s in %s\n"
						  "PARCOACH DYNAMIC-CHECK : see warnings about conditionals line %s\035[0;0m\n",
						   (int)gasnet_mynode(), OP_line,OP_name, FILE_name,warnings);
				abort();
			}
		}
#ifdef _DEBUG_INFO
		printf("\033[0;36m PARCOACH DYNAMIC-CHECK : OK\033[0;0m\n");
#endif
}



