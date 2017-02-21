#include "ExtInfo.h"
#include "Utils.h"

#include "llvm/IR/Module.h"

using namespace llvm;
using namespace std;

struct funcModPair {
  const char *name;
  const extModInfo modInfo;
};

static const funcModPair funcModPairs[] = {
  /* LLVM intrinsics */
  {"llvm.fabs.v2f64", { 1, false, {false} } },
  {"llvm.lifetime.end", { 2, false, {false, false} } },
  {"llvm.lifetime.start", { 2, false, {false, false} } },
  {"llvm.memcpy.p0i8.p0i8.i32", { 5, false, {true, false, false, false, false}
    } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, false, {true, false, false, false, false}
    } },
  {"llvm.memmove.p0i8.p0i8.i32", { 5, false, {true, true, false, false, false}
    } },
  {"llvm.memmove.p0i8.p0i8.i64", { 5, false, {true, true, false, false, false}
    } },
  {"llvm.memset.p0i8.i32", { 5, false, {true, false, false, false, false} } },
  {"llvm.memset.p0i8.i64", { 5, false, {true, false, false, false, false} } },
  {"llvm.trap", { 0, false, {} } },

  /* libc */
  {"calloc", { 2, true, {false, false} } },
  {"ceil", { 1, false, {false} } },
  {"clock", { 0, false, {} } },
  {"cos", { 1, false, {false} } },
  {"erfc", { 1, false, {false} } },
  {"exit", { 1, false, {false} } },
  {"exp", { 1, false, {false} } },
  {"fabs", { 1, false, {false} } },
  {"fabsf", { 1, false, {false} } },
  {"fclose", { 1, false, {true} } },
  {"feof", { 1, false, {false} } },
  {"fflush", { 1, false, {false} } },
  {"fgets", { 3, true, {true, false, false} } },
  {"floor", { 1, false, {false} } },
  {"fopen", { 2, true, {false, false} } },
  {"fprintf", {3, false, {true, false, false} } },
  {"fputc", { 2, false, {false, true} } },
  {"fread", { 4, false, {true, false, false, true} } },
  {"free", { 1, false, {true} } },
  {"fwrite", { 4, false, {false, false, false, true} } },
  {"ldexp", {2, false, {false, false} } },
  {"log", {1, false, {false} } },
  {"malloc", { 1, true, {false} } },
  {"pow", { 2, false, {false, false} } },
  {"printf", { 2, false, {false, false} } },
  {"putchar", { 1, false, {false} } },
  {"puts", { 1, false, {false} } },
  {"qsort", { 4, false, {true, false, false, false} } },
  {"realloc", { 2, true, {true, false} } },
  {"sin", { 1, false, {false} } },
  {"sprintf", { 3, false, {true, false, false} } },
  {"sqrt", { 1, false, {false} } },
  {"strcmp", { 2, false, {false, false} } },
  {"strcpy", { 2, true, {true, false} } },
  {"strcspn", { 2, false, {false, false} } },
  {"strerror", { 1, true, {false} } },
  {"strlen", { 1, false, {false} } },
  {"strncmp", { 3, false, {false, false, false} } },
  {"strncpy", { 3, true, {true, false, false} } },
  {"strspn", { 2, false, {false, false} } },
  {"strtod", { 2, false, {false, true} } },
  {"strtol", { 3, false, {false, true, false} } },
  {"system", { 1, false, {false} } },
  {"unlink", { 1, false, {false} } },
  {"__errno_location", { 0, true, {} } },
  {"__isoc99_fscanf", { 3, false, {true, false, true} } },
  {"__isoc99_scanf", {2, false, {false, true} } },
  {"__isoc99_sscanf", {3, false, {false, false, true} } },

  /* MPI */
  {"MPI_Abort", { 2, false, {false, false} } },
  {"MPI_Address", { 2, false, {false, true} } },
  {"MPI_Allgather", { 7, false, {false, false, false, true, false, false,
				 false} } },
  {"MPI_Allgatherv", { 8, false, {false, false, false, true, false, false,
				  false, false} } },
  {"MPI_Allreduce", { 6, false, {false, true, false, false, false, false} } },
  {"MPI_Alltoall", { 7, false, {false, false, false, true, false, false, true}
    } },
  {"MPI_Alltoallv", { 9, false, {false, false, false, false, true, false, false,
				 false, false} } },
  {"MPI_Barrier", { 1, false, {false} } },
  {"MPI_Bcast", { 5, false, {true, false, false, false, false} } },
  {"MPI_Comm_create", { 3, false, {false, false, true} } },
  {"MPI_Comm_group", { 2, false, {false, true} } },
  {"MPI_Comm_group_incl", { 4, false, {false, false, false, true} } },
  {"MPI_Comm_rank", { 2, false, {false, true} } },
  {"MPI_Comm_size", { 2, false, {false, true} } },
  {"MPI_Finalize", {0, false, {} } },
  {"MPI_Gather", { 8, false, {false, false, false, true, false, false, false,
			      false} } },
  {"MPI_Gatherv", { 10, false, {false, false, false, true, false, false, false,
				false, false, true} } },
  {"MPI_Get_count", { 3, false, {false, false, true} } },
  {"MPI_Group_free", { 1, false, {true} } },
  {"MPI_Group_incl", { 4, false, {false, false, false, true} } },
  {"MPI_Init", { 2, false, {false, false} } },
  {"MPI_Iprobe", {5, false, {false, false, false, true, true} } },
  {"MPI_Irecv", {7, false, {true, false, false, false, false, false, true} } },
  {"MPI_Isend", {7, false, {false, false, false, false, false, false, true} } },
  {"MPI_Op_create", {3, false, {false, false, true} } },
  {"MPI_Op_free", {1, false, {true} } },
  {"MPI_Probe", {4, false, {false, false, false, true} } },
  {"MPI_Recv", { 7, false, {true, false, false, false, false, false, true} } },
  {"MPI_Reduce", { 7, false, {false, true, false, false, false , false, false}
    } },
  {"MPI_Scan", { 6, false, {false, true, false, false, false, false} } },
  {"MPI_Scatter", { 8, false, {false, false, false, true, false, false, false,
			       false} } },
  {"MPI_Scatterv", { 9, false, {false, false, false, false, true, false, false,
				false, false} } },
  {"MPI_Send", { 6, false, {false, false, false, false, false, false} } },
  {"MPI_Sendrecv", { 12, false, {false, false, false, false, false, true, false,
				 false, false, false, false, true} } },
  {"MPI_Ssend", { 6, false, {false, false, false, false, false, false} } },
  {"MPI_Test", { 3, false, {false, false, true} } },
  {"MPI_Testall", { 4, false, {false, false, true, true} } },
  {"MPI_Type_commit", { 1, false, {false} } },
  {"MPI_Type_free", { 1, false, {true} } },
  {"MPI_Type_struct", { 5, false, {false, false, false, false, true} } },
  {"MPI_Wait", { 2 , false, {true, true} } },
  {"MPI_Waitall", { 3 , false, {false, true, true} } },
  {"MPI_Wtime", { 0 , false, {} } },

  /* OpenMP */
  {"omp_get_num_threads", { 0, false, {} } },
  {"omp_get_thread_num", { 0, false, {} } },
  {"__kmpc_fork_call", { 4, false, {false, false, false, false} } },
  /* gsl */
  {"gsl_integration_qag", { 10, true, {false, false, false, false, false, false,
				       false, true, true, true} } },
  {"gsl_integration_workspace_alloc", { 1, true, {false} } },
  {"gsl_integration_workspace_free", { 1, false, {true} } },
  {"gsl_rng_alloc", { 1, true, {false} } },
  {"gsl_rng_set", { 2, false, {false, false} } },
  {"gsl_rng_size", { 1, false, {false} } },
  {"gsl_rng_state", { 1, true, {false} } },
  {"gsl_rng_uniform", { 1, false, {false} } },

  {NULL, {0, false, {} } }
};

