#pragma once

#include "parcoach/CollListFunctionAnalysis.h"

#include "llvm/Passes/PassBuilder.h"

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
  CollectiveInstrumentation(WarningCollection const &);
  bool run(llvm::Function &F);

private:
  WarningCollection const &Warnings;
};
} // namespace parcoach
