#include <stdlib.h>
#include <stdio.h>
#include "omp.h"

/*
 * DESCRIPTION: depending of the verbosity level, a warning is issued or not
 */

#define N 5
#define M 5

int main(int argc, char** argv){
  int x = 0, y = 0;

	#pragma omp parallel
	{
    int r = omp_get_thread_num();

    while (x < N) {
      while (y < M) {
        if(r % 2)
        {
          #pragma omp barrier
        }
        /* else{ */
        /*   #pragma omp barrier */
        /* } */
        y++;
      }
      x++;
    }
	}

	printf("Test OK\n");
	return 0;
}
