#ifndef MODREFANALYSIS
#define MODREFANALYSIS

#include "MemoryRegion.h"
#include "PTACallGraph.h"
#include "parcoach/andersen/Andersen.h"
#include "parcoach/ExtInfo.h"

#include "llvm/IR/InstVisitor.h"

class ModRefAnalysis : public llvm::InstVisitor<ModRefAnalysis> {
public:
  ModRefAnalysis(PTACallGraph const &CG, Andersen const &PTA, parcoach::ExtInfo *extInfo);
  ~ModRefAnalysis();

  MemRegSet getFuncMod(const llvm::Function *F);
  MemRegSet getFuncRef(const llvm::Function *F);
  MemRegSet getFuncKill(const llvm::Function *F);

  void visitAllocaInst(llvm::AllocaInst &I);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitCallBase(llvm::CallBase &CB);

  void dump();

  MemRegSet globalKillSet;

private:
  void analyze();

  const llvm::Function *curFunc;
  PTACallGraph const &CG;
  Andersen const &PTA;
  parcoach::ExtInfo *extInfo;
  std::map<const llvm::Function *, MemRegSet> funcModMap;
  std::map<const llvm::Function *, MemRegSet> funcRefMap;
  std::map<const llvm::Function *, MemRegSet> funcLocalMap;
  std::map<const llvm::Function *, MemRegSet> funcKillMap;
};

#endif /* MODREFANALYSIS */
