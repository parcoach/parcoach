#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

struct Warning {
  using ConditionalsContainerTy = llvm::SmallVector<llvm::DebugLoc, 2>;
  Warning(llvm::Function const *, llvm::DebugLoc &, ConditionalsContainerTy &&);
  Warning() = default;
  ~Warning() = default;
  llvm::Function const *Missed;
  llvm::DebugLoc Where;
  ConditionalsContainerTy Conditionals;
  std::string toString() const;
  operator bool() const;
};

struct IAResult {
  llvm::DenseMap<llvm::CallBase *, Warning> Warnings;
};

class InterproceduralAnalysis
    : public llvm::AnalysisInfoMixin<InterproceduralAnalysis> {
  friend llvm::AnalysisInfoMixin<InterproceduralAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = IAResult;

  IAResult run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
