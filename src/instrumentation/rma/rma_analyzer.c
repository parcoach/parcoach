#include "rma_analyzer.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <mpi.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// #define USE_LIST 1
#define USE_TREE 1

/******************************************************
 *             RMA Analyzer global state              *
 ******************************************************/

rma_analyzer_state *state_htbl = NULL;
static int win_mpi_tag_id[RMA_ANALYZER_MAX_WIN] = {0};

/* Programming language used by the origin program */
static Lang language_program;

/* Activate or deactivate window filtering. Activated by default. */
static int rma_analyzer_filter_window = 1;

/******************************************************
 *    RMA Analyzer MPI Fortran/C wrapper routines     *
 ******************************************************/

int fc_MPI_Comm_size(MPI_Comm comm, int *size) {
  if (language_program == C)
    return MPI_Comm_size(comm, size);
  else // language_program == FORT
  {
    /* Since the communicator has been created in the Fortran program, we need
     * to convert it */
    MPI_Comm c_comm = MPI_Comm_f2c(comm);
    return MPI_Comm_size(c_comm, size);
  }
}

int fc_MPI_Comm_rank(MPI_Comm comm, int *rank) {
  if (language_program == C)
    return MPI_Comm_rank(comm, rank);
  else // language_program == FORT
  {
    /* Since the communicator has been created in the Fortran program, we need
     * to convert it */
    MPI_Comm c_comm = MPI_Comm_f2c(comm);
    return MPI_Comm_rank(c_comm, rank);
  }
}

int fc_MPI_Cancel(MPI_Request *request) {
  /* Since the request is created by the C library, no action needed */
  return MPI_Cancel(request);
}

int fc_MPI_Test(MPI_Request *request, int *flag, MPI_Status *status) {
  /* Since the request and the status are both created by the C library, no
   * action needed */
  return MPI_Test(request, flag, status);
}

int fc_MPI_Send(const void *buf, int count, MPI_Datatype datatype, int dest,
                int tag, MPI_Comm comm) {
  if (language_program == C)
    return MPI_Send(buf, count, datatype, dest, tag, comm);
  else // language_program == FORT
  {
    /* Since the communicator has been created in the Fortran program, we need
     * to convert it */
    MPI_Comm c_comm = MPI_Comm_f2c(comm);
    return MPI_Send(buf, count, datatype, dest, tag, c_comm);
  }
}

int fc_MPI_Irecv(void *buf, int count, MPI_Datatype datatype, int source,
                 int tag, MPI_Comm comm, MPI_Request *request) {
  if (language_program == C)
    return MPI_Irecv(buf, count, datatype, source, tag, comm, request);
  else // language_program == FORT
  {
    /* Since the communicator has been created in the Fortran program, we need
     * to convert it */
    MPI_Comm c_comm = MPI_Comm_f2c(comm);
    return MPI_Irecv(buf, count, datatype, source, tag, c_comm, request);
  }
}

int fc_MPI_Reduce(const void *sendbuf, void *recvbuf, int count,
                  MPI_Datatype datatype, MPI_Op op, int root, MPI_Comm comm) {
  if (language_program == C)
    return MPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, comm);
  else // language_program == FORT
  {
    /* Since the communicator has been created in the Fortran program, we need
     * to convert it */
    MPI_Comm c_comm = MPI_Comm_f2c(comm);
    return MPI_Reduce(sendbuf, recvbuf, count, datatype, op, root, c_comm);
  }
}

/******************************************************
 *             RMA Analyzer routines                  *
 ******************************************************/

/* Get the RMA analyzer state associated to the window */
rma_analyzer_state *rma_analyzer_get_state(MPI_Win win) {
  rma_analyzer_state *current, *next;
  rma_analyzer_state *state = NULL;
  HASH_ITER(hh, state_htbl, current, next) {
    if (current->state_win == win) {
      state = current;
    }
  }
  /*HASH_FIND_PTR(state_htbl, &win, state);*/
  if (NULL == state) {
    LOG(stderr, "Error : no state found for %i in %s, exiting now\n", win,
        __func__);
    exit(EXIT_FAILURE);
  }

  return state;
}

/* Checks if there is an active epoch currently profiled by the RMA
 * Analyzer for the specified window. Used by LOAD/STORE overloading
 * routines to filter unneeeded registering */
static int rma_analyzer_is_active_epoch(MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);
  return (state->active_epoch > 0);
}

/* Save the interval given in parameter so that future intersections
 * can be detected. Returns 1 if the interval has been saved, 0
 * otherwise: this means that the interval has been filtered out. */
