#include "Collectives.h"

#include <string>

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

std::vector<const char *> UPC_v_coll = {
  "_upcr_notify", "_upcr_all_broadcast", "_upcr_all_scatter",
  "_upcr_all_gather", "_upcr_all_gather_all", "_upcr_all_exchange",
  "_upcr_all_permute", "_upcr_all_reduce", "_upcr_prefix_reduce",
  "_upcr_all_sort"
}; // upc_barrier= upc_notify+upc_wait, TODO: add all collectives

std::vector<const char *> OMP_v_coll = {"__kmpc_cancel_barrier"};
// TODO: Add all OpenMP collectives

std::vector<const char *> v_coll(MPI_v_coll.size() + OMP_v_coll.size());
// TODO: add OMP_v_coll
