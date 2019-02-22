#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"


struct list {
  void *data;
  struct list *next;
};

int main(int argc, char** argv){
	int rank, size;


	struct list *first = malloc(sizeof(*first));
	struct list *cursor = first;

	for (int i=0; i<10; i++) {
	  cursor->next = malloc(sizeof(*cursor));
	  cursor->data = NULL;
	  cursor = cursor->next;
	}

	cursor->data = malloc(sizeof(int));

	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD,cursor->data);
	MPI_Comm_size(MPI_COMM_WORLD,&size);

	for (int i=0; i<10; i++) {
	  first = first->next;
	}

	int value = *((int *) first->data);


	// Rank dependent barrier.
	if (value > 0)
	  MPI_Barrier(MPI_COMM_WORLD);

	// False positive.
	if (argc > 0 )
	  MPI_Barrier(MPI_COMM_WORLD);

	MPI_Finalize();
	return 0;
}
