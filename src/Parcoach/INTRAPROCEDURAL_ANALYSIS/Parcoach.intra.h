//===- Parcoach.intra.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "DependencyGraph.h"
#include "InterDependenceMap.h"

#include <llvm/ADT/SetVector.h>
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

namespace {
  class ParcoachInstr : public llvm::ModulePass {
  public:
    static char ID;
    ParcoachInstr();

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;

    virtual bool doInitialization(llvm::Module &M);

    virtual bool runOnModule(llvm::Module &M);

    bool runOnFunction(llvm::Function &F);

  private:
    llvm::Module *MD;
    llvm::AliasAnalysis *AA;
    llvm::TargetLibraryInfo *TLI;

    const char *ProgName;

    // Map containing the dependencies between functions.
    InterDependenceMap depMap;
  };
}

#endif /* PARCOACH_H */
