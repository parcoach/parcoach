#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


int main(int argc, char** argv){
	
	#pragma omp parallel
	{

		int N=32;
		int sum=0;

		printf("Hi from thread %d\n",omp_get_thread_num());


		if(omp_get_thread_num()%2){
			#pragma omp single
			{
				printf("Thread %d is in the single region\n",omp_get_thread_num());
			}
		}else{
			#pragma omp barrier
		}

	}

	printf("Test OK\n");
	return 0;
}
