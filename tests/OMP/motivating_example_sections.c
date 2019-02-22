#include <stdlib.h>
#include <stdio.h>
#include "omp.h"



void c(){
	#pragma omp barrier;
}

int main(int argc, char** argv){


	#pragma omp parallel 
	{
		#pragma omp sections
		{
			#pragma omp section
			{
				c();
			}
			#pragma omp section
			{
				c();
			}
		}
	}
	return 0;
}
