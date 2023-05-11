#include "parcoach/StatisticsAnalysis.h"

#include "llvm/IR/InstIterator.h"

using namespace llvm;
namespace parcoach {
AnalysisKey StatisticsAnalysis::Key;

StatisticsAnalysis::Statistics
StatisticsAnalysis::run(Module &M, ModuleAnalysisManager & /*unused*/) {
  TimeTraceScope TTS("StatisticsAnalysis");
  Statistics Res{};
  auto IsCallInst = [](Instruction const &I) { return isa<CallInst>(I); };
  for (Function const &F : M) {
    Res.Functions++;
    for (auto const &I : make_filter_range(instructions(F), IsCallInst)) {
      CallInst const &CI = cast<CallInst>(I);
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
