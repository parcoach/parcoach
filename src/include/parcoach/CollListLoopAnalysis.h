#pragma once

#include "parcoach/CFGVisitors.h"

#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueMap.h"

namespace llvm {
class Function;
}

class PTACallGraph;

namespace parcoach {

struct LoopCFGInfo {
  LoopAggretationInfo LAI;
  CollectiveList::CommToBBToCollListMap CommToBBToCollList;
#ifndef NDEBUG
  void dump() const;
#endif
};

struct CollListLoopAnalysis {
  CollListLoopAnalysis(PTACallGraph const &CG,
                       llvm::SmallPtrSetImpl<llvm::Value *> const &Comms)
      : PTACG(CG), Communicators(Comms) {}
  PTACallGraph const &PTACG;
  llvm::SmallPtrSetImpl<llvm::Value *> const &Communicators;
  LoopCFGInfo run(llvm::Function &F, llvm::FunctionAnalysisManager &);
};
} // namespace parcoach
