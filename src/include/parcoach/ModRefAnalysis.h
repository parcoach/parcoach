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
                       ExtInfo const &extInfo, llvm::Module &M);
  ~ModRefAnalysisResult();

  MemRegSet getFuncMod(const llvm::Function *F) const;
  MemRegSet getFuncRef(const llvm::Function *F) const;
  MemRegSet getFuncKill(const llvm::Function *F) const;
  bool inGlobalKillSet(MemReg *R) const;

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
  llvm::ValueMap<const llvm::Function *, MemRegSet> funcModMap;
  llvm::ValueMap<const llvm::Function *, MemRegSet> funcRefMap;
  llvm::ValueMap<const llvm::Function *, MemRegSet> funcLocalMap;
  llvm::ValueMap<const llvm::Function *, MemRegSet> funcKillMap;
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
