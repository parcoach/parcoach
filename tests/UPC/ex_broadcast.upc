/*
 * The upc_all_broadcast function copies a block of memory with affinity to a single thread to a block of shared memory on each thread.
 * = a thread copies a block of memory it "owns" and sends it to all threads
 * Reference: UPC specification v1.3 (v1.0: http://www.upc.mtu.edu/UPCcollectives/spec.v1.0.pdf)
 * 
 * Error: all threads do not call upc_all_broadcast
 * UPC Runtime error: Early exit detected: the following threads did not reach the final implicit barrier: 1 3 
 */

#include <stdlib.h>
#include <stdio.h>
#include <upc.h>
#include <upc_collective.h>

/*
 *  T0               D0  T0
 *  T1  D0    ->     D0  T1
 *  T2               D0  T2
 *  T3               D0  T3
 */


int main(int argc, char **argv){

	static shared int A[THREADS];
	static shared int B[THREADS];

	A[MYTHREAD]=MYTHREAD;
	fprintf(stderr,"%d: A[%d]=%d B[0]=%d\n",MYTHREAD, MYTHREAD, A[MYTHREAD], B[0]);
	
	upc_barrier;
	if(MYTHREAD%2)
		upc_all_broadcast(B,&A[1], sizeof(int), UPC_IN_NOSYNC);
		
	upc_barrier;

	fprintf(stderr,"%d: B[0]=%d\n",MYTHREAD,B[0]);


	return 0;
}

