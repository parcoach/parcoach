#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

struct ParcoachPass : public llvm::PassInfoMixin<ParcoachPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

struct ParcoachInstrumentationPass
    : public llvm::PassInfoMixin<ParcoachInstrumentationPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

void RegisterAnalysis(llvm::ModuleAnalysisManager &MAM);
void RegisterPasses(llvm::ModulePassManager &MPM);
} // namespace parcoach
