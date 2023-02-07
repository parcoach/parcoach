#include "rma_analyzer.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <mpi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static float temps;
static clock_t t1, t2;

/******************************************************
 *     Fortran prototype definitions used in code     *
 ******************************************************/

int mpi_win_create_(int *base, int *size, int *disp_unit, int *info, int *comm,
                    int *win, int *res);
int mpi_win_allocate_(int *size, int *disp_unit, int *info, int *comm,
                      int *baseptr, int *win, int *res);
int mpi_win_lock_all_(int *assert, int *win, int *res);
int mpi_win_lock_(int *lock_type, int *rank, int *assert, int *win, int *res);
int mpi_win_fence_(int *assert, int *win, int *res);
int mpi_win_start_(int *group, int *assert, int *win, int *res);
int mpi_win_post_(int *group, int *assert, int *win, int *res);
int mpi_put_(int *origin_addr, int *origin_count, int *origin_datatype,
             int *target_rank, int *target_disp, int *target_count,
             int *target_datatype, int *win, int *res);
int mpi_get_(int *origin_addr, int *origin_count, int *origin_datatype,
             int *target_rank, int *target_disp, int *target_count,
             int *target_datatype, int *win, int *res);
int mpi_accumulate_(int *origin_addr, int *origin_count, int *origin_datatype,
                    int *target_rank, int *target_disp, int *target_count,
                    int *target_datatype, int *op, int *win, int *res);
int mpi_win_unlock_all_(int *win, int *res);
int mpi_win_unlock_(int *rank, int *win, int *res);
int mpi_win_complete_(int *win, int *res);
int mpi_win_wait_(int *win, int *res);
int mpi_win_free_(int *win, int *res);
int mpi_type_size_(int *datatype, int *size, int *res);
int mpi_win_flush_(int *rank, int *win, int *res);
int mpi_barrier_(int *comm, int *res);

/******************************************************
 *          Beginning of epochs functions             *
 ******************************************************/

/* This function is used to spawn the thread that would excute the
 * communication checking thread function at the win_create stage */
int new_win_create_(int *base, int *size, int *disp_unit, int *info, int *comm,
                    int *win) {
  int ret;
  MPI_Win c_win;
  t1 = clock();

  mpi_win_create_(base, size, disp_unit, info, comm, win, &ret);

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_start((void *)base, (MPI_Aint)size, (MPI_Comm)comm, &c_win,
                     FORT);

  return ret;
}

/* This function is used to spawn the thread that would excute the
 * communication checking thread function at the win_allocate stage */
int new_win_allocate_(int *size, int *disp_unit, int *info, int *comm,
                      int *baseptr, int *win) {
  int ret;
  MPI_Win c_win;
  t1 = clock();

  mpi_win_allocate_(size, disp_unit, info, comm, baseptr, win, &ret);

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_start((void *)baseptr, (MPI_Aint)size, (MPI_Comm)comm, &c_win,
                     FORT);

  return ret;
}

/* Global passive Target synchronization "Lock_all" */
int new_win_lock_all_(int *assert, int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_init_comm_check_thread(c_win);

  mpi_win_lock_all_(assert, win, &ret);
  return ret;
}

/* Passive target synchronization "Lock" */
int new_win_lock_(int *lock_type, int *rank, int *assert, int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_init_comm_check_thread(c_win);

  /* lock_type shared or exclusive */
  mpi_win_lock_(lock_type, rank, assert, win, &ret);
  return ret;
}

/* Active target synchronization "Fence" */
int new_win_fence_(int *assert, int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_state *state = rma_analyzer_get_state(c_win);

  if (state->count_fence > 0) {
    /* This fence is both the end of an epoch and the beginning of a
     * new one. The communication checking thread thus must be cleared
     * properly before starting the new epoch. */
    rma_analyzer_clear_comm_check_thread(DO_REDUCE, c_win);

    /* Ensure everyone has ended the epoch and finished progress
     * threads before reseting resources and creating new thread */
    mpi_win_fence_(0, win, &ret);
  }

  state->count_fence++;
  rma_analyzer_init_comm_check_thread(c_win);

  mpi_win_fence_(assert, win, &ret);
  return ret;
}

/* PSCW active target synchronisation "Win_start" called by the origin */
int new_win_start_(int *group, int *assert, int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_init_comm_check_thread(c_win);

  mpi_win_start_(group, assert, win, &ret);
  return ret;
}

/* PSCW active target synchronisation "Win_post" called by the target */
int new_win_post_(int *group, int *assert, int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_init_comm_check_thread(c_win);

  mpi_win_post_(group, assert, win, &ret);
  return ret;
}

/************************************************
 *      One-sided communication functions       *
 ***********************************************/

/* MPI_Put is used to one-sidedly send data to the window of another
 * process it's used here to retrieve the interval
 * [offset, offset+size[ and send it to the target with MPI_Send */
