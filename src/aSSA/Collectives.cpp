#include "Collectives.h"


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
