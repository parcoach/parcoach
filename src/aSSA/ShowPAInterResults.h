#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {
struct ShowPAInterResult : public llvm::PassInfoMixin<ShowPAInterResult> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};
} // namespace parcoach