int rma_analyzer_save_interval(Interval *itv, MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  if ((!rma_analyzer_filter_window) ||
      ((get_low_bound(itv) >= state->win_base) &&
       (get_up_bound(itv) < (state->win_base + state->win_size)))) {
    pthread_mutex_lock(&state->list_mutex);

    Interval *overlap_itv = NULL;
#ifdef USE_TREE
    overlap_itv = overlap_search(state->local_tree, itv);
    if (overlap_itv != NULL)
#else
    if (if_intersects_interval_list(*itv, state->local_list))
#endif
    {
      LOG(stderr, "Error : there is an intersection between the intervals in "
                  "this List!!\n");
      /* TODO: If you want to end the program instead of just printing an
       * error, this is here that you should do it */
      if (overlap_itv != NULL) {
        int access_type = get_access_type(itv);
        char *filename = get_filename(itv);
        int fileline = get_fileline(itv);
        int overlap_access_type = get_access_type(overlap_itv);
        char *overlap_filename = get_filename(overlap_itv);
        int overlap_fileline = get_fileline(overlap_itv);
        fprintf(stderr, "Error when inserting memory access of type %s ",
                access_type_to_str(access_type));
        fprintf(stderr, "from file %s at line %d ", filename, fileline);
        fprintf(stderr, "with already inserted interval of type %s ",
                access_type_to_str(overlap_access_type));
        fprintf(stderr, "from file %s at line %d.\n", overlap_filename,
                overlap_fileline);
        fprintf(stderr, "The program will be exiting now with MPI_Abort.\n");
      }
      MPI_Abort(MPI_COMM_WORLD, 1);
    } else {
      LOG(stderr, "Fine, there is no intersection between the intervals in "
                  "this List !\n");
    }

#ifdef USE_TREE
    if (state->local_tree == NULL)
      state->local_tree = new_interval_tree(itv);
    else
      insert_interval_tree(state->local_tree, itv);
#else
    state->local_list = insert_interval_head(state->local_list, *itv);
#endif

    pthread_mutex_unlock(&state->list_mutex);

    return 1;
  }

  /* The interval has been filtered out */
  return 0;
}

/* Save the interval given in parameter in all active windows.
 * Especially used for load and store instructions */
// static int count = 0;
void rma_analyzer_save_interval_all_wins(uint64_t address, uint64_t size,
                                         Access_type access_type, int fileline,
                                         char *filename) {
  rma_analyzer_state *current, *next;
  int ret;
  // count ++;
  // if(count == 1000000)
  //{
  //  printf(".");
  //  count = 0;
  // }
  HASH_ITER(hh, state_htbl, current, next) {
    if (rma_analyzer_is_active_epoch(current->state_win)) {
      Interval *saved_itv = create_interval(address, address + size,
                                            access_type, fileline, filename);

      ret = rma_analyzer_save_interval(saved_itv, current->state_win);

      if (!ret)
        free(saved_itv);
      // print_interval_list(current->local_list);
    }
  }
}

/* This routine is only to be called by the communication checking
 * thread. It checks if an interval has landed locally, and returns
 * only if an interval has been received. On return, the received_itv
 * parameter has been filled with a new interval. */
void rma_analyzer_check_communication(Interval *received_itv, MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  int nb_loops = 0;
  int mpi_flag = 0;
  MPI_Request mpi_request;

  fc_MPI_Irecv(received_itv, 1, state->interval_datatype, MPI_ANY_SOURCE,
               state->mpi_tag + state->count_epoch, state->win_comm,
               &mpi_request);

  while (mpi_flag == 0) {
    if ((state->thread_end == 1) && (state->count == state->value)) {
      fc_MPI_Cancel(&mpi_request);
      state->count = 0;
      pthread_exit(NULL);
      return;
    }

    /* Yield the thread when looping too much to reduce the pressure on
     * application threads, and the overhead */
    if (nb_loops >= 100) {
      nb_loops = 0;
      sched_yield();
    }
    nb_loops++;

    fc_MPI_Test(&mpi_request, &mpi_flag, MPI_STATUS_IGNORE);
  }
  state->count++;
  mpi_flag = 0;
}

