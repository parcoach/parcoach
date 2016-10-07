#ifndef FUNCTIONSUMMARY_H
#define FUNCTIONSUMMARY_H

#include "GlobalDepGraph.h"
#include "InterDepGraph.h"
#include "IntraDepGraph.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

class FunctionSummary;

typedef std::map<llvm::Function *, FunctionSummary *> FuncSummaryMapTy;


class FunctionSummary {
  friend class GlobalDepGraph;

public:
  FunctionSummary(llvm::Function *F,
		  FuncSummaryMapTy *funcMap,
		  InterDepGraph *interDeps,
		  llvm::Pass *pass);
  ~FunctionSummary();


  // First pass. It fills listeners map and detects memory locations
  // depending on MPI_Comm_rank.
  void firstPass();

  // Update dependencies.
  bool updateDeps();

  // Check collectives and print warning if execution of the collective
  // depends on rank value.
  void checkCollectives();

  llvm::StringRef getFuncName() {
    return F->getName();
  }

  void toDot(llvm::StringRef filename) {
    intraDeps.toDot(filename);
  }

protected:
  /* Dependencies */

  InterDepGraph *interDeps;
  IntraDepGraph intraDeps;

  /* Listeners to notify return value and side effect dependencies. */
  typedef llvm::DenseMap<llvm::MemoryLocation, FunctionSummary *> ListenerMapTy;

  /* Return value dependencies */

  ListenerMapTy retUserMap; // Memory location of all users of return value
  // inside calling functions.
  bool isRetDependent;
  llvm::MemoryLocation retDepSrc;


  /* Side effect dependencies */

  ListenerMapTy *argUserMap; // Memory location of all users of arguments
  // inside calling functions.
  bool *isArgDependent;
  llvm::MemoryLocation *argDepSrc;

private:
  llvm::Function *F;
  FuncSummaryMapTy *funcMap;

  // Analyses
  llvm::Pass *pass;
  llvm::Module *MD;
  llvm::DominatorTree *DT;
  llvm::PostDominatorTree *PDT;
  llvm::AliasAnalysis *AA;
  llvm::TargetLibraryInfo *TLI;

  // DenseMap <BasicBlock *, vector<BasicBlock *> *> IPDFs.
  // This map allows us to avoid recomputing the IPDF of the same basic block
  // several times.
  llvm::DenseMap<const llvm::BasicBlock *,
		 std::vector<llvm::BasicBlock *>> iPDFMap;
  std::vector<llvm::BasicBlock *> getIPDF(llvm::BasicBlock *bb);

  // For each basic block in the iterated post dominance frontier,
  // check if the condition depends on the rank.
  bool is_IPDF_rank_dependent(std::vector<llvm::BasicBlock *> &IPDF,
			      llvm::MemoryLocation *src = NULL);

  // Check if the condition of the basic block depends on the rank.
  bool is_BB_rank_dependent(const llvm::BasicBlock *BB,
			    llvm::MemoryLocation *src = NULL);

  // Check if a value depends on the rank.
  // There is no interprocedural analysis:
  // - Arguments of the function are assumed to be independent of the rank.
  // - Calls to functions other than MPI_comm_rank() are not checked.
  bool is_value_rank_dependent(const llvm::Value *value,
			       llvm::MemoryLocation *src = NULL);

  // Find memory locations where value depends on the rank.
  // There is no interprocedural analysis:
  // - Arguments of the function are assumed to be independent of the rank.
  // - Calls to functions other than MPI_comm_rank() are not checked.
  //  void findMemoryDep(const llvm::Function &F);
  bool updateAliasDep(const llvm::Function &F);
  bool updateFlowDep(const llvm::Function &F);

  bool updateArgDep();
  bool updateRetDep();

  void findMPICommRankRoots();
  void computeListeners();

};


#endif /* FUNCTIONSUMMARY_H */
