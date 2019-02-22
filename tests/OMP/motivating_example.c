#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */

/*
void f(int r){

	if(r==0){
		printf("Thread %d in conditional\n",omp_get_thread_num());
		#pragma omp barrier
	}

}
*/


int main(int argc, char** argv){

	int r, id;	

	#pragma omp parallel private(r)
	{
		id=omp_get_thread_num();
		r=omp_get_thread_num()%2;
		printf("I am Thread %d / %d, r=%d\n",omp_get_thread_num(),omp_get_num_threads(),r);
		if(r==0){
			printf("Thread %d in conditional\n",omp_get_thread_num());
			#pragma omp barrier
		}

		//f(r);
		printf("Thread %d out of f\n",omp_get_thread_num());
	}
	printf("Test OK\n");
	return 0;
}