struct funcDepPair {
  const char *name;
  const extDepInfo depInfo;
};


// TO DO
static const funcDepPair funcDepPairs[] = {
  /* LLVM intrinsics */
  {"llvm.fabs.v2f64", { 1, {}, {0} } },
  {"llvm.lifetime.end", { 2, {}, {} } },
  {"llvm.lifetime.start", { 2, {}, {} } },
  {"llvm.memcpy.p0i8.p0i8.i32", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memcpy.p0i8.p0i8.i64", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memmove.p0i8.p0i8.i32", { 5, { {0, {1, 2} }, {1, {2} } }, {} } },
  {"llvm.memmove.p0i8.p0i8.i64", { 5, { {0, {1, 2} }, {1, {2} } }, {} } },
  {"llvm.memset.p0i8.i32", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.memset.p0i8.i64", { 5, { {0, {1, 2} } }, {} } },
  {"llvm.trap", { 0, {}, {} } },

  /* libc */
  {"calloc", { 2, {}, {0, 1} } },
  {"ceil", { 1, {}, {0} } },
  {"clock", { 0, {}, {} } },
  {"cos", { 1, {}, {0} } },
  {"erfc", { 1, {}, {0} } },
  {"exit", { 1, {}, {} } },
  {"exp", { 1, {}, {0} } },
  {"fabs", { 1, {}, {0} } },
  {"fabsf", { 1, {}, {0} } },
  {"fclose", { 1, {}, {} } },
  {"feof", { 1, {}, {} } },
  {"fflush", { 1, {}, {} } },
  {"fgets", { 3, { {0, {1, 2} } }, {1, 2} } },
  {"floor", { 1, {}, {0} } },
  {"fopen", { 2, {}, {} } },
  {"fprintf", {3, {}, {} } },
  {"fputc", { 2, {}, {} } },
  {"fread", { 4, { {0, {1, 2, 3} } }, {} } },
  {"free", { 1, {}, {} } },
  {"fwrite", { 4, { {3, {0, 1, 2} } }, {} } },
  {"ldexp", {2, {}, {0, 1} } },
  {"log", {1, {}, {0} } },
  {"malloc", { 1, {}, {} } },
  {"pow", { 2, {}, {0, 1} } },
  {"printf", { 2, {}, {} } },
  {"putchar", { 1, {}, {} } },
  {"puts", { 1, {}, {} } },
  {"qsort", { 4, {}, {} } },
  {"realloc", { 2, { {0, {0, 1} } }, {0, 1} } },
  {"sin", { 1, {}, {0} } },
  {"sprintf", { 3, { {0, {1, 2} } }, {} } },
  {"sqrt", { 1, {}, {0} } },
  {"strcmp", { 2, {}, {0, 1} } },
  {"strcpy", { 2, {}, {} } },
  {"strcspn", { 2, {}, {0, 1} } },
  {"strerror", { 1, {}, {} } },
  {"strlen", { 1, {}, {0} } },
  {"strncmp", { 3, {}, {0, 1, 2} } },
  {"strncpy", { 3, { {0, {1, 2} } }, {1, 2} } },
  {"strspn", { 2, {}, {0, 1} } },
  {"strtod", { 2, {}, {0} } },
  {"strtol", { 3, {}, {} } },
  {"system", { 1, {}, {} } },
  {"unlink", { 1, {}, {} } },
  {"__errno_location", { 0, {}, {} } },
  {"__isoc99_fscanf", { 3, { {2, {0, 1} } }, {} } },
  {"__isoc99_scanf", {2, { {1, {0} } }, {} } },
  {"__isoc99_sscanf", {3, { {2, {0, 1} } }, {} } },

  /* MPI */
  {"MPI_Abort", { 2, {}, {} } },
  {"MPI_Address", { 2, {}, {} } },
  {"MPI_Allgather", { 7, {}, {} } },
  {"MPI_Allgatherv", { 8, {}, {} } },
  {"MPI_Allreduce", { 6, {}, {} } },
  {"MPI_Alltoall", { 7, {}, {} } },
  {"MPI_Alltoallv", { 9, {}, {} } },
  {"MPI_Barrier", { 1, {}, {} } },
  {"MPI_Bcast", { 5, {}, {} } },
  {"MPI_Comm_create", { 3, {}, {} } },
  {"MPI_Comm_group", { 2, {}, {} } },
  {"MPI_Comm_group_incl", { 4, {}, {} } },
  {"MPI_Comm_rank", { 2, {}, {} } },
  {"MPI_Comm_size", { 2, {}, {} } },
  {"MPI_Finalize", {0, {}, {} } },
  {"MPI_Gather", { 8, {}, {} } },
  {"MPI_Gatherv", { 10, {}, {} } },
  {"MPI_Get_count", { 3, {}, {} } },
  {"MPI_Group_free", { 1, {}, {} } },
  {"MPI_Group_incl", { 4, {}, {} } },
  {"MPI_Init", {2, {}, {} } },
  {"MPI_Iprobe", {5, {}, {} } },
  {"MPI_Irecv", {7, {}, {} } },
  {"MPI_Isend", {7, {}, {} } },
  {"MPI_Op_create", {3, {}, {} } },
  {"MPI_Op_free", {1, {}, {} } },
  {"MPI_Probe", {4, {}, {} } },
  {"MPI_Recv", { 7, {}, {} } },
  {"MPI_Reduce", { 7, {}, {} } },
  {"MPI_Scan", { 6, {}, {} } },
  {"MPI_Scatter", { 8, {}, {} } },
  {"MPI_Scatterv", { 9, {}, {} } },
  {"MPI_Send", { 6, {}, {} } },
  {"MPI_Sendrecv", { 12, {}, {} } },
  {"MPI_Ssend", { 6, {}, {} } },
  {"MPI_Test", { 3, {}, {} } },
  {"MPI_Testall", { 4, {}, {} } },
  {"MPI_Type_commit", { 1, {}, {} } },
  {"MPI_Type_free", { 1, {}, {} } },
  {"MPI_Type_struct", { 5, {}, {} } },
  {"MPI_Wait", { 2 , {}, {} } },
  {"MPI_Waitall", { 3 , {}, {} } },
  {"MPI_Wtime", { 0 , {}, {} } },

  /* OpenMP */
  {"omp_get_num_threads", { 0 , {}, {} } },
  {"omp_get_thread_num", { 0 , {}, {} } },
  {"__kmpc_fork_call", { 4, {}, {} } },

  /* gsl */
  {"gsl_integration_qag", { 10, {}, {} } },
  {"gsl_integration_workspace_alloc", { 1, {}, {} } },
  {"gsl_integration_workspace_free", { 1, {}, {} } },
  {"gsl_rng_alloc", { 1, {}, {} } },
  {"gsl_rng_set", { 2, {}, {} } },
  {"gsl_rng_size", { 1, {}, {} } },
  {"gsl_rng_state", { 1, {}, {} } },
  {"gsl_rng_uniform", { 1, {}, {} } },

  {NULL, {0, {}, {} } }
};


ExtInfo::ExtInfo(Module &m) : m(m) {
  for (const funcModPair *i = funcModPairs; i->name; ++i)
    extModInfoMap[i->name] = &i->modInfo;

  for (const funcDepPair *i = funcDepPairs; i->name; ++i)
    extDepInfoMap[i->name] = &i->depInfo;

  bool missingInfo = false;

  for (Function &F : m) {
    if (!F.isDeclaration() || isIntrinsicDbgFunction(&F))
      continue;

    if (!getExtModInfo(&F)) {
      missingInfo = true;
      errs() << "missing info for external function " << F.getName() << "\n";
    }
  }

  if (missingInfo) {
    errs() << "Error: you have to fill the funcModPairs array in ExtInfo.cpp"
	   << " with the missing functions. exiting..\n";
    exit(EXIT_FAILURE);
  }
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
