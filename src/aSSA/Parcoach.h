//===- Parcoach.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "PTACallGraph.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraphSCCPass.h"

namespace {
  class ParcoachInstr : public llvm::ModulePass {
  public:
    static char ID;
    static unsigned nbCollectivesFound;
    static unsigned nbCollectivesTainted;
    static unsigned nbWarnings;
    ParcoachInstr();

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;
    virtual bool doFinalization(llvm::Module &M);
    virtual bool runOnSCC(PTACallGraphSCC &SCC, DepGraph *DG);
    virtual bool runOnModule(llvm::Module &M);

  private:
  };
}

#endif /* PARCOACH_H */
