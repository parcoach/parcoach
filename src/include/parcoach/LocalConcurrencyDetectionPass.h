#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {
struct LocalConcurrencyDetectionPass
    : public llvm::PassInfoMixin<LocalConcurrencyDetectionPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};
} // namespace parcoach
