#include "parcoach/RMAPasses.h"
#include "llvm/IR/InstIterator.h"

using namespace llvm;
namespace parcoach::rma {

namespace {
void CountMPIfuncs(RMAStatisticsAnalysis::Result &Res, Instruction &I,
                   StringRef Name) {

  if (Name == "MPI_Get" || Name == "mpi_get_") {
    Res.Get++;
  } else if (Name == "MPI_Put" || Name == "mpi_put_") {
    Res.Put++;
  } else if (Name == "MPI_Win_create" || Name == "mpi_win_create_") {
    Res.Win++;
  } else if (Name == "MPI_Win_allocate" || Name == "mpi_win_allocate_") {
    Res.Win++;
  } else if (Name == "MPI_Accumulate" || Name == "mpi_accumulate_") {
    Res.Acc++;
  } else if (Name == "MPI_Win_fence" || Name == "mpi_win_fence_") {
    Res.Fence++;
  } else if (Name == "MPI_Win_flush" || Name == "mpi_win_flush_") {
    Res.Flush++;
  } else if (Name == "MPI_Win_lock" || Name == "mpi_win_lock_") {
    Res.Lock++;
  } else if (Name == "MPI_Win_unlock" || Name == "mpi_win_unlock_") {
    Res.Unlock++;
  } else if (Name == "MPI_Win_unlock_all" || Name == "mpi_win_unlock_all_") {
    Res.Unlockall++;
  } else if (Name == "MPI_Win_lock_all" || Name == "mpi_win_lock_all_") {
    Res.Lockall++;
  } else if (Name == "MPI_Win_free" || Name == "mpi_win_free_") {
    Res.Free++;
  } else if (Name == "MPI_Barrier" || Name == "mpi_barrier_") {
    Res.Barrier++;
  }
}
} // namespace

AnalysisKey RMAStatisticsAnalysis::Key;

RMAStatisticsAnalysis::Statistics
RMAStatisticsAnalysis::run(Function &F, FunctionAnalysisManager &) {
  TimeTraceScope TTS("RMAStatisticsAnalysis");
  Statistics Res{};
  for (Instruction &I : instructions(F)) {
    DebugLoc dbg = I.getDebugLoc(); // get debug infos
    if (CallBase *cb = dyn_cast<CallBase>(&I)) {
      if (Function *calledFunction = cb->getCalledFunction()) {
        StringRef FName = calledFunction->getName();
        if (FName.startswith("MPI_") || FName.startswith("mpi_")) {
          Res.Mpi++;
          CountMPIfuncs(Res, I, calledFunction->getName());
        }
      }
    }
  }
  return Res;
}

size_t RMAStatisticsAnalysis::Statistics::getTotalRMA() const {
  return Win + Put + Get + Fence + Acc + Lock + Lockall + Unlock + Unlockall +
         Free + Flush;
}

size_t RMAStatisticsAnalysis::Statistics::getTotalOneSided() const {
  return Put + Get + Acc;
}

} // namespace parcoach::rma
