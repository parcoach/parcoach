#include "rma_analyzer.h"

#include <mpi.h>

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

using namespace std;
namespace {
map<MPI_Win, std::unique_ptr<rma_analyzer_state>> States;
/******************************************************
 *             RMA Analyzer global state              *
 ******************************************************/

int win_mpi_tag_id[RMA_ANALYZER_MAX_WIN] = {0};

/* Activate or deactivate window filtering. Activated by default. */
int rma_analyzer_filter_window = 1;

/* Checks if there is an active epoch currently profiled by the RMA
 * Analyzer for the specified window. Used by LOAD/STORE overloading
 * routines to filter unneeeded registering */
int rma_analyzer_is_active_epoch(MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);
  return (state->active_epoch > 0);
}

/* Save the interval given in parameter so that future intersections
 * can be detected. Returns 1 if the interval has been saved, 0
 * otherwise: this means that the interval has been filtered out. */
int rma_analyzer_save_interval(Interval *itv, MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  if ((!rma_analyzer_filter_window) ||
      ((get_low_bound(itv) >= state->win_base) &&
       (get_up_bound(itv) < (state->win_base + state->win_size)))) {
    scoped_lock Lock(state->ListMutex);

    RMA_DEBUG({
      cerr << "overlap search for:\n";
      print_interval(*itv);
    });
    Interval *overlap_itv = overlap_search(state->local_tree, itv);
    if (overlap_itv != NULL) {
      RMA_DEBUG(
          cerr << "Error : there is an intersection between the intervals in "
                  "this List!!\n");
      /* TODO: If you want to end the program instead of just printing an
       * error, this is here that you should do it */
      if (overlap_itv != NULL) {
        Access_type access_type = get_access_type(itv);
        char *filename = itv->filename;
        int fileline = get_fileline(itv);
        Access_type overlap_access_type = get_access_type(overlap_itv);
        char *overlap_filename = overlap_itv->filename;
        int overlap_fileline = get_fileline(overlap_itv);
        ostringstream Err;
        Err << "Error when inserting memory access of type "
            << access_type_to_str(access_type) << " from file " << filename
            << " at line " << fileline << " "
            << "with already inserted interval of type "
            << access_type_to_str(overlap_access_type) << " from file "
            << overlap_filename << " at line " << overlap_fileline << ".\n"
            << "The program will be exiting now with MPI_Abort.\n";
        // Emit the error all at once.
        cerr << Err.str();
      }
      MPI_Abort(MPI_COMM_WORLD, 1);
    } else {
      RMA_DEBUG(
          cerr << "Fine, there is no intersection between the intervals in "
                  "this List !\n");
    }

    if (state->local_tree == NULL) {
      state->local_tree = new_interval_tree(itv);
    } else {
      insert_interval_tree(state->local_tree, itv);
    }

    return 1;
  }

  /* The interval has been filtered out */
  return 0;
}

/* This routine is only to be called by the communication checking
 * thread. It checks if an interval has landed locally, and returns
 * only if an interval has been received. On return, the received_itv
 * parameter has been filled with a new interval.
 * Returns true if the thread should exit. */
bool rma_analyzer_check_communication(Interval *received_itv, MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  int nb_loops = 0;
  int mpi_flag = 0;
  MPI_Request mpi_request;

  MPI_Irecv(received_itv, 1, state->interval_datatype, MPI_ANY_SOURCE,
            state->mpi_tag + state->count_epoch, state->win_comm, &mpi_request);

  while (mpi_flag == 0) {
    if ((state->thread_end == 1) && (state->count == state->value)) {
      MPI_Cancel(&mpi_request);
      state->count = 0;
      return true;
    }

    /* Yield the thread when looping too much to reduce the pressure on
     * application threads, and the overhead */
    if (nb_loops >= 100) {
      nb_loops = 0;
      sched_yield();
    }
    nb_loops++;

    MPI_Test(&mpi_request, &mpi_flag, MPI_STATUS_IGNORE);
  }
  state->count++;
  mpi_flag = 0;
  return false;
}

