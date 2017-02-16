#ifndef MODREFANALYSIS
#define MODREFANALYSIS

#include "andersen/Andersen.h"
#include "MemoryRegion.h"
#include "PTACallGraph.h"

#include "llvm/IR/InstVisitor.h"

class ModRefAnalysis : public llvm::InstVisitor<ModRefAnalysis> {
public:
  ModRefAnalysis(PTACallGraph &CG, Andersen *PTA);
  ~ModRefAnalysis();

  MemRegSet getFuncMod(const llvm::Function *F);
  MemRegSet getFuncRef(const llvm::Function *F);

  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitCallSite(llvm::CallSite CS);

  void dump();

private:
  void analyze();

  const llvm::Function *curFunc;
  PTACallGraph &CG;
  Andersen *PTA;
  llvm::DenseMap<const llvm::Function *, MemRegSet> funcModMap;
  llvm::DenseMap<const llvm::Function *, MemRegSet> funcRefMap;
};

#endif /* MODREFANALYSIS */
