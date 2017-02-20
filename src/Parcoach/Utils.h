#ifndef UTILS_H
#define UTILS_H

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"

#include <vector>

#define DEBUG_TYPE "hello"



/*
 * POSTDOMINANCE
 */

std::vector<llvm::BasicBlock * >
postdominance_frontier(llvm::PostDominatorTree &PDT, llvm::BasicBlock *BB);


std::vector<llvm::BasicBlock * >
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
                                llvm::BasicBlock *BB);

/*
 * BFS
 */

void BFS(llvm::Function *F);

/*
 * Metadata
 */
std::string
getBBcollSequence(llvm::Instruction &inst);


#endif /* UTILS_H */
