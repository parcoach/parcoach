#ifndef UTILS_H
#define UTILS_H

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Module.h"

#include <vector>

#define DEBUG_TYPE "hello"

const llvm::Argument *
getFunctionArgument(const llvm::Function *F,unsigned idx);

bool
blockDominatesEntry(llvm::BasicBlock *BB, llvm::PostDominatorTree &PDT,
		    llvm::DominatorTree *DT, llvm::BasicBlock *EntryBlock);

void
print_iPDF(std::vector<llvm::BasicBlock *> iPDF, llvm::BasicBlock *BB);

std::vector<llvm::BasicBlock * >
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
				llvm::BasicBlock *BB);


/*
 * INSTRUMENTATION
 */

// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void
instrumentCC(llvm::Module *M, llvm::Instruction *I, int OP_color,
	     std::string OP_name, int OP_line, llvm::StringRef WarningMsg,
	     llvm::StringRef File);

// get metadata
std::string
getWarning(llvm::Instruction &inst);

int
isCollectiveFunction(const llvm::Function &F);

int getInstructionLine(const llvm::Instruction &I);

std::string getFunctionFilename(const llvm::Function &F);

#endif /* UTILS_H */
