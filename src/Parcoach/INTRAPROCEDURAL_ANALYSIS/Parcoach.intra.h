#ifndef PARCOACHINTRA_H
#define PARCOACHINTRA_H

#include <llvm/ADT/SetVector.h>
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

namespace {
  class ParcoachInstr : public llvm::FunctionPass {
  public:
    static char ID;
    ParcoachInstr();

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;

    virtual bool doInitialization(llvm::Module &M);

    virtual bool runOnFunction(llvm::Function &F);

  private:
    llvm::Module *MD;
    llvm::DominatorTree *DT;
    llvm::PostDominatorTree *PDT;
    llvm::LoopInfo *LI;
    llvm::AliasAnalysis *AA;

    // Set containing all memory locations where value value depends
    // on rank.
    std::set<const llvm::Value *> rankPtrSet;

    const char *ProgName;

    // For each calls to a collective, check whether the call site
    // depends on the rank and update STAT_collectives and STAT_warnings
    // accordingly.
    void checkCollectives(llvm::Function &F, llvm::StringRef filename,
			  int &STAT_collectives, int &STAT_warnings);

    // For each basic block in the iterated post dominance frontier,
    // check if the condition depends on the rank.
    bool is_IPDF_rank_dependent(std::vector<llvm::BasicBlock *> &IPDF);

    // Check if the condition of the basic block depends on the rank.
    bool is_BB_rank_dependent(const llvm::BasicBlock *BB);

    // Check if a value depends on the rank.
    // There is no interprocedural analysis:
    // - Arguments of the function are assumed to be independent of the rank.
    // - Calls to functions other than MPI_comm_rank() are not checked.
    bool is_value_rank_dependent(const llvm::Value *value);

    // Find memory locations where value depends on the rank.
    // There is no interprocedural analysis:
    // - Arguments of the function are assumed to be independent of the rank.
    // - Calls to functions other than MPI_comm_rank() are not checked.
    void findRankPointers(const llvm::Function &F);
    void findRankPointersRec(const llvm::Value *v,
			     llvm::SetVector<const llvm::User *> &seenUsers,
			     llvm::SetVector<const llvm::Value *> &toAdd);
    void printRankPointers() const;
  };
}

#endif /* PARCOACHINTRA_H */
