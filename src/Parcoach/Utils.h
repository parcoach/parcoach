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

void print_iPDF(std::vector<llvm::BasicBlock * > iPDF, llvm::BasicBlock *BB);

/*
 * BFS
 */

void BFS(llvm::Function *F);

/*
 * Metadata
 */
std::string
getBBcollSequence(llvm::Instruction &inst);


/*
 * INSTRUMENTATION
 */


void
instrumentFunction(llvm::Function *F);

void
instrumentCC(llvm::Module *M, llvm::Instruction *I, int OP_color,
       std::string OP_name, int OP_line, llvm::StringRef WarningMsg,
       llvm::StringRef File);


#endif /* UTILS_H */
