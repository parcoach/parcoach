//===- Parcoach.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "PTACallGraph.h"
#include "ParcoachAnalysisInter.h"

#include "llvm/Analysis/CallGraphSCCPass.h"
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
      tstart_assa, tend_assa, tstart_depgraph, tend_depgraph, tstart_flooding,
      tend_flooding, tstart_parcoach, tend_parcoach;
  ParcoachInstr(llvm::ModuleAnalysisManager &AM);

  virtual bool doInitialization(llvm::Module &M);
  virtual bool doFinalization(llvm::Module &M);
  virtual bool runOnModule(llvm::Module &M);

private:
  ParcoachAnalysisInter *PAInter;
  llvm::ModuleAnalysisManager &MAM;

  typedef bool Preheader;
  typedef std::map<const llvm::BasicBlock *, Preheader> BBPreheaderMap;
  BBPreheaderMap bbPreheaderMap;

  std::map<llvm::Instruction *, llvm::Instruction *> ompNewInst2oldInst;

  void replaceOMPMicroFunctionCalls(
      llvm::Module &M,
      std::map<const llvm::Function *, std::set<const llvm::Value *>>
          &func2SharedVarMap);
  void revertOmpTransformation();

  void cudaTransformation(llvm::Module &M);
};

struct ParcoachPass : public llvm::PassInfoMixin<ParcoachPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace parcoach

#endif /* PARCOACH_H */
