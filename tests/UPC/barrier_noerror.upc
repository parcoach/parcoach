#include <stdlib.h>
#include <stdio.h>
#include <upc.h>

int main(int argc, char **argv){

	printf("Hi from thread %d\n",MYTHREAD);

	upc_barrier;

	return 0;
}

