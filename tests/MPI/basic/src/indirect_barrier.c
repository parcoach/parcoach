#include "mpi.h"
#include <stdio.h>

void foo(int *a) {
  MPI_Barrier(MPI_COMM_WORLD);
  (*a)--;
}

void bar(int *a) { (*a)++; }

typedef void (*FuncTy)(int *);

int main(int argc, char **argv) {

  MPI_Init(&argc, &argv);
  int r;
  MPI_Comm_rank(MPI_COMM_WORLD, &r);

  FuncTy call;
#if 0 // Doesn't work just yet
	if(r) {
		call = &foo;
	} else {
		call = &bar;
	}

	call(&argc);
#else
  call = argc == 3 ? foo : bar;
  if (r) {
    call(&argc);
  }
#endif

  MPI_Finalize();
  return 0;
}
