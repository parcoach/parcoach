//===- Parcoach.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "PTACallGraph.h"
#include "ParcoachAnalysisInter.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace parcoach {
class ParcoachInstr {
public:
  /* timers */
  static double tstart, tend, tstart_aa, tend_aa, tstart_pta, tend_pta,
      tstart_regcreation, tend_regcreation, tstart_modref, tend_modref,
      tstart_assa, tend_assa, tstart_depgraph, tend_depgraph, tstart_parcoach,
      tend_parcoach;
  ParcoachInstr(llvm::ModuleAnalysisManager &AM);

  virtual bool runOnModule(llvm::Module &M);

private:
  llvm::ModuleAnalysisManager &MAM;

  typedef bool Preheader;
  typedef std::map<const llvm::BasicBlock *, Preheader> BBPreheaderMap;
  BBPreheaderMap bbPreheaderMap;

  std::map<llvm::Instruction *, llvm::Instruction *> ompNewInst2oldInst;

  bool doInitialization(llvm::Module &M);
  bool doFinalization(llvm::Module &M, ParcoachAnalysisInter const &);

  void replaceOMPMicroFunctionCalls(
      llvm::Module &M,
      std::map<const llvm::Function *, std::set<const llvm::Value *>>
          &func2SharedVarMap);
  void revertOmpTransformation();

  void cudaTransformation(llvm::Module &M);
};

} // namespace parcoach

#endif /* PARCOACH_H */
