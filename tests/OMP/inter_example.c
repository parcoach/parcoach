#include <stdlib.h>
#include <stdio.h>
#include "omp.h"


void g(){
	#pragma omp barrier
}

void f(){
	int r;
	#pragma omp parallel private(r)
	{
		r=omp_get_thread_num()%2;
		if(r==0)
			g();
	}
}

int main(int argc, char** argv){
	f();
	return 0;
}
