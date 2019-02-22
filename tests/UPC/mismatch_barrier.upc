/*
 * Error: all threads do not call upc_barrier
 * UPC Runtime error: Early exit detected: the following threads did not reach the final implicit barrier: 1 3 
 */

#include <stdlib.h>
#include <stdio.h>
#include <upc.h>

int main(int argc, char **argv){

  int r=8;

	if(THREADS<2){
		printf("This test needs at least 2 MPI processes\n");
		return 1;		
	}


	if(r)
		upc_barrier;

	return 0;
}

