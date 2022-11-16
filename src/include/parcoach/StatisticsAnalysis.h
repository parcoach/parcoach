#pragma once

#include "llvm/Passes/PassBuilder.h"

namespace llvm {
class raw_ostream;
}

namespace parcoach {

class StatisticsAnalysis : public llvm::AnalysisInfoMixin<StatisticsAnalysis> {
  friend llvm::AnalysisInfoMixin<StatisticsAnalysis>;
  static llvm::AnalysisKey Key;

public:
  struct Statistics {
    size_t Functions;
    size_t IndirectCalls;
    size_t DirectCalls;
    void print(llvm::raw_ostream &Out) const;
  };
  using Result = Statistics;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