/* This function is needed to receive the intervals and do comparaisons */
void rma_analyzer_comm_check_thread(MPI_Win win) {
  uint64_t low_bound, up_bound;
  Access_type access_type;
  int fileline, ret;

  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  while (1) {
    /* This interval is dedicated to receive intervals from other
     * processes during the whole lifetime of the thread. */

    Interval *received = new Interval;
    if (rma_analyzer_check_communication(received, win)) {
      return;
    }

    low_bound = get_low_bound(received);
    up_bound = get_up_bound(received);
    access_type = get_access_type(received);
    char *filename = received->filename;
    fileline = get_fileline(received);

    /* Create the interval to add in the list.
     * Add the window offset to the received interval to match the local
     * pointers addresses and find conflicts between remote and local
     * accesses */
    Interval *saved_interval =
        create_interval(low_bound + state->win_base, up_bound + state->win_base,
                        access_type, fileline, filename);

    ret = rma_analyzer_save_interval(saved_interval, win);

    if (!ret) {
      delete saved_interval;
    }

    // print_interval_list(state->local_list);
  }
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

} // namespace

/******************************************************
 *    RMA Analyzer MPI Fortran/C wrapper routines     *
 ******************************************************/

/******************************************************
 *             RMA Analyzer routines                  *
 ******************************************************/

/* Get the RMA analyzer state associated to the window */
extern "C" rma_analyzer_state *rma_analyzer_get_state(MPI_Win win) {
  auto &State = States[win];

  if (!State) {
    RMA_DEBUG(cerr << "Error : no state found for " << (void *)win << " in "
                   << __func__ << ", exiting now\n");
    exit(EXIT_FAILURE);
  }

  return State.get();
}

/* Save the interval given in parameter in all active windows.
 * Especially used for load and store instructions */
// static int count = 0;
extern "C" void rma_analyzer_save_interval_all_wins(uint64_t address,
                                                    uint64_t size,
                                                    Access_type access_type,
                                                    int fileline,
                                                    char *filename) {
  // count ++;
  // if(count == 1000000)
  //{
  //  printf(".");
  //  count = 0;
  // }
  RMA_DEBUG(cerr << "rma_save_interval_all_wins\n");
  for (auto &[_, State] : States) {
    if (rma_analyzer_is_active_epoch(State->state_win)) {
      Interval *saved_itv = create_interval(address, address + size,
                                            access_type, fileline, filename);
      RMA_DEBUG(print_interval(*saved_itv));
      int ret = rma_analyzer_save_interval(saved_itv, State->state_win);

      if (!ret) {
        delete saved_itv;
      }
      // print_interval_list(current->local_list);
    }
  }
}

/* This routine initializes the state variables needed for the
 * communication checking thread to work and starts it. */
extern "C" void rma_analyzer_init_comm_check_thread(MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  for (int i = 0; i < state->size_comm; i++) {
    state->array[i] = 0;
  }
  state->thread_end = 0;
  state->count_epoch++;
  state->active_epoch = 1;

  state->Thread = thread(&rma_analyzer_comm_check_thread, win);
}

/* This routine clears the state variables needed for the
 * communication checking thread to work. It should be called after
 * any call to pthread_join() for the communication checking thread. */
extern "C" void rma_analyzer_clear_comm_check_thread(int do_reduce,
                                                     MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  int my_rank;
  MPI_Comm_rank(state->win_comm, &my_rank);

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
      MPI_Reduce(&(state->array[i]), (void *)(&state->value), 1, MPI_INT,
                 MPI_SUM, i, state->win_comm);
    }
  } else {
    state->value = 0;
  }

  LOG(stderr, "I'm process %d I expect %d comms\n", my_rank, state->value);

  /* Switch the flag to check the end of the thread, and wait for it */
  state->thread_end = 1;
  state->Thread.join();
  LOG(stderr, "I passed the pthread join\n");

  /* Clear the state of the communication checking thread */
  state->count = 0;
  state->active_epoch = 0;

  // print_interval_tree_stats(state->local_tree);
  // in_order_print_tree(state->local_tree);
  free_interval_tree(state->local_tree);
  state->local_tree = NULL;
}

