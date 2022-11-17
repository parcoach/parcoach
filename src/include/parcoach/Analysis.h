#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

class ParcoachAnalysisInter;

struct Warning {
  using ConditionalsContainerTy = llvm::SmallVector<llvm::DebugLoc, 2>;
  Warning(llvm::Function const *, llvm::DebugLoc &, ConditionalsContainerTy &&);
  Warning() = default;
  ~Warning() = default;
  llvm::Function const *Missed;
  llvm::DebugLoc Where;
  ConditionalsContainerTy Conditionals;
  std::string toString() const;
};

using CallToWarningMapTy = llvm::ValueMap<llvm::CallBase *, Warning>;

class InterproceduralAnalysis
    : public llvm::AnalysisInfoMixin<InterproceduralAnalysis> {
  friend llvm::AnalysisInfoMixin<InterproceduralAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = std::unique_ptr<parcoach::ParcoachAnalysisInter>;

  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
