#include "parcoach/MPICommAnalysis.h"

#include "parcoach/Collectives.h"

#include "llvm/ADT/STLExtras.h"

using namespace llvm;

namespace parcoach {
AnalysisKey MPICommAnalysis::Key;

MPICommAnalysis::Result MPICommAnalysis::run(Module &M,
                                             ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("MPICommAnalysis");
  MPICommAnalysis::Result Res;
  for (Function &F : M) {
    MPICollective const *Coll =
        dyn_cast_or_null<MPICollective>(Collective::find(F));
    // Ignore non-MPI collective, or those (MPIFinalize) not taking a
    // communicator parameter.
    if (!Coll || Coll->CommArgId < 0) {
      continue;
    }
    for (User *U : F.users()) {
      CallInst *CI = dyn_cast<CallInst>(U);
      if (!CI) {
        continue;
      }
      Res.insert(Coll->getCommunicator(*CI));
    }
  }
  return Res;
}
} // namespace parcoach
