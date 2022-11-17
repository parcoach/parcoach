#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

struct PrepareOpenMPInstr : public llvm::PassInfoMixin<PrepareOpenMPInstr> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
};
} // namespace parcoach
