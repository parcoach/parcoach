#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */


int main(int argc, char** argv){
	
	#pragma omp parallel
	{
		if(omp_get_thread_num()%2)
		{
			#pragma omp barrier
		}
	}

	printf("Test OK\n");
	return 0;
}
