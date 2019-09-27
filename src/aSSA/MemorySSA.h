#ifndef MEMORYSSA_H
#define MEMORYSSA_H

#include "ExtInfo.h"
#include "MSSAMuChi.h"
#include "PTACallGraph.h"
#include "andersen/Andersen.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/IR/CallSite.h"

#include <map>

class DepGraphDCF;
class ModRefAnalysis;

class MemorySSA {
  friend class DepGraphDCF;

  // Containers for Mu,Chi,Phi,BB and Values
  typedef std::set<MSSAMu *> MuSet;
  typedef std::set<MSSAChi *> ChiSet;
  typedef std::set<MSSAPhi *> PhiSet;
  typedef std::set<const llvm::BasicBlock *> BBSet;
  typedef std::set<const llvm::Value *> ValueSet;
  typedef std::set<MemReg *> MemRegSet;

  // Chi and Mu annotations
  typedef std::map<const llvm::LoadInst *, MuSet> LoadToMuMap;
  typedef std::map<const llvm::StoreInst *, ChiSet> StoreToChiMap;
  typedef std::map<llvm::CallSite, MuSet> CallSiteToMuSetMap;
  typedef std::map<llvm::CallSite, ChiSet> CallSiteToChiSetMap;
  typedef std::map<const llvm::Function *, std::map<llvm::CallSite, MSSAChi *>>
      FuncCallSiteToChiMap;
  typedef std::map<const llvm::Function *,
                   std::map<llvm::CallSite, std::map<unsigned, MSSAChi *>>>
      FuncCallSiteToArgChiMap;

  // Phis
  typedef std::map<const llvm::BasicBlock *, PhiSet> BBToPhiMap;
  typedef std::map<MemReg *, BBSet> MemRegToBBMap;
  typedef std::map<const llvm::PHINode *, ValueSet> LLVMPhiToPredMap;

  // Map functions to entry Chi set and return Mu set
  typedef std::map<const llvm::Function *, ChiSet> FunToEntryChiMap;
  typedef std::map<const llvm::Function *, MuSet> FunToReturnMuMap;

  typedef std::map<const llvm::Function *, std::map<MemReg *, MSSAChi *>>
      FunRegToEntryChiMap;
  typedef std::map<const llvm::Function *, std::map<MemReg *, MSSAMu *>>
      FunRegToReturnMuMap;

  typedef std::map<const llvm::Function *, MSSAChi *> FuncToChiMap;
  typedef std::map<const llvm::Function *, std::map<unsigned, MSSAChi *>>
      FuncToArgChiMap;

public:
  MemorySSA(llvm::Module *m, Andersen *PTA, PTACallGraph *CG,
            ModRefAnalysis *MRA, ExtInfo *extInfo);
  virtual ~MemorySSA();

  void buildSSA(const llvm::Function *F, llvm::DominatorTree &DT,
                llvm::DominanceFrontier &DF, llvm::PostDominatorTree &PDT);

  void dumpMSSA(const llvm::Function *F);

  void printTimers() const;

private:
  void createArtificalChiForCalledFunction(llvm::CallSite CS,
                                           const llvm::Function *callee);

  void computeMuChi(const llvm::Function *F);

  void computeMuChiForCalledFunction(const llvm::Instruction *inst,
                                     llvm::Function *callee);

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
                std::map<MemReg *, unsigned> &C,
                std::map<MemReg *, std::vector<MSSAVar *>> &S);

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
  PTACallGraph *CG;
  ModRefAnalysis *MRA;
  ExtInfo *extInfo;

  llvm::DominanceFrontier *curDF;
  llvm::DominatorTree *curDT;
  llvm::PostDominatorTree *curPDT;
  MemRegSet usedRegs;
  MemRegToBBMap regDefToBBMap;

  LoadToMuMap loadToMuMap;
  StoreToChiMap storeToChiMap;
  CallSiteToMuSetMap callSiteToMuMap;
  CallSiteToChiSetMap callSiteToChiMap;
  CallSiteToChiSetMap callSiteToSyncChiMap;
  BBToPhiMap bbToPhiMap;
  FunToEntryChiMap funToEntryChiMap;
  FunToReturnMuMap funToReturnMuMap;
  LLVMPhiToPredMap llvmPhiToPredMap;

  FunRegToEntryChiMap funRegToEntryChiMap;
  FunRegToReturnMuMap funRegToReturnMuMap;

  // External function artifical chis.
  CallSiteToChiSetMap extCallSiteToCallerRetChi; // inside caller (necessary
  // because there is no mod/ref analysis for external functions).

  FuncCallSiteToChiMap extCallSiteToVarArgEntryChi;
  FuncCallSiteToChiMap extCallSiteToVarArgExitChi;
  FuncCallSiteToArgChiMap extCallSiteToArgEntryChi;
  FuncCallSiteToArgChiMap extCallSiteToArgExitChi;
  FuncCallSiteToChiMap extCallSiteToCalleeRetChi;
  std::map<const llvm::Function *, std::set<llvm::CallSite>> extFuncToCSMap;
};

#endif /* MEMORYSSA_H */
