#pragma once

#include "llvm/ADT/SmallSet.h"
#include "llvm/Passes/PassBuilder.h"

namespace parcoach::rma {
struct RMAInstrumentationPass
    : public llvm::PassInfoMixin<RMAInstrumentationPass> {
  static llvm::PreservedAnalyses run(llvm::Module &M,
                                     llvm::ModuleAnalysisManager &AM);
};

using LocalConcurrencyVector =
    llvm::SmallSetVector<std::pair<llvm::Instruction *, llvm::Instruction *>,
                         16>;

class LocalConcurrencyAnalysis
    : public llvm::AnalysisInfoMixin<LocalConcurrencyAnalysis> {
  friend llvm::AnalysisInfoMixin<LocalConcurrencyAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = LocalConcurrencyVector;

  static Result run(llvm::Function &F, llvm::FunctionAnalysisManager &AM);
};

class RMAStatisticsAnalysis
    : public llvm::AnalysisInfoMixin<RMAStatisticsAnalysis> {
  friend llvm::AnalysisInfoMixin<RMAStatisticsAnalysis>;
  static llvm::AnalysisKey Key;

public:
  struct Statistics {
    size_t Get, Put, Acc, Win, Free, Fence, Flush, Lock, Lockall, Unlock,
        Unlockall, Barrier, Mpi, Load, Store, InstLoad, InstStore;
    size_t getTotalRMA() const;
    size_t getTotalOneSided() const;
  };
  using Result = Statistics;
  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &);
};

} // namespace parcoach::rma
