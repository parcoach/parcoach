#ifndef __RMA_ANALYZER__H__
#define __RMA_ANALYZER__H__

#include "interval.h"
#include "interval_tree.h"
#include <mpi.h>

#include <limits>
#include <mutex>
#include <thread>

#define RMA_ANALYZER_BASE_MPI_TAG std::numeric_limits<unsigned short>::max()
/* Maximum number of epochs during the lifetime of a window */
#define RMA_ANALYZER_MAX_EPOCH_NUMBER 1024
/* Maximum number of windows supported simultaneously */
#define RMA_ANALYZER_MAX_WIN 32

#define DO_REDUCE 1

/* This structure contains the global state of the RMA analyzer
 * associated to a specific window */
struct rma_analyzer_state {
  /* Key of RMA analyzer state hash table */
  MPI_Win state_win;
  std::thread Thread;
  std::mutex ListMutex;
  uint64_t win_base;
  size_t win_size;
  MPI_Comm win_comm;
  MPI_Datatype interval_datatype;
  Interval_tree *local_tree;
  int size_comm;
  int mpi_tag;
  int *array;
  int volatile value;
  int volatile thread_end;
  int volatile count_epoch;
  int volatile count_fence;
  int volatile count;
  int volatile active_epoch;
  int volatile from_sync;
};

extern "C" {

/* Get the RMA analyzer state associated to the window */
rma_analyzer_state *rma_analyzer_get_state(MPI_Win win);

/* This routine initializes the state variables needed for the
 * communication checking thread to work and starts it. */
void rma_analyzer_init_comm_check_thread(MPI_Win win);

/* This routine clears the state variables needed for the
 * communication checking thread to work. It should be called after
 * any call to pthread_join(); for the communication checking thread. */
void rma_analyzer_clear_comm_check_thread(int do_reduce, MPI_Win win);

/* This routine initializes the state variables needed for the communication
 * checking thread to work and starts it on all windows that have been cleared
 * by a synchronization. This is particularly used for in-window
 * synchronizations that are not attached to a specific window, such as
 * MPI_Barrier.  */
void rma_analyzer_init_comm_check_thread_all_wins();

/* This routine clears the state variables needed for the communication
 * checking thread to work on all windows. This is particularly used for
 * in-window synchronizations that are not attached to a specific window, such
 * as Barrier.  */
void rma_analyzer_clear_comm_check_thread_all_wins(int do_reduce);

/* This routine takes care of the update of the local list with the
 * new interval and the sending of the detected interval to the remote
 * peer */
void rma_analyzer_update_on_comm_send(
    uint64_t local_address, uint64_t local_size, uint64_t target_disp,
    uint64_t target_size, int target_rank, Access_type local_access_type,
    Access_type target_access_type, int fileline, char *filename, MPI_Win win);

/* Save the interval in all active windows.
 * Especially used for load and store instructions */
void rma_analyzer_save_interval_all_wins(uint64_t address, uint64_t size,
                                         Access_type access_type, int fileline,
                                         char *filename);

/* Initialize the RMA analyzer */
void rma_analyzer_start(void *base, MPI_Aint size, MPI_Comm comm, MPI_Win *win);

/* Free the resources used by the RMA analyzer */
void rma_analyzer_stop(MPI_Win win);
}

#endif // __RMA_ANALYZER_H__
