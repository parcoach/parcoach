#include "ExtInfo.h"

struct funcModPair {
  const char *name;
  const extModInfo modInfo;
};

static const funcModPair funcModPairs[] = {
  //llvm.dbg.value
  {"MPI_Init", { 2, false, {false, false} } },
  {"MPI_Comm_rank", { 2, false, {false, true} } },
  {"MPI_Comm_size", { 2, false, {false, true} } },
  {"puts", { 1, false, {false} } },
  {"strcpy", { 2, true, {true, false} } },
  {"strtol", { 3, false, {false, true, false} } },
  {"llvm.memset.p0i8.i64", { 5, false, {true, false, false, false, false} } },
  {"MPI_Finalize", {0, false, {} } },
  {"llvm.lifetime.start", { 2, false, {false, false} } },
  //  {"llvm.dbg.declare", {} },
  {"sprintf", { 3, false, {true, false, false} } },
  {"unlink", { 1, false, {false} } },
  {"fopen", { 2, true, {false, false} } },
  {"fclose", { 1, false, {true} } },
  {"MPI_Bcast", { 5, false, {true, false, false, false, false} } },
  {"MPI_Barrier", { 1, false, {false} } },
  {"system", { 1, false, {false} } },
  {"llvm.lifetime.end", { 2, false, {false, false} } },
  {"fprintf", {3, false, {true, false, false} } },
  {"fflush", { 1, false, {false} } },
  {"log", {1, false, {false} } },
  {"printf", { 2, false, {false, false} } },
  {"MPI_Allreduce", { 6, false, {false, true, false, false, false, false} } },
  {"malloc", { 1, true, {false} } },
  {"MPI_Allgather", { 7, false, {false, false, false, true, false, false,
				 false} } },
  {"free", { 1, false, {true} } },
  {"exp", { 1, false, {false} } },
  {"pow", { 2, false, {false, false} } },
  {"gsl_rng_alloc", { 1, true, {false} } },
  {"gsl_rng_set", { 1, false, {false} } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, false, {true, false, false, false, false}
    } },
  {"putchar", { 1, false, {false} } },
  {"feof", { 1, false, {false} } },
  {"fgets", { 1, true, {true} } },
  {"__isoc99_sscanf", {3, false, {false, false, true} } },
  {"strcmp", { 2, false, {false, false} } },
  {"strtod", { 2, false, {true, false} } },
  {"strlen", { 1, false, {false} } },
  {"__isoc99_fscanf", { 3, false, {true, false, true} } },
  {"exit", { 1, false, {false} } },
  {"MPI_Abort", { 2, false, {false, false} } },
  {"MPI_Reduce", { 7, false, {false, true, false, false, false , false, false}
    } },
  {"sqrt", { 1, false, {false} } },
  {"fabs", { 1, false, {false} } },
  {"gsl_rng_state", { 1, true, {false} } },
  {"gsl_rng_size", { 1, false, {false} } },
  {"MPI_Recv", { 7, false, {true, false, false, false, false, false, true} } },
  {"MPI_Send", { 6, false, {false, false, false, false, false, false} } },
  {"fwrite", { 4, false, {false, false, false, true} } },
  {"__errno_location", { 0, true, {} } },
  {"strerror", { 1, true, {false} } },
  {"MPI_Ssend", { 6, false, {false, false, false, false, false, false} } },
  {"fread", { 4, false, {true, false, false, true} } },
  {"llvm.memmove.p0i8.p0i8.i64", { 5, false, {true, true, false, false, false}
    } },
  {"strncmp", { 3, false, {false, false, false} } },
  {"gsl_rng_uniform", { 1, false, {false} } },
  {"MPI_Wtime", { 0 , false, {} } },
  {"qsort", { 4, false, {true, false, false, false} } },
  {"MPI_Sendrecv", { 12, false, {false, false, false, false, false, true, false,
				 false, false, false, false, true} } },
  {"MPI_Gather", { 8, false, {false, false, false, true, false, false, false,
			      false} } },
  {"fputc", { 2, false, {false, true} } },
  {"fabsf", { 1, false, {false} } },
  {"gsl_integration_workspace_alloc", { 1, true, {false} } },
  {"gsl_integration_qag", { 10, true, {false, false, false, false, false, false,
				       false, true, true, true} } },
  {"gsl_integration_workspace_free", { 1, false, {true} } },
  {"MPI_Allgatherv", { 8, false, {false, false, false, true, false, false,
				  false, false} } },
  {"erfc", { 1, false, {false} } },
  {"cos", { 1, false, {false} } },
  {"sin", { 1, false, {false} } },
  {NULL, {0, false, {} } }
};

ExtInfo::ExtInfo() {
  for (const funcModPair *i = funcModPairs; i->name; ++i)
    extModInfoMap[i->name] = &i->modInfo;
}

ExtInfo::~ExtInfo() {
}

const extModInfo *
ExtInfo::getExtModInfo(const llvm::Function *F) {
  auto I = extModInfoMap.find(F->getName());

  if (I != extModInfoMap.end())
    return extModInfoMap[F->getName()];

  return NULL;
}
