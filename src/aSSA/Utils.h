#ifndef UTILS_H
#define UTILS_H

#include "llvm/Analysis/PostDominators.h"

#include <vector>

const llvm::Argument *
getFunctionArgument(const llvm::Function *F,unsigned idx);

unsigned getNumArgs(const llvm::Function *F);

bool
blockDominatesEntry(llvm::BasicBlock *BB, llvm::PostDominatorTree &PDT,
		    llvm::DominatorTree *DT, llvm::BasicBlock *EntryBlock);

void
print_iPDF(std::vector<llvm::BasicBlock *> iPDF, llvm::BasicBlock *BB);

std::vector<llvm::BasicBlock * >
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
				llvm::BasicBlock *BB);

llvm::Function *createFunctionWithName(std::string name, llvm::Module *m);

std::string getValueLabel(const llvm::Value *v);

bool isMemoryAlloc(const llvm::Function *F);

#endif /* UTILS_H */
