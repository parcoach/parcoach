#ifndef UTILS_H
#define UTILS_H

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/CallSite.h"

#include <vector>

bool isCallSite(const llvm::Instruction *inst);


std::string getValueLabel(const llvm::Value *v);
std::string getCallValueLabel(const llvm::Value *v);

std::vector<llvm::BasicBlock * >
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
				llvm::BasicBlock *BB);

void
print_iPDF(std::vector<llvm::BasicBlock *> iPDF, llvm::BasicBlock *BB);

std::set<const llvm::Value *>
computeIPDFPredicates(llvm::PostDominatorTree &PDT,
		      llvm::BasicBlock *BB);

const llvm::Argument *
getFunctionArgument(const llvm::Function *F,unsigned idx);

const llvm::Value *getReturnValue(const llvm::Function *F);

double gettime();

bool isIntrinsicDbgFunction(const llvm::Function *F);

bool isIntrinsicDbgInst(const llvm::Instruction *I);

#endif /* UTILS_H */
