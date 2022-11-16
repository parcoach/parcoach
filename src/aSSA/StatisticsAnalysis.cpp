#include "parcoach/StatisticsAnalysis.h"

#include "llvm/IR/InstIterator.h"

using namespace llvm;
namespace parcoach {
AnalysisKey StatisticsAnalysis::Key;

StatisticsAnalysis::Statistics
StatisticsAnalysis::run(Module &M, ModuleAnalysisManager &) {
  Statistics Res{};
  auto IsCallInst = [](Instruction const &I) { return isa<CallInst>(I); };
  for (const Function &F : M) {
    Res.Functions++;
    for (auto &I : make_filter_range(instructions(F), IsCallInst)) {
      const CallInst &CI = cast<CallInst>(I);
      if (CI.getCalledFunction()) {
        Res.DirectCalls++;
      } else {
        Res.IndirectCalls++;
      }
    }
  }
  return Res;
}

void StatisticsAnalysis::Statistics::print(raw_ostream &Out) const {
  Out << "nb functions : " << Functions << "\n";
  Out << "nb direct calls : " << DirectCalls << "\n";
  Out << "nb indirect calls : " << IndirectCalls << "\n";
}
} // namespace parcoach
