#ifndef MODREFANALYSIS
#define MODREFANALYSIS

#include "ExtInfo.h"
#include "MemoryRegion.h"
#include "PTACallGraph.h"
#include "andersen/Andersen.h"

#include "llvm/IR/InstVisitor.h"

class ModRefAnalysis : public llvm::InstVisitor<ModRefAnalysis> {
public:
  ModRefAnalysis(PTACallGraph &CG, Andersen *PTA, ExtInfo *extInfo);
  ~ModRefAnalysis();

  MemRegSet getFuncMod(const llvm::Function *F);
  MemRegSet getFuncRef(const llvm::Function *F);
  MemRegSet getFuncKill(const llvm::Function *F);

  void visitAllocaInst(llvm::AllocaInst &I);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitCallSite(llvm::CallSite CS);

  void dump();

  MemRegSet globalKillSet;

private:
  void analyze();

  const llvm::Function *curFunc;
  PTACallGraph &CG;
  Andersen *PTA;
  ExtInfo *extInfo;
  std::map<const llvm::Function *, MemRegSet> funcModMap;
  std::map<const llvm::Function *, MemRegSet> funcRefMap;
  std::map<const llvm::Function *, MemRegSet> funcLocalMap;
  std::map<const llvm::Function *, MemRegSet> funcKillMap;
};

#endif /* MODREFANALYSIS */
