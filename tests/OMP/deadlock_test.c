#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


void mysingle(){

				#pragma omp single
				{
					printf("Thread %d is in the second single region\n",omp_get_thread_num());
				}

}

int main(int argc, char** argv){
	
	#pragma omp parallel
	{
			#pragma omp single
			{
				printf("Thread %d is in the first single region\n",omp_get_thread_num());
				mysingle();
			}
	}

	printf("Test OK\n");
	return 0;
}
