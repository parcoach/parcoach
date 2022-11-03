#include "Collectives.h"
#include "../aSSA/Options.h"

/*
 * MPI COLLECTIVES
 */

struct CollCell {
  const char *name;
  int arg_id;
};

std::vector<CollCell> MPI_v_coll = {
    {"MPI_Barrier", 0},
    {"MPI_Comm_split", 0},
    {"MPI_Comm_create", 0},
    {"MPI_Comm_dup", 0},
    {"MPI_Comm_dup_with_info", 0},
    {"MPI_Ibarrier", 0},
    {"MPI_Bcast", 4},
    {"MPI_Ibcast", 4},
    {"MPI_Allreduce", 5},
    {"MPI_Reduce_scatter", 5},
    {"MPI_Reduce_scatter_block", 5},
    {"MPI_Scan", 5},
    {"MPI_Exscan", 5},
    {"MPI_Iallreduce", 5},
    {"MPI_Ireduce_scatter_block", 5},
    {"MPI_Ireduce_scatter", 5},
    {"MPI_Iscan", 5},
    {"MPI_Iexscan", 5},
    {"MPI_Reduce", 6},
    {"MPI_Ireduce", 6},
    {"MPI_Allgather", 6},
    {"MPI_Alltoall", 6},
    {"MPI_Iallgather", 6},
    {"MPI_Ialltoall", 6},
    {"MPI_Scatter", 7},
    {"MPI_Gather", 7},
    {"MPI_Igather", 7},
    {"MPI_Allgatherv", 7},
    {"MPI_Iscatter", 7},
    {"MPI_Iallgatherv", 7},
    {"MPI_Scatterv", 8},
    {"MPI_Gatherv", 8},
    {"MPI_Alltoallv", 8},
    {"MPI_Alltoallw", 8},
    {"MPI_Igatherv", 8},
    {"MPI_Iscatterv", 8},
    {"MPI_Ialltoallv", 8},
    {"MPI_Ialltoallw", 8},
    {"MPI_Finalize", -1},
};

int Com_arg_id(int color) {
  return MPI_v_coll[color].arg_id;
  /*MPI_Barrier=0,MPI_Bcast=4,MPI_Scatter=7,MPI_Scatterv=8,MPI_Gather=7,
    MPI_Gatherv=8, MPI_Allgather=6, MPI_Allgatherv=7, MPI_Alltoall=6,
    MPI_Alltoallv=8, MPI_Alltoallw=8, MPI_Reduce=6, MPI_Allreduce=5,
    MPI_Reduce_scatter=5, MPI_Reduce_scatter_block=5, MPI_Scan=5, MPI_Exscan=5,
    MPI_Comm_split=0, MPI_Comm_create=0,
    MPI_Comm_dup=0, MPI_Comm_dup_with_info=0, MPI_Ibarrier=0, MPI_Igather=7,
    MPI_Igatherv=8, MPI_Iscatter=7, MPI_Iscatterv=8, MPI_Iallgather=6,
    MPI_Iallgatherv=7, MPI_Ialltoall=6, MPI_Ialltoallv=8, MPI_Ialltoallw=8,
    MPI_Ireduce=6, MPI_Iallreduce=5, MPI_Ireduce_scatter_block=5,
    MPI_Ireduce_scatter=5, MPI_Iscan=5, MPI_Iexscan=5,MPI_Ibcast=4,
    MPI_Finalize=-1
  */
}

/*
 * OMP COLLECTIVES
 */

std::vector<const char *> OMP_v_coll = {"__kmpc_barrier"};

/*
 * UPC COLLECTIVES - in upcr_collective.h
 */

std::vector<const char *> UPC_v_coll = {
    "_upcr_wait",         "_upcr_all_broadcast", "_upcr_all_reduceD",
    "_upcr_all_gather",   "_upcr_all_scatter",   "_upcr_all_scatter",
    "_upcr_all_reduceC",  "_upcr_all_reduceUC",  "_upcr_all_reduceS",
    "_upcr_all_reduceUS", "_upcr_all_reduceI",   "_upcr_all_reduceUI",
    "_upcr_all_reduceL",  "_upcr_all_reduceUL",  "_upcr_all_reduceF",
    "_upcr_all_reduceLD"};

/*
 * CUDA COLLECTIVES
 */

std::vector<const char *> CUDA_v_coll = {"llvm.nvvm.barrier0"};

/*
 * ALL COLLECTIVES
 */

std::vector<const char *> v_coll;

void initCollectives() {
  static bool hasBeenInitalized = false;

  if (hasBeenInitalized)
    return;

  hasBeenInitalized = true;

  if (optMpiTaint)
    for (const auto &[name, _] : MPI_v_coll)
      v_coll.push_back(name);
  if (optOmpTaint)
    v_coll.insert(v_coll.end(), OMP_v_coll.begin(), OMP_v_coll.end());
  if (optUpcTaint)
    v_coll.insert(v_coll.end(), UPC_v_coll.begin(), UPC_v_coll.end());
  if (optCudaTaint)
    v_coll.insert(v_coll.end(), CUDA_v_coll.begin(), CUDA_v_coll.end());
}

bool isCollective(const llvm::Function *F) {
  for (unsigned i = 0; i < v_coll.size(); ++i) {
    if (F->getName().equals(v_coll[i]))
      return true;
  }

  return false;
}

bool isMpiCollective(int color) {
  if (color >= (int)v_coll.size())
    return false;

  for (unsigned i = 0; i < MPI_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], MPI_v_coll[i].name))
      return true;
  }

  return false;
}

bool isOmpCollective(int color) {
  if (color >= (int)v_coll.size())
    return false;

  for (unsigned i = 0; i < OMP_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], OMP_v_coll[i]))
      return true;
  }

  return false;
}

bool isUpcCollective(int color) {
  if (color >= (int)v_coll.size())
    return false;

  for (unsigned i = 0; i < UPC_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], UPC_v_coll[i]))
      return true;
  }

  return false;
}

bool isCudaCollective(int color) {
  if (color >= (int)v_coll.size())
    return false;

  for (unsigned i = 0; i < CUDA_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], CUDA_v_coll[i]))
      return true;
  }

  return false;
}

int getCollectiveColor(const llvm::Function *F) {
  for (unsigned i = 0; i < v_coll.size(); ++i) {
    if (F->getName().equals(v_coll[i]))
      return i;
  }

  return -1;
}
