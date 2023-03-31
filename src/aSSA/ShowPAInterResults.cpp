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
  if (Res->empty()) {
    SMDiagnostic("", SourceMgr::DK_Remark, "No issues found.")
        .print(ProgName, errs(), true, true);
  }
  for (auto const &[_, W] : *Res) {
    SMDiagnostic(W.Where.Filename, SourceMgr::DK_Warning, W.toString())
        .print(ProgName, errs(), true, true);
  }
  return PreservedAnalyses::all();
}
} // namespace parcoach
