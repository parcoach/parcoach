#ifndef MEMSSA_H
#define MEMSSA_H

#include "DepGraph.h"
#include "MSSAMuChi.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/DominanceFrontier.h"

/* Implement algorithm from:
   Cytron et al. - 1989 - An Efficient Method of Computing Static Single
   Assignment Form
   to compute Memory SSA.
*/

class MemSSA {
  typedef std::set<MSSAMu *> MuSet;
  typedef std::set<MSSAChi *> ChiSet;
  typedef std::set<MSSAPhi *> PhiSet;
  typedef std::set<const llvm::BasicBlock *> BBSet;
  typedef std::set<const llvm::Value *> ValueSet;

  typedef llvm::DenseMap<const llvm::LoadInst *, MuSet> LoadToMuMap;
  typedef llvm::DenseMap<const llvm::StoreInst *, ChiSet> StoreToChiMap;
  typedef llvm::DenseMap<const llvm::CallInst *,
    llvm::DenseMap<unsigned, MuSet> > CallToMuMap;
  typedef llvm::DenseMap<const llvm::CallInst *,
    llvm::DenseMap<unsigned, ChiSet> > CallToChiMap;
  typedef llvm::DenseMap<const llvm::ReturnInst *, MuSet> RetToMuMap;
  typedef llvm::DenseMap<const llvm::BasicBlock *, PhiSet> BBToPhiMap;
  typedef llvm::DenseMap<const llvm::Function *, ChiSet> FunToEntryChiMap;
  typedef llvm::DenseMap<const llvm::Function *, MuSet> FunToReturnMuMap;
  typedef llvm::DenseMap<const Region *, BBSet> RegToBBMap;
  typedef llvm::DenseMap<const llvm::PHINode *, ValueSet> LLVMPhiASSA;
  typedef llvm::DenseMap<MSSAPhi *, ValueSet> MSSAPhiASSA;

 public:
  MemSSA(const llvm::Function *F, DepGraph *pg,
	 llvm::PostDominatorTree &PDT,
	 llvm::DominanceFrontier &DF, llvm::DominatorTree &DT,
	 std::vector<Region *> &regions);
  virtual ~MemSSA();

  void computeMuChi();
  void computePhi();
  void rename();
  void computeASSA();
  void dump();

 private:
  void getValueRegions(std::vector<Region *> &regs, const llvm::Value *v);
  void renameBB(const llvm::BasicBlock *X,
		llvm::DenseMap<Region *, unsigned> &C,
		llvm::DenseMap<Region *, std::vector<unsigned> > &S);
  unsigned whichPred(const llvm::BasicBlock *pred,
		     const llvm::BasicBlock *succ) const;

  const llvm::Function *F;
  DepGraph *pg;
  llvm::PostDominatorTree &PDT;
  llvm::DominanceFrontier &DF;
  llvm::DominatorTree &DT;
  std::vector<Region *> &regions;
  std::map<const llvm::Value *, Region *> regionMap;

  LoadToMuMap loadToMuMap;
  StoreToChiMap storeToChiMap;
  CallToMuMap callToMuMap;
  CallToChiMap callToChiMap;
  RetToMuMap retToMuMap;

  RegToBBMap regDefToBBMap;
  BBToPhiMap bbToPhiMap;

  FunToEntryChiMap funToEntryChiMap;
  FunToReturnMuMap funToReturnMuMap;

  LLVMPhiASSA llvmPhiASSA;
  MSSAPhiASSA mssaPhiASSA;

};

#endif /* MEMSSA_H */
