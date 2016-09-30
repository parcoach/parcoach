/*
 * This contains the analysis of a function.
 * It stores all memory location dependent of the rank.
 */

#ifndef FUNCTIONSUMMARY_H
#define FUNCTIONSUMMARY_H

#include "DependencyGraph.h"
#include "InterDependenceMap.h"
#include "GlobalGraph.h"

#include <llvm/ADT/SetVector.h>
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

class FunctionSummary {
  friend class GlobalGraph;

 public:
  FunctionSummary(llvm::Function *F,
		  const char *ProgName,
		  InterDependenceMap &interDepMap,
		  llvm::Pass *pass);

  ~FunctionSummary();

  // Update inter dep map.
  bool updateInterDep();

  // Check collectives and print warning if execution of the collective
  // depends on rank value.
  void checkCollectives();


  void toDot(llvm::StringRef filename);

 private:
  const char *ProgName;
  llvm::Pass *pass;

  // Analyses
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

  // Map containing dependencies between functions.
  InterDependenceMap &interDepMap;

  llvm::Function *F;

  // Dependency graph of the function analyzed.
  DependencyGraph depGraph;

  int STAT_warnings;
  int STAT_collectives;

  // First pass which is run by the constructor.
  // It detects all memory locations depending on rank and udpates
  // the inter dep map accordingly.
  void firstPass();

  bool updateArgMap();

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
  bool findAliasDep(const llvm::Function &F);
  bool findValueDep(const llvm::Function &F);
};

#endif /* FUNCTIONSUMMARY_H */
