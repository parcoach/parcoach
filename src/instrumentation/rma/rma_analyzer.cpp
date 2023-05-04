#include "rma_analyzer.h"

#include "Interval.h"
#include "util.h"

#include <mpi.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

using namespace std;
using namespace parcoach::rma;
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
int rma_analyzer_save_interval(parcoach::rma::Access &&Acc, MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);
  RMA_DEBUG({
    Err << "Access " << Acc << " for window: " << win << "\n";
    cerr << Err.str();
  });
  scoped_lock Lock(state->ListMutex);
  auto Conflicting = state->getConflictingIntervals(Acc);
  for (Access const &Found : Conflicting) {
    RMA_DEBUG({
      Err << "Interval " << Acc << " conflicts with " << Found << "\n";
      cerr << Err.str();
    });
    ostringstream Err;
    Err << "Error when inserting memory access of type " << Acc.Type
        << " from file " << Acc.Dbg
        << " with already inserted interval of type " << Found.Type
        << " from file " << Found.Dbg << ".\n"
        << "The program will be exiting now with MPI_Abort.\n";
    // Emit the error all at once.
    cerr << Err.str();
  }
  if (!Conflicting.empty()) {
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  RMA_DEBUG({
    Err << "OK, inserting interval " << Acc << "\n";
    Err << "In all intervals:\n";
    for (auto &I : state->Intervals) {
      Err << I << "\n";
    }
    Err << "===\n";
    cerr << Err.str();
  });
  state->Intervals.emplace(Acc);
  return 1;
}

/* This routine is only to be called by the communication checking
 * thread. It checks if an interval has landed locally, and returns
 * only if an interval has been received. On return, the received_itv
 * parameter has been filled with a new interval.
 * Returns true if the thread should exit. */
