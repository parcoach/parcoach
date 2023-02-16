#pragma once

#include "ParcoachAnalysisInter.h"

namespace llvm {
class Function;
}

namespace parcoach {

struct ParcoachInstrumentationPass
    : public llvm::PassInfoMixin<ParcoachInstrumentationPass> {
  static llvm::PreservedAnalyses run(llvm::Module &M,
                                     llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

struct CollectiveInstrumentation {
  CollectiveInstrumentation(CallToWarningMapTy const &);
  bool run(llvm::Function &F);

private:
  CallToWarningMapTy const &Warnings;
};
} // namespace parcoach
