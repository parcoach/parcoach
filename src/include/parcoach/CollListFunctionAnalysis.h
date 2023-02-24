#pragma once
#include "parcoach/CollListLoopAnalysis.h"

#include "parcoach/CollectiveList.h"
#include "parcoach/Warning.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class Function;
}

namespace parcoach {

constexpr char ProgName[] = "PARCOACH";
using CallToWarningMap = llvm::DenseMap<llvm::CallBase *, Warning>;

class CollListFunctionAnalysis
    : public llvm::AnalysisInfoMixin<CollListFunctionAnalysis> {
  friend llvm::AnalysisInfoMixin<CollListFunctionAnalysis>;
  static llvm::AnalysisKey Key;

public:
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<CollectiveList::CommToBBToCollListMap>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

class CollectiveAnalysis : public llvm::AnalysisInfoMixin<CollectiveAnalysis> {
  friend llvm::AnalysisInfoMixin<CollectiveAnalysis>;
  static llvm::AnalysisKey Key;
  bool EmitDotDG_;

public:
  CollectiveAnalysis(bool EmitDotDG) : EmitDotDG_(EmitDotDG) {}
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<CallToWarningMap>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
