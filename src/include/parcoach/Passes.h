#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

struct ParcoachPass : public llvm::PassInfoMixin<ParcoachPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

void RegisterPasses(llvm::ModulePassManager &MPM);
} // namespace parcoach
