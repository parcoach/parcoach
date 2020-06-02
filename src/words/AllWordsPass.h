#ifndef ALL_WORDS_PASS_H
#define ALL_WORDS_PASS_H

#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"

class AllWordsPass : public llvm::ModulePass
{
private:
    /* data */
public:
    static char ID;

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const;
    virtual bool doInitialization(llvm::Module &M);
    virtual bool doFinalization(llvm::Module &M);
    virtual bool runOnModule(llvm::Module &M);

    AllWordsPass(/* args */);
    virtual ~AllWordsPass();
};

#endif//ALL_WORDS_PASS_H