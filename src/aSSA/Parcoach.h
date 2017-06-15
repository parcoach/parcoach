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
    static unsigned nbWarnings;
    static unsigned nbConds;

    static unsigned nbWarningsParcoach;
    static unsigned nbCondsParcoach;

    /* timers */
    static double tstart, tend,
      tstart_aa, tend_aa,
      tstart_pta, tend_pta,
      tstart_regcreation, tend_regcreation,
      tstart_modref, tend_modref,
      tstart_assa, tend_assa,
      tstart_depgraph, tend_depgraph,
      tstart_flooding, tend_flooding,
      tstart_parcoach, tend_parcoach;
    ParcoachInstr();

    virtual void checkCollectives(llvm::Function *F, DepGraph *DG);
    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;
    virtual bool doInitialization(llvm::Module &M);
    virtual bool doFinalization(llvm::Module &M);
    virtual bool runOnModule(llvm::Module &M);

  private:
    std::set<const llvm::BasicBlock *> parcoachOnlyNodes;
    std::set<const llvm::BasicBlock *> parcoachNodes;

    void replaceOMPMicroFunctionCalls(llvm::Module &M);
  };
}

#endif /* PARCOACH_H */
