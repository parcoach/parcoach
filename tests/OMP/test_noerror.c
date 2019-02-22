#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


/*
 * DESCRIPTION: test file, all processes call a barrier - No error
 */


int main(int argc, char** argv){
	
	#pragma omp parallel
	{

		int N=32;
		int sum=0;

		printf("Hi from thread %d\n",omp_get_thread_num());
		#pragma omp barrier

		#pragma omp single nowait
		{
			printf("Thread %d is in the single region\n",omp_get_thread_num());
		}

		#pragma omp sections
		{
			#pragma omp section
			{
				printf("Thread %d is in the first section region\n",omp_get_thread_num());
			}
			#pragma omp section
			{
				printf("Thread %d is in the second section region\n",omp_get_thread_num());
			}
		}

		#pragma omp for nowait
		for (int i=0; i<N; i++){
			sum+=i;			
		}


	#pragma omp parallel
	{
		printf("Hi from thread %d\n",omp_get_thread_num());
	}

	}

	printf("Test OK\n");
	return 0;
}
