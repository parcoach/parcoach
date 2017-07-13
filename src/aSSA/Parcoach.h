//===- Parcoach.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "PTACallGraph.h"
#include "ParcoachAnalysisInter.h"
#include "ParcoachAnalysisIntra.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/CallGraphSCCPass.h"

namespace {
  class ParcoachInstr : public llvm::ModulePass {
  public:
    static char ID;

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

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;
    virtual bool doInitialization(llvm::Module &M);
    virtual bool doFinalization(llvm::Module &M);
    virtual bool runOnModule(llvm::Module &M);

  private:
    ParcoachAnalysisInter *PAInter;
    ParcoachAnalysisIntra *PAIntra;

    std::map<llvm::Instruction *, llvm::Instruction *> ompNewInst2oldInst;

    void replaceOMPMicroFunctionCalls(llvm::Module &M,
				      std::map<const llvm::Function *,
				      std::set<const llvm::Value *> > &
				      func2SharedVarMap);
    void revertOmpTransformation();
  };
}

#endif /* PARCOACH_H */
