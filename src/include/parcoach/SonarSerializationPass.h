#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace parcoach {
struct SonarSerializationPass
    : public llvm::PassInfoMixin<SonarSerializationPass> {
  static llvm::PreservedAnalyses run(llvm::Module &M,
                                     llvm::ModuleAnalysisManager &AM);
};
} // namespace parcoach
