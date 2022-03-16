#ifndef UTILS_H
#define UTILS_H

#include "PTACallGraph.h"
#include "llvm/Analysis/PostDominators.h"

#include "CallSite.h"

#include <vector>

bool isCallSite(const llvm::Instruction *inst);

std::string getValueLabel(const llvm::Value *v);
std::string getCallValueLabel(const llvm::Value *v);

std::vector<llvm::BasicBlock *>
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
                                llvm::BasicBlock *BB);

void print_iPDF(std::vector<llvm::BasicBlock *> iPDF, llvm::BasicBlock *BB);

std::set<const llvm::Value *>
computeIPDFPredicates(llvm::PostDominatorTree &PDT, llvm::BasicBlock *BB);

const llvm::Argument *getFunctionArgument(const llvm::Function *F,
                                          unsigned idx);

const llvm::Value *getReturnValue(const llvm::Function *F);

double gettime();

bool isIntrinsicDbgFunction(const llvm::Function *F);

bool isIntrinsicDbgInst(const llvm::Instruction *I);

bool functionDoesNotRet(const llvm::Function *F);

const llvm::Value *getBasicBlockCond(const llvm::BasicBlock *BB);

unsigned getBBSetIntersectionSize(const std::set<const llvm::BasicBlock *> S1,
                                  const std::set<const llvm::BasicBlock *> S2);

unsigned
getInstSetIntersectionSize(const std::set<const llvm::Instruction *> S1,
                           const std::set<const llvm::Instruction *> S2);

#endif /* UTILS_H */
