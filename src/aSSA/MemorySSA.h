#ifndef MEMORYSSA_H
#define MEMORYSSA_H

#include "andersen/Andersen.h"
#include "MSSAMuChi.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/IR/CallSite.h"

#include <map>

class DepGraph;
class ModRefAnalysis;

class MemorySSA {
  friend class DepGraph;

  // Containers for Mu,Chi,Phi,BB and Values
  typedef std::set<MSSAMu *> MuSet;
  typedef std::set<MSSAChi *> ChiSet;
  typedef std::set<MSSAPhi *> PhiSet;
  typedef std::set<const llvm::BasicBlock *> BBSet;
  typedef std::set<const llvm::Value *> ValueSet;
  typedef std::set<MemReg *> MemRegSet;

  // Chi and Mu annotations
  typedef llvm::DenseMap<const llvm::LoadInst *, MuSet> LoadToMuMap;
  typedef llvm::DenseMap<const llvm::StoreInst *, ChiSet> StoreToChiMap;
  typedef std::map<llvm::CallSite, MuSet> CallSiteToMuMap;
  typedef std::map<llvm::CallSite, ChiSet> CallSiteToChiMap;

  // Phis
  typedef llvm::DenseMap<const llvm::BasicBlock *, PhiSet> BBToPhiMap;
  typedef llvm::DenseMap<MemReg *, BBSet> MemRegToBBMap;
  typedef llvm::DenseMap<const llvm::PHINode *, ValueSet> LLVMPhiToPredMap;

  // Map functions to entry Chi set and return Mu set
  typedef llvm::DenseMap<const llvm::Function *, ChiSet> FunToEntryChiMap;
  typedef llvm::DenseMap<const llvm::Function *, MuSet> FunToReturnMuMap;

  typedef llvm::DenseMap<const llvm::Function *,
			 llvm::DenseMap<MemReg *, MSSAChi *> >
  FunRegToEntryChiMap;
  typedef llvm::DenseMap<const llvm::Function *,
			 llvm::DenseMap<MemReg *, MSSAMu *> >
  FunRegToReturnMuMap;

  typedef llvm::DenseMap<const llvm::Function *, MSSAChi *> FuncToChiMap;
  typedef llvm::DenseMap<const llvm::Function *,
			 llvm::DenseMap<unsigned, MSSAChi *> > FuncToArgChiMap;

public:
  MemorySSA(llvm::Module *m, Andersen *PTA, ModRefAnalysis *MRA);
  virtual ~MemorySSA();

  void buildSSA(const llvm::Function *F, llvm::DominatorTree &DT,
		llvm::DominanceFrontier &DF, llvm::PostDominatorTree &PDT);
  void buildExtSSA(const llvm::Function *F);

  void dumpMSSA(const llvm::Function *F);

  void printTimers() const;

private:
  void computeMuChi(const llvm::Function *F);

  // The three following functions generate SSA from mu/chi by implementing the
  // algorithm from the paper:
  // R. Cytron, J. Ferrante, B. K. Rosen, M. N. Wegman, and F. K.
  // Zadeck, “Efficiently computing static single assignment form and
  // the control dependence graph,” ACM Trans. Program. Lang. Syst.,
  // vol. 13, no. 4, pp. 451–490, Oct. 1991.
  // http://doi.acm.org/10.1145/115372.115320
  void computePhi(const llvm::Function *F);
  void rename(const llvm::Function *F);
  void renameBB(const llvm::Function *F, const llvm::BasicBlock *X,
		llvm::DenseMap<MemReg *, unsigned> &C,
		llvm::DenseMap<MemReg *, std::vector<MSSAVar *> > &S);

  void computePhiPredicates(const llvm::Function *F);
  void computeLLVMPhiPredicates(const llvm::PHINode *phi);
  void computeMSSAPhiPredicates(MSSAPhi *phi);

  unsigned whichPred(const llvm::BasicBlock *pred,
		     const llvm::BasicBlock *succ) const;

  double computeMuChiTime;
  double computePhiTime;
  double renameTime;
  double computePhiPredicatesTime;

protected:
  llvm::Module *m;
  Andersen *PTA;
  ModRefAnalysis *MRA;

  llvm::DominanceFrontier *curDF;
  llvm::DominatorTree *curDT;
  llvm::PostDominatorTree *curPDT;
  MemRegSet usedRegs;
  MemRegToBBMap regDefToBBMap;

  LoadToMuMap loadToMuMap;
  StoreToChiMap storeToChiMap;
  CallSiteToMuMap callSiteToMuMap;
  CallSiteToChiMap callSiteToChiMap;
  BBToPhiMap bbToPhiMap;
  FunToEntryChiMap funToEntryChiMap;
  FunToReturnMuMap funToReturnMuMap;
  LLVMPhiToPredMap llvmPhiToPredMap;

  FunRegToEntryChiMap funRegToEntryChiMap;
  FunRegToReturnMuMap funRegToReturnMuMap;

  // External function artifical chis.
  CallSiteToChiMap extCallSiteToRetChiMap;
  FuncToChiMap extVarArgEntryChi;
  FuncToChiMap extVarArgExitChi;
  FuncToArgChiMap extArgEntryChi;
  FuncToArgChiMap extArgExitChi;
  FuncToChiMap extRetChi;
};

#endif /* MEMORYSSA_H */
