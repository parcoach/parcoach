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
  {"llvm.memset.p0i8.i32", { 5, false, {true, false, false, false, false} } },
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
  {"gsl_rng_set", { 2, false, {false, false} } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, false, {true, false, false, false, false}
    } },
  {"llvm.memcpy.p0i8.p0i8.i32", { 5, false, {true, false, false, false, false}
    } },
  {"putchar", { 1, false, {false} } },
  {"feof", { 1, false, {false} } },
  {"fgets", { 3, true, {true, false, false} } },
  {"__isoc99_sscanf", {3, false, {false, false, true} } },
  {"strcmp", { 2, false, {false, false} } },
  {"strtod", { 2, false, {false, true} } },
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
  {"llvm.memmove.p0i8.p0i8.i32", { 5, false, {true, true, false, false, false}
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

struct funcDepPair {
  const char *name;
  const extDepInfo depInfo;
};


// TO DO
static const funcDepPair funcDepPairs[] = {
  {"MPI_Init", {0, {}, {} } },
  {"MPI_Comm_rank", { 2, {}, {} } },
  {"MPI_Comm_size", { 2, {}, {} } },
  {"puts", { 1, {}, {} } },
  {"strcpy", { 2, {}, {} } },
  {"strtol", { 3, {}, {} } },
  {"llvm.memset.p0i8.i64", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memset.p0i8.i32", { 5, { {0, {1, 2} } }, {} } },
  {"MPI_Finalize", {0, {}, {} } },
  {"llvm.lifetime.start", { 2, {}, {} } },
  //  {"llvm.dbg.declare", {} },
  {"sprintf", { 3, { {0, {1, 2} } }, {} } },
  {"unlink", { 1, {}, {} } },
  {"fopen", { 2, {}, {} } },
  {"fclose", { 1, {}, {} } },
  {"MPI_Bcast", { 5, {}, {} } },
  {"MPI_Barrier", { 1, {}, {} } },
  {"system", { 1, {}, {} } },
  {"llvm.lifetime.end", { 2, {}, {} } },
  {"fprintf", {3, {}, {} } },
  {"fflush", { 1, {}, {} } },
  {"log", {1, {}, {0} } },
  {"printf", { 2, {}, {} } },
  {"MPI_Allreduce", { 6, {}, {} } },
  {"malloc", { 1, {}, {} } },
  {"MPI_Allgather", { 7, {}, {} } },
  {"free", { 1, {}, {} } },
  {"exp", { 1, {}, {0} } },
  {"pow", { 2, {}, {0, 1} } },
  {"gsl_rng_alloc", { 1, {}, {} } },
  {"gsl_rng_set", { 2, {}, {} } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memcpy.p0i8.p0i8.i32", { 5, { {0, {1, 2} } }, {} } },
  {"putchar", { 1, {}, {} } },
  {"feof", { 1, {}, {} } },
  {"fgets", { 3, { {0, {1, 2} } }, {1, 2} } },
  {"__isoc99_sscanf", {3, { {2, {0, 1} } }, {} } },
  {"strcmp", { 2, {}, {0, 1} } },
  {"strtod", { 2, {}, {0} } },
  {"strlen", { 1, {}, {0} } },
  {"__isoc99_fscanf", { 3, { {2, {0, 1} } }, {} } },
  {"exit", { 1, {}, {} } },
  {"MPI_Abort", { 2, {}, {} } },
  {"MPI_Reduce", { 7, {}, {} } },
  {"sqrt", { 1, {}, {0} } },
  {"fabs", { 1, {}, {0} } },
  {"gsl_rng_state", { 1, {}, {} } },
  {"gsl_rng_size", { 1, {}, {} } },
  {"MPI_Recv", { 7, {}, {} } },
  {"MPI_Send", { 6, {}, {} } },
  {"fwrite", { 4, { {3, {0, 1, 2} } }, {} } },
  {"__errno_location", { 0, {}, {} } },
  {"strerror", { 1, {}, {} } },
  {"MPI_Ssend", { 6, {}, {} } },
  {"fread", { 4, { {0, {1, 2, 3} } }, {} } },
  {"llvm.memmove.p0i8.p0i8.i64", { 5, { {0, {1, 2} }, {1, {2} } }, {} } },
  {"llvm.memmove.p0i8.p0i8.i32", { 5, { {0, {1, 2} }, {1, {2} } }, {} } },
  {"strncmp", { 3, {}, {0, 1, 2} } },
  {"gsl_rng_uniform", { 1, {}, {} } },
  {"MPI_Wtime", { 0 , {}, {} } },
  {"qsort", { 4, {}, {} } },
  {"MPI_Sendrecv", { 12, {}, {} } },
  {"MPI_Gather", { 8, {}, {} } },
  {"fputc", { 2, {}, {} } },
  {"fabsf", { 1, {}, {0} } },
  {"gsl_integration_workspace_alloc", { 1, {}, {} } },
  {"gsl_integration_qag", { 10, {}, {} } },
  {"gsl_integration_workspace_free", { 1, {}, {} } },
  {"MPI_Allgatherv", { 8, {}, {} } },
  {"erfc", { 1, {}, {0} } },
  {"cos", { 1, {}, {0} } },
  {"sin", { 1, {}, {0} } },
  {NULL, {0, {}, {} } }
};


ExtInfo::ExtInfo() {
  for (const funcModPair *i = funcModPairs; i->name; ++i)
    extModInfoMap[i->name] = &i->modInfo;

  for (const funcDepPair *i = funcDepPairs; i->name; ++i)
    extDepInfoMap[i->name] = &i->depInfo;
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

const extDepInfo *
ExtInfo::getExtDepInfo(const llvm::Function *F) {
  auto I = extDepInfoMap.find(F->getName());

  if (I != extDepInfoMap.end())
    return extDepInfoMap[F->getName()];

  return NULL;
}
