#pragma once

#include "parcoach/ExtInfo.h"
#include "parcoach/andersen/Andersen.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/IR/ValueMap.h"

#include <map>

class DepGraphDCF;
class PTACallGraph;
class MSSAMu;
class MSSAChi;
class MSSAPhi;
class MSSAVar;
class MemReg;

namespace llvm {
class PostDominatorTree;
}

namespace parcoach {
class ModRefAnalysisResult;
class MemorySSA {

  // Containers for Mu,Chi,Phi,BB and Values
  using MuSet = std::set<MSSAMu *>;
  using ChiSet = std::set<MSSAChi *>;
  using PhiSet = std::set<MSSAPhi *>;
  using BBSet = std::set<const llvm::BasicBlock *>;
  using ValueSet = std::set<const llvm::Value *>;
  using MemRegSet = std::set<MemReg *>;

  // Chi and Mu annotations
  using LoadToMuMap = llvm::ValueMap<const llvm::LoadInst *, MuSet>;
  using StoreToChiMap = llvm::ValueMap<const llvm::StoreInst *, ChiSet>;
  using CallSiteToMuSetMap = llvm::ValueMap<const llvm::CallBase *, MuSet>;
  using CallSiteToChiSetMap = llvm::ValueMap<llvm::CallBase *, ChiSet>;
  using FuncCallSiteToChiMap =
      llvm::ValueMap<const llvm::Function *,
                     std::map<llvm::CallBase *, MSSAChi *>>;
  using FuncCallSiteToArgChiMap =
      llvm::ValueMap<const llvm::Function *,
                     std::map<llvm::CallBase *, std::map<unsigned, MSSAChi *>>>;

  // Phis
  using BBToPhiMap = llvm::ValueMap<const llvm::BasicBlock *, PhiSet>;
  using MemRegToBBMap = std::map<MemReg *, BBSet>;
  using LLVMPhiToPredMap = llvm::ValueMap<const llvm::PHINode *, ValueSet>;

  // Map functions to entry Chi set and return Mu set
  using FunToEntryChiMap = llvm::ValueMap<const llvm::Function *, ChiSet>;
  using FunToReturnMuMap = llvm::ValueMap<const llvm::Function *, MuSet>;

  using FunRegToEntryChiMap =
      llvm::ValueMap<const llvm::Function *, std::map<MemReg *, MSSAChi *>>;
  using FunRegToReturnMuMap =
      llvm::ValueMap<const llvm::Function *, std::map<MemReg *, MSSAMu *>>;

  using FuncToChiMap = llvm::ValueMap<const llvm::Function *, MSSAChi *>;
  using FuncToArgChiMap =
      llvm::ValueMap<const llvm::Function *, std::map<unsigned, MSSAChi *>>;
  using FuncToCallBaseSet =
      llvm::ValueMap<const llvm::Function *, std::set<llvm::CallBase *>>;

public:
  MemorySSA(llvm::Module &M, Andersen const &PTA, PTACallGraph const &CG,
            ModRefAnalysisResult *MRA, ExtInfo *extInfo,
            llvm::ModuleAnalysisManager &AM);
  virtual ~MemorySSA();

  void printTimers() const;

private:
  void dumpMSSA(const llvm::Function *F);

  void buildSSA(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  void buildSSA(const llvm::Function *F, llvm::DominatorTree &DT,
                llvm::DominanceFrontier &DF, llvm::PostDominatorTree &PDT);

  void createArtificalChiForCalledFunction(llvm::CallBase *CB,
                                           const llvm::Function *callee);

  void computeMuChi(const llvm::Function *F);

  void computeMuChiForCalledFunction(llvm::CallBase *inst,
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
  Andersen const &PTA;
  PTACallGraph const &CG;
  ModRefAnalysisResult *MRA;
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
  FuncCallSiteToChiMap extCallSiteToCalleeRetChi;
  FuncCallSiteToChiMap extCallSiteToVarArgEntryChi;
  FuncCallSiteToChiMap extCallSiteToVarArgExitChi;
  FuncCallSiteToArgChiMap extCallSiteToArgEntryChi;
  FuncCallSiteToArgChiMap extCallSiteToArgExitChi;
  FuncToCallBaseSet extFuncToCSMap;

public:
  auto const &getLoadToMuMap() const { return loadToMuMap; }
  auto const &getStoreToChiMap() const { return storeToChiMap; }
  auto const &getCSToMuMap() const { return callSiteToMuMap; }
  auto const &getCSToChiMap() const { return callSiteToChiMap; };
  auto const &getCSToSynChiMap() const { return callSiteToSyncChiMap; };
  auto const &getBBToPhiMap() const { return bbToPhiMap; };
  auto const &getFunToEntryChiMap() const { return funToEntryChiMap; };
  auto const &getPhiToPredMap() const { return llvmPhiToPredMap; }
  auto const &getFunRegToEntryChiMap() const { return funRegToEntryChiMap; }
  auto const &getFunRegToReturnMuMap() const { return funRegToReturnMuMap; }
  auto const &getExtCSToCallerRetChi() const {
    return extCallSiteToCallerRetChi;
  }
  auto const &getExtCSToCalleeRetChi() const {
    return extCallSiteToCalleeRetChi;
  }
  auto const &getExtCSToVArgEntryChi() const {
    return extCallSiteToVarArgEntryChi;
  }
  auto const &getExtCSToVArgExitChi() const {
    return extCallSiteToVarArgExitChi;
  }
  auto const &getExtCSToArgEntryChi() const { return extCallSiteToArgEntryChi; }
  auto const &getExtCSToArgExitChi() const { return extCallSiteToArgExitChi; }
  auto const &getExtFuncToCSMap() const { return extFuncToCSMap; }
};

class MemorySSAAnalysis : public llvm::AnalysisInfoMixin<MemorySSAAnalysis> {
  friend llvm::AnalysisInfoMixin<MemorySSAAnalysis>;
  static llvm::AnalysisKey Key;

public:
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<MemorySSA>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
} // namespace parcoach
