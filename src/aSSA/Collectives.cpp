#include "Collectives.h"
#include "Options.h"

/*
 * MPI COLLECTIVES
 */

std::vector<const char *> MPI_v_coll = {
  "MPI_Barrier", "MPI_Comm_split", "MPI_Comm_create", "MPI_Comm_dup", "MPI_Comm_dup_with_info", "MPI_Ibarrier", 
  "MPI_Bcast", "MPI_Ibcast",
  "MPI_Allreduce","MPI_Reduce_scatter","MPI_Reduce_scatter_block","MPI_Scan", "MPI_Exscan", "MPI_Iallreduce", "MPI_Ireduce_scatter_block","MPI_Ireduce_scatter", "MPI_Iscan", "MPI_Iexscan",
	"MPI_Reduce", "MPI_Ireduce","MPI_Allgather","MPI_Alltoall","MPI_Iallgather","MPI_Ialltoall",
	"MPI_Scatter","MPI_Gather","MPI_Igather", "MPI_Allgatherv","MPI_Iscatter", "MPI_Iallgatherv",
	"MPI_Scatterv", "MPI_Gatherv", "MPI_Alltoallv", "MPI_Alltoallw","MPI_Igatherv","MPI_Iscatterv","MPI_Ialltoallv","MPI_Ialltoallw"
};

int Com_arg_id(int color) {
	if(color<=5)
		return 0;
	if(color>5 && color<=7)
		return 4;
	if(color>7 && color<=17)
		return 5;
	if(color>17 && color<=23)
		return 6;
	if(color>23 && color<=29)
		return 7;
	if(color>29)
		return 8;

	return 0;
/*MPI_Barrier=0,MPI_Bcast=4,MPI_Scatter=7,MPI_Scatterv=8,MPI_Gather=7,
  MPI_Gatherv=8, MPI_Allgather=6, MPI_Allgatherv=7, MPI_Alltoall=6,
  MPI_Alltoallv=8, MPI_Alltoallw=8, MPI_Reduce=6, MPI_Allreduce=5,
  MPI_Reduce_scatter=5, MPI_Reduce_scatter_block=5, MPI_Scan=5, MPI_Exscan=5, MPI_Comm_split=0, MPI_Comm_create=0,
  MPI_Comm_dup=0, MPI_Comm_dup_with_info=0, MPI_Ibarrier=0, MPI_Igather=7,
  MPI_Igatherv=8, MPI_Iscatter=7, MPI_Iscatterv=8, MPI_Iallgather=6,
  MPI_Iallgatherv=7, MPI_Ialltoall=6, MPI_Ialltoallv=8, MPI_Ialltoallw=8,
  MPI_Ireduce=6, MPI_Iallreduce=5, MPI_Ireduce_scatter_block=5,
  MPI_Ireduce_scatter=5, MPI_Iscan=5, MPI_Iexscan=5,MPI_Ibcast=4
*/
}

/*
 * OMP COLLECTIVES
 */

std::vector<const char *> OMP_v_coll = {"__kmpc_barrier"};


/*
 * UPC COLLECTIVES - in upcr_collective.h
 */

std::vector<const char *> UPC_v_coll = {"_upcr_wait", "_upcr_all_broadcast", "_upcr_all_reduceD", "_upcr_all_gather", "_upcr_all_scatter", "_upcr_all_scatter","_upcr_all_reduceC", "_upcr_all_reduceUC", "_upcr_all_reduceS", "_upcr_all_reduceUS", "_upcr_all_reduceI", "_upcr_all_reduceUI", "_upcr_all_reduceL", "_upcr_all_reduceUL", "_upcr_all_reduceF", "_upcr_all_reduceLD" };


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
  if (color >= (int) v_coll.size())
    return false;

  for (unsigned i=0; i<MPI_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], MPI_v_coll[i]))
      return true;
  }

  return false;
}

bool isOmpCollective(int color) {
  if (color >= (int) v_coll.size())
    return false;

  for (unsigned i=0; i<OMP_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], OMP_v_coll[i]))
      return true;
  }

  return false;
}

bool isUpcCollective(int color) {
  if (color >= (int) v_coll.size())
    return false;

  for (unsigned i=0; i<UPC_v_coll.size(); ++i) {
    if (!strcmp(v_coll[color], UPC_v_coll[i]))
      return true;
  }

  return false;
}

bool isCudaCollective(int color) {
  if (color >= (int) v_coll.size())
    return false;

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