optional<Access> rma_analyzer_check_communication(MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  int nb_loops = 0;
  int mpi_flag = 0;
  MPI_Request mpi_request;
  Access ReceivedInterval;

  MPI_Irecv(&ReceivedInterval, 1, state->interval_datatype, MPI_ANY_SOURCE,
            state->mpi_tag + state->count_epoch, state->win_comm, &mpi_request);

  while (mpi_flag == 0) {
    if ((state->thread_end == 1) && (state->count == state->value)) {
      MPI_Cancel(&mpi_request);
      state->count = 0;
      return {};
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
  // Fix the interval by adding the base address.
  RMA_DEBUG(cerr << "Received(beforefix): " << ReceivedInterval << "\n");
  ReceivedInterval.Itv.fixForWindow(state->win_base);
  RMA_DEBUG(cerr << "Received(afterfix): " << ReceivedInterval << "\n");
  return ReceivedInterval;
}

/* This function is needed to receive the intervals and do comparaisons */
void rma_analyzer_comm_check_thread(MPI_Win win) {
  while (1) {
    auto Received = rma_analyzer_check_communication(win);
    if (!Received) {
      return;
    }

    rma_analyzer_save_interval(std::move(*Received), win);
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

IntervalViewContainer
rma_analyzer_state::getIntersectingIntervals(Access const &A) const {
  IntervalViewContainer Ret;
  // Find the first neighbor > A
  auto ItNeighbor = Intervals.upper_bound(A);
  // Add neighbors from It to End, exit early when finding a non-intersecting.
  auto AddNeighbors = [&](auto It, auto End) {
    while (It != End) {
      if (It->Itv.intersects(A.Itv)) {
        Ret.insert(std::ref(*It));
      } else {
        // We found the first neighbors non-intersecting, we can exit the loop.
        break;
      }
      ++It;
    }
  };
  AddNeighbors(ItNeighbor, Intervals.end());
  AddNeighbors(make_reverse_iterator(ItNeighbor), Intervals.rend());
  return Ret;
}

IntervalViewContainer
rma_analyzer_state::getConflictingIntervals(Access const &A) const {
  IntervalViewContainer Ret;
  for (Access const &Current : getIntersectingIntervals(A)) {
    if (Current.conflictsWith(A)) {
      Ret.insert(std::ref(Current));
    }
  }
  return Ret;
}

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
extern "C" void rma_analyzer_save_interval_all_wins(Access Acc) {
  // count ++;
  // if(count == 1000000)
  //{
  //  printf(".");
  //  count = 0;
  // }
  RMA_DEBUG(cerr << "rma_save_interval_all_wins\n");
  for (auto &[_, State] : States) {
    if (rma_analyzer_is_active_epoch(State->state_win)) {
      RMA_DEBUG(cerr << "saving interval to all win: " << Acc << "\n");
      rma_analyzer_save_interval(std::move(Acc), State->state_win);
    }
  }
}

/* This routine initializes the state variables needed for the
 * communication checking thread to work and starts it. */
extern "C" void rma_analyzer_init_comm_check_thread(MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
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
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  int my_rank;
  MPI_Comm_rank(state->win_comm, &my_rank);

  RMA_DEBUG(cerr << "My work stops here !\n");
  for (int i = 0; i < state->size_comm; i++) {
    RMA_DEBUG(cerr << "Array elements: " << state->array[i] << "\n");
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

  RMA_DEBUG(cerr << "I'm process " << my_rank << " I expect " << state->value
                 << " comms.\n");

  /* Switch the flag to check the end of the thread, and wait for it */
  state->thread_end = 1;
  state->Thread.join();
  RMA_DEBUG(cerr << "I passed the pthread join\n");

  /* Clear the state of the communication checking thread */
  state->count = 0;
  state->active_epoch = 0;
  state->Intervals.clear();
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
extern "C" void rma_analyzer_update_on_comm_send(Access LocalAccess,
                                                 Access TargetAccess,
                                                 int target_rank, MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  RMA_DEBUG(cerr << "LocalInterval: " << LocalAccess << "\n");

  rma_analyzer_save_interval(std::move(LocalAccess), win);

  RMA_DEBUG(cerr << "TargetInterval: " << TargetAccess << "\n");

  // print_interval(*target_itv);
  /* Send the struct Interval with interval_datatype MPI datatype to the target
   */
  MPI_Send(&TargetAccess, 1, state->interval_datatype, target_rank,
           state->mpi_tag + state->count_epoch, state->win_comm);

  /* Increment number of communications sent to the target */
  state->array[target_rank]++;
}

/* Initialize the RMA analyzer */
extern "C" void rma_analyzer_start(void *base, MPI_Aint size, MPI_Comm comm,
                                   MPI_Win *win) {
  RMA_DEBUG(cerr << "Starting RMA analyzer\n");

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

  /* Create the MPI Datatype needed to exchange intervals */
  int struct_size = sizeof(Access);
  RMA_DEBUG(cerr << "Creating type: " << struct_size << ", " << (void *)MPI_BYTE
                 << ", " << &(new_state->interval_datatype) << "\n");
  MPI_Type_contiguous(struct_size, MPI_BYTE, &(new_state->interval_datatype));
  MPI_Type_commit(&new_state->interval_datatype);

  /* Get the tag range that the RMA analyzer will be able to use for
   * this specific window */
  int tag_id = rma_analyzer_get_tag_range_id();
  if (tag_id < 0) {
    RMA_DEBUG({
      cerr << "Error : no tag to use. Please check if you have allocated more "
              "than "
           << RMA_ANALYZER_MAX_WIN << " windows simultaneously.\n";
    });
  }
  new_state->mpi_tag =
      RMA_ANALYZER_BASE_MPI_TAG + (RMA_ANALYZER_MAX_EPOCH_NUMBER * tag_id);
  RMA_DEBUG(cerr << "Got tag " << new_state->mpi_tag << "\n");

  new_state->value = 0;
  new_state->thread_end = 0;
  new_state->count_epoch = 0;
  new_state->count_fence = 0;
  new_state->count = 0;
  new_state->active_epoch = 0;
  new_state->from_sync = 0;

  RMA_DEBUG(cerr << "New state window added for window "
                 << (void *)new_state->state_win
                 << ". Win addr: " << new_state->win_base << " ("
                 << (void *)new_state->win_base << ").\n");
  States.emplace(*win, std::move(new_state));

  RMA_DEBUG({
    int r;
    MPI_Comm_rank(comm, &r);
    cerr << "[" << r << "] States address: " << &States << "\n";
  });
}

/* Free the resources used by the RMA analyzer */
extern "C" void rma_analyzer_stop(MPI_Win win) {
  RMA_DEBUG(cerr << "Getting state in " << __func__ << "\n");
  // If we've reached that point, no conflicts have been detected.
  printf("PARCOACH: stopping RMA analyzer, no issues found.\n");
  rma_analyzer_state *state = rma_analyzer_get_state(win);

  rma_analyzer_free_tag_range(state->mpi_tag);
  delete[] state->array;
  MPI_Type_free(&state->interval_datatype);
  States.erase(win);
}