int new_put_(int *origin_addr, int *origin_count, int *origin_datatype,
             int *target_rank, int *target_disp, int *target_count,
             int *target_datatype, int *win) {
  LOG(stderr, "A process is accessing a put function !\n\n");
  uint64_t local_size = 0;
  uint64_t target_size = 0;

  int ret;
  MPI_Win c_win;

  mpi_type_size_(origin_datatype, (int *)&local_size, &ret);
  local_size *= (uint64_t)origin_count;
  mpi_type_size_(target_datatype, (int *)&target_size, &ret);
  target_size *= (uint64_t)*target_count;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_update_on_comm_send(
      (uint64_t)origin_addr, local_size, (uint64_t)*target_disp, target_size,
      (int)*target_rank, RMA_READ, RMA_WRITE, 0, NULL, c_win);

  mpi_put_(origin_addr, origin_count, origin_datatype, target_rank, target_disp,
           target_count, target_datatype, win, &ret);
  return ret;
}

/* This function is the same as MPI_Put the only difference is that
 * the get permits to one-sidedly fetch data from the window of
 * another MPI process */
int new_get_(int *origin_addr, int *origin_count, int *origin_datatype,
             int *target_rank, int *target_disp, int *target_count,
             int *target_datatype, int *win) {
  LOG(stderr, "A process is accessing a get function !\n\n");
  uint64_t local_size = 0;
  uint64_t target_size = 0;

  int ret;
  MPI_Win c_win;

  mpi_type_size_(origin_datatype, (int *)&local_size, &ret);
  local_size *= (uint64_t)origin_count;
  mpi_type_size_(target_datatype, (int *)&target_size, &ret);
  target_size *= (uint64_t)target_count;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_update_on_comm_send(
      (uint64_t)origin_addr, local_size, (uint64_t)target_disp, target_size,
      (int)*target_rank, RMA_WRITE, RMA_READ, 0, NULL, c_win);

  mpi_get_(origin_addr, origin_count, origin_datatype, target_rank, target_disp,
           target_count, target_datatype, win, &ret);
  return ret;
}

/* An atomic put that does the same as MPI_Put here ! */
int new_accumulate_(int *origin_addr, int *origin_count, int *origin_datatype,
                    int *target_rank, int *target_disp, int *target_count,
                    int *target_datatype, int *op, int *win) {
  LOG(stderr, "A process is accessing an accumulate function !\n\n");
  uint64_t local_size = 0;
  uint64_t target_size = 0;

  int ret;
  MPI_Win c_win;

  mpi_type_size_(origin_datatype, (int *)&local_size, &ret);
  local_size *= (uint64_t)origin_count;
  mpi_type_size_(target_datatype, (int *)&target_size, &ret);
  target_size *= (uint64_t)target_count;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_update_on_comm_send(
      (uint64_t)origin_addr, local_size, (uint64_t)target_disp, target_size,
      (int)*target_rank, RMA_READ, RMA_WRITE, 0, NULL, c_win);

  mpi_accumulate_(origin_addr, origin_count, origin_datatype, target_rank,
                  target_disp, target_count, target_datatype, op, win, &ret);
  return ret;
}

/********************************************
 *            End of epochs                 *
 ********************************************/

/* Global Passive target synchronisation "Unlock_all" called by all
 * the processus */
int new_win_unlock_all_(int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_clear_comm_check_thread(DO_REDUCE, c_win);

  mpi_win_unlock_all_(win, &ret);
  return ret;
}

/* Passive target Synchronization "Unlock" */
int new_win_unlock_(int *rank, int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_clear_comm_check_thread(DO_REDUCE, c_win);

  mpi_win_unlock_(rank, win, &ret);
  return ret;
}

/* PSCW active synchronisation "Win_complete" called by the origin */
int new_win_complete_(int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_clear_comm_check_thread(DO_REDUCE, c_win);

  mpi_win_complete_(win, &ret);
  return ret;
}

/* PSCW active synchronisation "Win_wait" called by the target */
int new_win_wait_(int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_clear_comm_check_thread(DO_REDUCE, c_win);

  mpi_win_wait_(win, &ret);
  return ret;
}

/* Free memory */
int new_win_free_(int *win) {
  int ret;
  MPI_Win c_win;

  c_win = MPI_Win_f2c(*win);
  rma_analyzer_state *state = rma_analyzer_get_state(c_win);

  if (state->count_fence > 0) {
    /* We need to free here the last thread created by the Fence
     * call, because there is no way that we can know in the RMA
     * Analyzer which Fence is the last one of the lifetime of the
     * window. We enforce 0 in the clear call to avoid waiting on
     * non-existent communications. */
    rma_analyzer_clear_comm_check_thread(0, c_win);
  }

  t2 = clock();
  temps = (float)(t2 - t1) / CLOCKS_PER_SEC;
  LOG(stderr, "time = %f\n", temps);
  LOG(stderr, "I passed the win_free\n");
  LOG(stderr, "time = %f\n", temps);

  rma_analyzer_stop(c_win);
  mpi_win_free_(win, &ret);
  return ret;
}

int new_win_flush_(int *rank, int *win) {
  int ret;
  mpi_win_flush_(rank, win, &ret);
  return ret;
}

/********************************************
 *        Synchronization handling          *
 ********************************************/

int new_barrier_(int *comm) {
  int ret;

  /* For Barrier call, we need to reset and restart the state of all active
   * windows */
  rma_analyzer_clear_comm_check_thread_all_wins(DO_REDUCE);
  rma_analyzer_init_comm_check_thread_all_wins();

  mpi_barrier_(comm, &ret);
  return ret;
}
