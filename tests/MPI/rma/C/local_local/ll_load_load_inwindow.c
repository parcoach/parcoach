// RUN: %mpicc -g -S -emit-llvm %s -o %t.ll
// RUN: %parcoach -check=rma 2>&1 %t.ll | %filecheck %s
// CHECK-NOT: LocalConcurrency detected
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

  // Check that only two processes are spawn
  int comm_size;
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (comm_size != 2) {
    printf(
        "This application is meant to be run with 2 MPI processes, not %d.\n",
        comm_size);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }

  // get my rank
  int my_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

  // create the window
  int window_buffer[100] = {0};
  MPI_Win window;
  MPI_Win_create(&window_buffer, 100 * sizeof(int), sizeof(int), MPI_INFO_NULL,
                 MPI_COMM_WORLD, &window);

  window_buffer[0] = 12345;

  MPI_Win_lock_all(0, window);

  if (my_rank == 0) {
    // Load access to window_buffer[0]
    int my_value_2 = window_buffer[0];

    // Load access to window_buffer[0]
    int my_value_3 = window_buffer[0];
  }

  MPI_Win_unlock_all(window);

  // Destroy the window
  MPI_Win_free(&window);
  printf("I passed the win_free  !\n");

  MPI_Finalize();

  return EXIT_SUCCESS;
}
