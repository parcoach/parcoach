#pragma once

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

class MPICommAnalysis : public llvm::AnalysisInfoMixin<MPICommAnalysis> {
  friend llvm::AnalysisInfoMixin<MPICommAnalysis>;
  static llvm::AnalysisKey Key;

public:
  // There are rarely more than 8 communicators in a given program.
  using Result = llvm::SmallPtrSet<llvm::Value *, 8>;

  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
