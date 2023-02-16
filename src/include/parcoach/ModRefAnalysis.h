#pragma once

#include "MemoryRegion.h"
#include "parcoach/ExtInfo.h"
#include "parcoach/andersen/Andersen.h"

#include "llvm/IR/InstVisitor.h"

class PTACallGraph;

namespace parcoach {

class ModRefAnalysisResult : public llvm::InstVisitor<ModRefAnalysisResult> {
public:
  ModRefAnalysisResult(PTACallGraph const &CG, Andersen const &PTA,
                       ExtInfo const &extInfo, MemReg const &Regions,
                       llvm::Module &M);
  ~ModRefAnalysisResult();

  MemRegSet getFuncMod(llvm::Function const *F) const;
  MemRegSet getFuncRef(llvm::Function const *F) const;
  MemRegSet getFuncKill(llvm::Function const *F) const;
  bool inGlobalKillSet(MemRegEntry *R) const;

  void visitAllocaInst(llvm::AllocaInst &I);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitCallBase(llvm::CallBase &CB);

#ifndef NDEBUG
  void dump() const;
#endif

private:
  void analyze(llvm::Module &M);

  llvm::Function *curFunc;
  PTACallGraph const &CG;
  Andersen const &PTA;
  ExtInfo const &extInfo;
  MemReg const &Regions;
  llvm::ValueMap<llvm::Function const *, MemRegSet> funcModMap;
  llvm::ValueMap<llvm::Function const *, MemRegSet> funcRefMap;
  llvm::ValueMap<llvm::Function const *, MemRegSet> funcLocalMap;
  llvm::ValueMap<llvm::Function const *, MemRegSet> funcKillMap;
  MemRegSet globalKillSet;
};

class ModRefAnalysis : public llvm::AnalysisInfoMixin<ModRefAnalysis> {
  friend llvm::AnalysisInfoMixin<ModRefAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = std::unique_ptr<ModRefAnalysisResult>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
