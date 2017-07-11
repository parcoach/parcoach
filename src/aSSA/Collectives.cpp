#include "Collectives.h"
#include "Options.h"

/*
 * MPI COLLECTIVES
 */

std::vector<const char *> MPI_v_coll = {
  "MPI_Barrier", "MPI_Bcast", "MPI_Scatter", "MPI_Scatterv", "MPI_Gather",
  "MPI_Gatherv", "MPI_Allgather", "MPI_Allgatherv", "MPI_Alltoall",
  "MPI_Alltoallv", "MPI_Alltoallw", "MPI_Reduce", "MPI_Allreduce",
  "MPI_Reduce_scatter", "MPI_Scan", "MPI_Comm_split", "MPI_Comm_create",
  "MPI_Comm_dup", "MPI_Comm_dup_with_info", "MPI_Ibarrier", "MPI_Igather",
  "MPI_Igatherv", "MPI_Iscatter", "MPI_Iscatterv", "MPI_Iallgather",
  "MPI_Iallgatherv", "MPI_Ialltoall", "MPI_Ialltoallv", "MPI_Ialltoallw",
  "MPI_Ireduce", "MPI_Iallreduce", "MPI_Ireduce_scatter_block",
  "MPI_Ireduce_scatter", "MPI_Iscan", "MPI_Iexscan","MPI_Ibcast"
};


/*
 * OMP COLLECTIVES
 */

std::vector<const char *> OMP_v_coll = {"__kmpc_barrier"};


/*
 * UPC COLLECTIVES
 */

std::vector<const char *> UPC_v_coll = {"_upcr_wait"};


/*
 * CUDA COLLECTIVES
 */

std::vector<const char *> CUDA_v_coll = {"llvm.nvvm.barrier0"};


/*
 * ALL COLLECTIVES
 */

std::vector<const char *> v_coll;

void initCollectives()
{
  static bool hasBeenInitalized = false;

  if (hasBeenInitalized)
    return;

  hasBeenInitalized = true;

  if (optMpiTaint)
    v_coll.insert(v_coll.end(), MPI_v_coll.begin(), MPI_v_coll.end());
  if (optOmpTaint)
    v_coll.insert(v_coll.end(), OMP_v_coll.begin(), OMP_v_coll.end());
  if (optUpcTaint)
    v_coll.insert(v_coll.end(), UPC_v_coll.begin(), UPC_v_coll.end());
  if (optCudaTaint)
    v_coll.insert(v_coll.end(), CUDA_v_coll.begin(), CUDA_v_coll.end());
}

bool isCollective(const llvm::Function *F) {
  for (unsigned i=0; i<v_coll.size(); ++i) {
    if (F->getName().equals(v_coll[i]))
      return true;
  }

  return false;
}

bool isMpiCollective(int color) {
  for (unsigned i=0; i<MPI_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], MPI_v_coll[i]))
      return true;
  }

  return false;
}

bool isOmpCollective(int color) {
  for (unsigned i=0; i<OMP_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], OMP_v_coll[i]))
      return true;
  }

  return false;
}

bool isUpcCollective(int color) {
  for (unsigned i=0; i<UPC_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], UPC_v_coll[i]))
      return true;
  }

  return false;
}

bool isCudaCollective(int color) {
  for (unsigned i=0; i<CUDA_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], CUDA_v_coll[i]))
      return true;
  }

  return false;
}


int getCollectiveColor(const llvm::Function *F) {
  for (unsigned i=0; i<v_coll.size(); ++i) {
    if (F->getName().equals(v_coll[i]))
      return i;
  }

  return -1;
}
