//===- Parcoach.h - Parcoach function pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

namespace {
  class ParcoachInstr : public llvm::FunctionPass {
  public:
    static char ID;
    static unsigned nbCollectivesFound;
    static unsigned nbWarnings;
    ParcoachInstr();

    virtual void checkCollectives(llvm::Function &F, llvm::PostDominatorTree &PDT);
    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;
    virtual bool runOnFunction(llvm::Function &F);

  private:
  };
}

#endif /* PARCOACH_H */
