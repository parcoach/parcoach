#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {

struct PrepareOpenMPInstr : public llvm::PassInfoMixin<PrepareOpenMPInstr> {
  static llvm::PreservedAnalyses run(llvm::Module &M,
                                     llvm::ModuleAnalysisManager &AM);
};
} // namespace parcoach