/* This routine initializes the state variables needed for the communication
 * checking thread to work and starts it on all windows that have been cleared
 * by a synchronization. This is particularly used for in-window
 * synchronizations that are not attached to a specific window, such as
 * MPI_Barrier.  */
extern "C" void rma_analyzer_init_comm_check_thread_all_wins() {
  for (auto &[_, State] : States) {
    /* Only restarts if the epoch has not been really stopped, but the flag has
     * been flipped by a synchronization inside the epoch */
    if ((0 == rma_analyzer_is_active_epoch(State->state_win)) &&
        (1 == State->from_sync)) {
      rma_analyzer_init_comm_check_thread(State->state_win);
    }
  }
}

/* This routine clears the state variables needed for the communication
 * checking thread to work on all windows. This is particularly used for
 * in-window synchronizations that are not attached to a specific window, such
 * as Barrier.  */
extern "C" void rma_analyzer_clear_comm_check_thread_all_wins(int do_reduce) {
  for (auto &[_, State] : States) {
    if (rma_analyzer_is_active_epoch(State->state_win)) {
      rma_analyzer_clear_comm_check_thread(do_reduce, State->state_win);
      State->from_sync = 1;
    }
  }
}

/* This routine takes care of the update of the local list with the
 * new interval and the sending of the detected interval to the remote
 * peer */
extern "C" void rma_analyzer_update_on_comm_send(
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
  RMA_DEBUG(print_interval(*local_itv));

  rma_analyzer_save_interval(local_itv, win);
  // print_interval_list(state->local_list);

  /* The remote data race detection begins here */
  Interval *target_itv =
      create_interval(target_disp, target_disp + target_size - 1,
                      target_access_type, fileline, filename);
  RMA_DEBUG(print_interval(*target_itv));

  // print_interval(*target_itv);
  /* Send the struct Interval with interval_datatype MPI datatype to the target
   */
  MPI_Send(target_itv, 1, state->interval_datatype, target_rank,
           state->mpi_tag + state->count_epoch, state->win_comm);

  /* Increment number of communications sent to the target */
  state->array[target_rank]++;
}

/* Initialize the RMA analyzer */
extern "C" void rma_analyzer_start(void *base, MPI_Aint size, MPI_Comm comm,
                                   MPI_Win *win) {
  LOG(stderr, "Starting RMA analyzer\n");

  /* Check if filtering of the window has been deactivated by env */
  char *tmp = getenv("RMA_ANALYZER_FILTER_WINDOW");
  if (tmp) {
    rma_analyzer_filter_window = atoi(tmp);
  }

  unique_ptr<rma_analyzer_state> new_state = make_unique<rma_analyzer_state>();

  new_state->state_win = *win;
  new_state->win_base = (uint64_t)base;
  new_state->win_size = (size_t)size;
  new_state->win_comm = comm;
  MPI_Comm_size(new_state->win_comm, &(new_state->size_comm));
  new_state->array = new int[new_state->size_comm];

  new_state->local_tree = nullptr;

  /* Create the MPI Datatype needed to exchange intervals */
  int struct_size = sizeof(Interval);
  LOG(stderr, "Creating type: %i, %p, %p", struct_size, (void *)MPI_BYTE,
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

  LOG(stderr, "New state window added for window %p\n",
      (void *)new_state->state_win);
  States.emplace(*win, std::move(new_state));
}

/* Free the resources used by the RMA analyzer */
extern "C" void rma_analyzer_stop(MPI_Win win) {
  LOG(stderr, "Getting state in %s\n", __func__);
  // If we've reached that point, no conflicts have been detected.
  printf("PARCOACH: stopping RMA analyzer, no issues found.\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  rma_analyzer_free_tag_range(state->mpi_tag);
  delete[] state->array;
  MPI_Type_free(&state->interval_datatype);
  States.erase(win);
}
