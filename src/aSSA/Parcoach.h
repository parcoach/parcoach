//===- Parcoach.h - Parcoach module pass ------------------------*- C++ -*-===//
//
// This file defines the ParcoachInstr class.
//
//===----------------------------------------------------------------------===//

#ifndef PARCOACH_H
#define PARCOACH_H

#include "PTACallGraph.h"
#include "ParcoachAnalysisInter.h"
#include "parcoach/MemoryRegion.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace parcoach {
class ParcoachInstr {
public:
  ParcoachInstr(llvm::ModuleAnalysisManager &AM);

  virtual bool runOnModule(llvm::Module &M);

private:
  llvm::ModuleAnalysisManager &MAM;

  typedef bool Preheader;
  typedef std::map<const llvm::BasicBlock *, Preheader> BBPreheaderMap;
  BBPreheaderMap bbPreheaderMap;

  std::map<llvm::Instruction *, llvm::Instruction *> ompNewInst2oldInst;

  void doFinalization(llvm::Module &M, ParcoachAnalysisInter const &);

  void replaceOMPMicroFunctionCalls(llvm::Module &M);
  void revertOmpTransformation();

  void cudaTransformation(llvm::Module &M);
};

} // namespace parcoach

#endif /* PARCOACH_H */
