#ifndef UTILS_H
#define UTILS_H

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"

#include <vector>

#define DEBUG_TYPE "hello"


/*
 * POSTDOMINANCE
 */

bool
blockDominatesEntry(llvm::BasicBlock *BB, llvm::PostDominatorTree &PDT,
		    llvm::DominatorTree *DT, llvm::BasicBlock *EntryBlock);

std::vector<llvm::BasicBlock * > 
postdominance_frontier(llvm::PostDominatorTree &PDT, llvm::BasicBlock *BB);


std::vector<llvm::BasicBlock * >
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
				llvm::BasicBlock *BB);

void
print_iPDF(std::vector<llvm::BasicBlock *> iPDF, llvm::BasicBlock *BB);



/*
 * INSTRUMENTATION
 */

void
instrumentCC(llvm::Module *M, llvm::Instruction *I, int OP_color,
	     std::string OP_name, int OP_line, llvm::StringRef WarningMsg,
	     llvm::StringRef File);


/*
 * METADATA INFO
 */

std::string
getWarning(llvm::Instruction &inst);

std::string
getFuncSummary(llvm::Function &F);

std::string
getBBcollSequence(llvm::Instruction &inst);

/*
 * OTHER
 */

int
isCollectiveFunction(const llvm::Function &F);

int getInstructionLine(const llvm::Instruction &I);

std::string getFunctionFilename(const llvm::Function &F);

#endif /* UTILS_H */