/* This function is needed to receive the intervals and do comparaisons */
void *rma_analyzer_comm_check_thread(void *args) {
  uint64_t low_bound, up_bound;
  Access_type access_type;
  int fileline, ret;
  char *filename = NULL;
  MPI_Win win = *(MPI_Win *)args;
  free(args);

  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  /* This interval is dedicated to receive intervals from other
   * processes during the whole lifetime of the thread. */
  Interval *received = malloc(sizeof(Interval));

  rma_analyzer_check_communication(received, win);

  // LOG(stderr,"MPI process received the first ");
  // print_interval(*received);

  low_bound = get_low_bound(received);
  up_bound = get_up_bound(received);
  access_type = get_access_type(received);
  filename = get_filename(received);
  fileline = get_fileline(received);

  /* Create the interval to add in the list.
   * Add the window offset to the received interval to match the local
   * pointers addresses and find conflicts between remote and local
   * accesses */
  Interval *saved_interval =
      create_interval(low_bound + state->win_base, up_bound + state->win_base,
                      access_type, fileline, filename);

  ret = rma_analyzer_save_interval(saved_interval, win);

  if (!ret)
    free(saved_interval);

  // print_interval_list(state->local_list);

  while (1) {
    rma_analyzer_check_communication(received, win);

    // LOG(stderr,"MPI process received ");
    // print_interval(*received);

    low_bound = get_low_bound(received);
    up_bound = get_up_bound(received);
    access_type = get_access_type(received);
    filename = get_filename(received);
    fileline = get_fileline(received);

    /* Create the interval to add in the list.
     * Add the window offset to the received interval to match the local
     * pointers addresses and find conflicts between remote and local
     * accesses */
    Interval *saved_interval =
        create_interval(low_bound + state->win_base, up_bound + state->win_base,
                        access_type, fileline, filename);

    ret = rma_analyzer_save_interval(saved_interval, win);

    if (!ret)
      free(saved_interval);

    // print_interval_list(state->local_list);
  }
}

/* This routine initializes the state variables needed for the
 * communication checking thread to work and starts it. */
void rma_analyzer_init_comm_check_thread(MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  for (int i = 0; i < state->size_comm; i++) {
    state->array[i] = 0;
  }
  state->thread_end = 0;
  state->count_epoch++;
  state->active_epoch = 1;
  MPI_Win *tmp = malloc(sizeof(MPI_Win));
  *tmp = win;
  (void)pthread_create(&state->threadId, NULL, &rma_analyzer_comm_check_thread,
                       tmp);
}

/* This routine clears the state variables needed for the
 * communication checking thread to work. It should be called after
 * any call to pthread_join() for the communication checking thread. */
void rma_analyzer_clear_comm_check_thread(int do_reduce, MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  int my_rank;
  fc_MPI_Comm_rank(state->win_comm, &my_rank);

  LOG(stderr, "My work stops here !\n");
  for (int i = 0; i < state->size_comm; i++) {
    LOG(stderr, "array elements : %d\n", state->array[i]);
  }

  /* Get the number of expected communications on each process, to be
   * able to detect the termination of communication checking. If
   * specified in the routine call, do the reduce, else just affects
   * zero to the number of communication the thread waits for. */
  if (do_reduce) {
    for (int i = 0; i < state->size_comm; i++) {
      fc_MPI_Reduce(&(state->array[i]), (void *)(&state->value), 1, MPI_INT,
                    MPI_SUM, i, state->win_comm);
    }
  } else {
    state->value = 0;
  }

  LOG(stderr, "I'm process %d I expect %d comms\n", my_rank, state->value);

  /* Switch the flag to check the end of the thread, and wait for it */
  state->thread_end = 1;
  pthread_join(state->threadId, NULL);
  LOG(stderr, "I passed the pthread join\n");

  /* Clear the state of the communication checking thread */
  state->count = 0;
  state->active_epoch = 0;

#ifdef USE_TREE
  // print_interval_tree_stats(state->local_tree);
  // in_order_print_tree(state->local_tree);
  free_interval_tree(state->local_tree);
  state->local_tree = NULL;
#else
  // print_interval_list(state->local_list);
  free_interval_list(&state->local_list);
#endif
}

/* This routine initializes the state variables needed for the communication
 * checking thread to work and starts it on all windows that have been cleared
 * by a synchronization. This is particularly used for in-window
 * synchronizations that are not attached to a specific window, such as
 * MPI_Barrier.  */
void rma_analyzer_init_comm_check_thread_all_wins() {
  rma_analyzer_state *current, *next;
  HASH_ITER(hh, state_htbl, current, next) {
    /* Only restarts if the epoch has not been really stopped, but the flag has
     * been flipped by a synchronization inside the epoch */
    if ((0 == rma_analyzer_is_active_epoch(current->state_win)) &&
        (1 == current->from_sync)) {
      rma_analyzer_init_comm_check_thread(current->state_win);
    }
  }
}

/* This routine clears the state variables needed for the communication
 * checking thread to work on all windows. This is particularly used for
 * in-window synchronizations that are not attached to a specific window, such
 * as Barrier.  */
void rma_analyzer_clear_comm_check_thread_all_wins(int do_reduce) {
  rma_analyzer_state *current, *next;
  HASH_ITER(hh, state_htbl, current, next) {
    if (rma_analyzer_is_active_epoch(current->state_win)) {
      rma_analyzer_clear_comm_check_thread(do_reduce, current->state_win);
      current->from_sync = 1;
    }
  }
}

