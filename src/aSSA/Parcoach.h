//===- Parcoach.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "DepGraph.h"
#include "ASSA.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

namespace {
  class ParcoachInstr : public llvm::ModulePass {
  public:
    static char ID;
    ParcoachInstr();

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;

    virtual bool runOnModule(llvm::Module &M);

    bool runOnFunction(llvm::Function &F);

  private:
    DepGraph dg;
  };
}

#endif /* PARCOACH_H */
