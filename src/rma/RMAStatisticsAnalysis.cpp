#include "parcoach/LocalConcurrencyDetectionPass.h"
#include "llvm/IR/InstIterator.h"

using namespace llvm;
namespace parcoach {

namespace {
// Count MPI operations in Fortran
void CountMPIfuncFORTRAN(RMAStatisticsAnalysis::Result &Res, Instruction &I) {
  if (I.getOperand(0)->getName() == ("mpi_put_")) {
    // GreenErr() << "PARCOACH DEBUG: Found Put\n ";
    // DEBUG INFO: I.print(errs(),nullptr);
    Res.Put++;
  } else if (I.getOperand(0)->getName() == ("mpi_get_")) {
    Res.Get++;
  } else if (I.getOperand(0)->getName() == ("mpi_accumulate_")) {
    Res.Acc++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_create_")) {
    Res.Win++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_free_")) {
    Res.Free++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_fence_")) {
    Res.Fence++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_unlock_all_")) {
    Res.Unlockall++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_unlock_")) {
    Res.Unlock++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_lock_")) {
    Res.Lock++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_lock_all_")) {
    Res.Lockall++;
  } else if (I.getOperand(0)->getName() == ("mpi_win_flush_")) {
    Res.Flush++;
  } else if (I.getOperand(0)->getName() == ("mpi_barrier_")) {
    Res.Barrier++;
  }
}

void CountMPIfuncC(RMAStatisticsAnalysis::Result &Res, Instruction &I,
                   StringRef Name) {

  if (Name == "MPI_Get") {
    Res.Get++;
  } else if (Name == "MPI_Put") {
    Res.Put++;
  } else if (Name == "MPI_Win_create") {
    Res.Win++;
  } else if (Name == "MPI_Accumulate") {
    Res.Acc++;
  } else if (Name == "MPI_Win_fence") {
    Res.Fence++;
  } else if (Name == "MPI_Win_flush") {
    Res.Flush++;
  } else if (Name == "MPI_Win_lock") {
    Res.Lock++;
  } else if (Name == "MPI_Win_unlock") {
    Res.Unlock++;
  } else if (Name == "MPI_Win_unlock_all") {
    Res.Unlockall++;
  } else if (Name == "MPI_Win_lock_all") {
    Res.Lockall++;
  } else if (Name == "MPI_Win_free") {
    Res.Free++;
  } else if (Name == "MPI_Barrier") {
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
    if (I.getOpcode() == Instruction::BitCast) {
      if (I.getOperand(0)->getName().startswith("mpi_")) {
        Res.Mpi++;
        CountMPIfuncFORTRAN(Res, I);
      }
    } else if (CallBase *cb = dyn_cast<CallBase>(&I)) {
      if (Function *calledFunction = cb->getCalledFunction()) {
        if (calledFunction->getName().startswith("MPI_")) {
          Res.Mpi++;
          CountMPIfuncC(Res, I, calledFunction->getName());
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

} // namespace parcoach