/* This routine takes care of the update of the local list with the
 * new interval and the sending of the detected interval to the remote
 * peer */
void rma_analyzer_update_on_comm_send(
    uint64_t local_address, uint64_t local_size, uint64_t target_disp,
    uint64_t target_size, int target_rank, Access_type local_access_type,
    Access_type target_access_type, int fileline, char *filename, MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  /* The local data race detection begins here */
  Interval *local_itv =
      create_interval(local_address, local_address + local_size - 1,
                      local_access_type, fileline, filename);

  // LOG(stderr,"local ");
  // print_interval(*local_itv);

  rma_analyzer_save_interval(local_itv, win);
  // print_interval_list(state->local_list);

  /* The remote data race detection begins here */
  Interval *target_itv =
      create_interval(target_disp, target_disp + target_size - 1,
                      target_access_type, fileline, filename);

  // print_interval(*target_itv);
  /* Send the struct Interval with interval_datatype MPI datatype to the target
   */
  fc_MPI_Send(target_itv, 1, state->interval_datatype, target_rank,
              state->mpi_tag + state->count_epoch, state->win_comm);

  /* Increment number of communications sent to the target */
  state->array[target_rank]++;
}

/* This routine returns a new tag range to use for the allocation of a
 * new window */
int rma_analyzer_get_tag_range_id() {
  for (int i = 0; i < RMA_ANALYZER_MAX_WIN; i++) {
    if (win_mpi_tag_id[i] == 0) {
      win_mpi_tag_id[i] = 1;
      return i;
    }
  }

  /* No tag found, return an error as -1 */
  return -1;
}

/* Gives the tag range back to the RMA analyzer to use it for a newly
 * allocated window */
void rma_analyzer_free_tag_range(int tag_id) {
  int id = (tag_id - RMA_ANALYZER_BASE_MPI_TAG) / RMA_ANALYZER_MAX_EPOCH_NUMBER;
  win_mpi_tag_id[id] = 0;
}

/* Initialize the RMA analyzer */
void rma_analyzer_start(void *base, MPI_Aint size, MPI_Comm comm, MPI_Win *win,
                        Lang language) {
  LOG(stderr, "Starting RMA analyzer\n");
  if (language_program == NONE) {
    language_program = language;
  } else {
    assert(language_program == language);
  }

  /* Check if filtering of the window has been deactivated by env */
  char *tmp = getenv("RMA_ANALYZER_FILTER_WINDOW");
  if (tmp) {
    rma_analyzer_filter_window = atoi(tmp);
  }

  rma_analyzer_state *new_state = malloc(sizeof(rma_analyzer_state));

  new_state->state_win = *win;
  new_state->win_base = (uint64_t)base;
  new_state->win_size = (size_t)size;
  new_state->win_comm = comm;
  fc_MPI_Comm_size(new_state->win_comm, &(new_state->size_comm));
  new_state->array = malloc(sizeof(int) * new_state->size_comm);

  pthread_mutex_init(&new_state->list_mutex, NULL);
#ifdef USE_TREE
  new_state->local_tree = NULL;
#else
  new_state->local_list = create_interval_list();
#endif

  /* Create the MPI Datatype needed to exchange intervals */
  int struct_size = sizeof(Interval);
  LOG(stderr, "Creating type: %i, %i, %p", struct_size, MPI_BYTE,
      &(new_state->interval_datatype));
  MPI_Type_contiguous(struct_size, MPI_BYTE, &(new_state->interval_datatype));
  MPI_Type_commit(&new_state->interval_datatype);

  /* Get the tag range that the RMA analyzer will be able to use for
   * this specific window */
  int tag_id = rma_analyzer_get_tag_range_id();
  if (tag_id < 0) {
    LOG(stderr,
        "Error : no tag to use. Please check if you have allocated more than "
        "%d windows simultaneously.\n",
        RMA_ANALYZER_MAX_WIN);
  }
  new_state->mpi_tag =
      RMA_ANALYZER_BASE_MPI_TAG + (RMA_ANALYZER_MAX_EPOCH_NUMBER * tag_id);
  LOG(stderr, "Got tag %d\n", new_state->mpi_tag);

  new_state->value = 0;
  new_state->thread_end = 0;
  new_state->count_epoch = 0;
  new_state->count_fence = 0;
  new_state->count = 0;
  new_state->active_epoch = 0;
  new_state->from_sync = 0;

  HASH_ADD_PTR(state_htbl, state_win, new_state);
  LOG(stderr, "New state window added for window %i\n", new_state->state_win);
}

/* Free the resources used by the RMA analyzer */
void rma_analyzer_stop(MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  rma_analyzer_free_tag_range(state->mpi_tag);
  free(state->array);
  MPI_Type_free(&state->interval_datatype);
  HASH_DEL(state_htbl, state);
}
