#pragma once

#include "parcoach/ExtInfo.h"
#include "parcoach/MemoryRegion.h"
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

namespace llvm {
class PostDominatorTree;
}

namespace parcoach {
class ModRefAnalysisResult;
class MemorySSA {

  // Containers for Mu,Chi,Phi,BB and Values
  using MuOwnerSet = std::vector<std::unique_ptr<MSSAMu>>;
  using ChiOwnerSet = std::vector<std::unique_ptr<MSSAChi>>;
  using PhiOwnerSet = std::vector<std::unique_ptr<MSSAPhi>>;
  using MuSet = std::set<MSSAMu *>;
  using ChiSet = std::set<MSSAChi *>;
  using PhiSet = std::set<MSSAPhi *>;
  using BBSet = std::set<llvm::BasicBlock const *>;
  using ValueSet = std::set<llvm::Value const *>;

  // Chi and Mu annotations
  using LoadToMuMap = llvm::ValueMap<llvm::LoadInst const *, MuOwnerSet>;
  using StoreToChiMap = llvm::ValueMap<llvm::StoreInst const *, ChiOwnerSet>;
  using CallSiteToMuSetMap = llvm::ValueMap<llvm::CallBase const *, MuOwnerSet>;
  using CallSiteToChiSetMap = llvm::ValueMap<llvm::CallBase *, ChiOwnerSet>;
  using FuncCallSiteToChiMap =
      llvm::ValueMap<llvm::Function const *,
                     std::map<llvm::CallBase *, std::unique_ptr<MSSAChi>>>;
  using FuncCallSiteToArgChiMap =
      llvm::ValueMap<llvm::Function const *,
                     std::map<llvm::CallBase *, std::map<unsigned, MSSAChi *>>>;

  // Phis
  using BBToPhiMap = llvm::ValueMap<llvm::BasicBlock const *, PhiOwnerSet>;
  using MemRegToBBMap = std::map<MemRegEntry *, BBSet>;
  using LLVMPhiToPredMap = llvm::ValueMap<llvm::PHINode const *, ValueSet>;

  // Map functions to entry Chi set and return Mu set
  using FunToEntryChiMap = llvm::ValueMap<llvm::Function const *, ChiOwnerSet>;
  using FunToReturnMuMap = llvm::ValueMap<llvm::Function const *, MuOwnerSet>;

  using FunRegToEntryChiMap =
      llvm::ValueMap<llvm::Function const *,
                     std::map<MemRegEntry *, MSSAChi *>>;
  using FunRegToReturnMuMap =
      llvm::ValueMap<llvm::Function const *, std::map<MemRegEntry *, MSSAMu *>>;

  using FuncToChiMap = llvm::ValueMap<llvm::Function const *, MSSAChi *>;
  using FuncToArgChiMap =
      llvm::ValueMap<llvm::Function const *, std::map<unsigned, MSSAChi *>>;
  using FuncToCallBaseSet =
      llvm::ValueMap<llvm::Function const *, std::set<llvm::CallBase *>>;

public:
  MemorySSA(llvm::Module &M, Andersen const &PTA, PTACallGraph const &CG,
            MemReg const &Regions, ModRefAnalysisResult *MRA,
            ExtInfo const &extInfo, llvm::ModuleAnalysisManager &AM);
  virtual ~MemorySSA();

private:
  void dumpMSSA(llvm::Function const *F);

  void buildSSA(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  void buildSSA(llvm::Function const *F, llvm::DominatorTree &DT,
                llvm::DominanceFrontier &DF, llvm::PostDominatorTree &PDT);

  void createArtificalChiForCalledFunction(llvm::CallBase *CB,
                                           llvm::Function const *callee);

  void computeMuChi(llvm::Function const *F);

  void computeMuChiForCalledFunction(llvm::CallBase *inst,
                                     llvm::Function *callee);

  // The three following functions generate SSA from mu/chi by implementing the
  // algorithm from the paper:
  // R. Cytron, J. Ferrante, B. K. Rosen, M. N. Wegman, and F. K.
  // Zadeck, “Efficiently computing static single assignment form and
  // the control dependence graph,” ACM Trans. Program. Lang. Syst.,
  // vol. 13, no. 4, pp. 451–490, Oct. 1991.
  // http://doi.acm.org/10.1145/115372.115320
  void computePhi(llvm::Function const *F);
  void rename(llvm::Function const *F);
  void renameBB(llvm::Function const *F, llvm::BasicBlock const *X,
                std::map<MemRegEntry *, unsigned> &C,
                std::map<MemRegEntry *, std::vector<MSSAVar *>> &S);

  void computePhiPredicates(llvm::Function const *F);
  void computeLLVMPhiPredicates(llvm::PHINode const *phi);
  void computeMSSAPhiPredicates(MSSAPhi *phi);

  static unsigned whichPred(llvm::BasicBlock const *pred,
                            llvm::BasicBlock const *succ);

protected:
  Andersen const &PTA;
  PTACallGraph const &CG;
  MemReg const &Regions;
  ModRefAnalysisResult *MRA;
  ExtInfo const &extInfo;

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
  // Owner of chis allocated in the 2 ArgChi maps above; the data structure
  // makes it impossible to hold the unique_ptr in a map in a map in a map...
  ChiOwnerSet AllocatedArgChi;
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
  static Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
} // namespace parcoach
