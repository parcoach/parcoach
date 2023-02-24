#include "ShowPAInterResults.h"
#include "Utils.h"

#include "parcoach/CollListFunctionAnalysis.h"
#include "parcoach/Collectives.h"
#include "parcoach/Options.h"

#include "llvm/Support/WithColor.h"

using namespace llvm;
namespace parcoach {

PreservedAnalyses ShowPAInterResult::run(Module &M, ModuleAnalysisManager &AM) {
  auto &Res = AM.getResult<CollectiveAnalysis>(M);
  if (Res->size() == 0) {
    SMDiagnostic("", SourceMgr::DK_Remark, "No issues found.")
        .print(ProgName, dbgs(), 1, 1);
  }
  for (auto const &[_, Warning] : *Res) {
    DebugLoc const &DLoc = Warning.Where;
    SMDiagnostic(DLoc ? DLoc->getFilename() : "", SourceMgr::DK_Warning,
                 Warning.toString())
        .print(ProgName, dbgs(), 1, 1);
  }
  return PreservedAnalyses::all();
}
} // namespace parcoach
