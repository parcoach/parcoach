#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */

#define N 5

int main(int argc, char** argv){
  int x = 0;

	#pragma omp parallel
	{
    while (x < N) {
      if(omp_get_thread_num()%2)
      {
        #pragma omp barrier
      }
      else{
        #pragma omp barrier
      }
      x++;
    }
	}

	printf("Test OK\n");
	return 0;
}
