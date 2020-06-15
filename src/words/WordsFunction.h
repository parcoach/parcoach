#ifndef WORDS_FUNCTION_H
#define WORDS_FUNCTION_H

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/Analysis/LoopInfo.h>
#include <set>
#include <string>
#include <map>
#include <stack>

class WordsFunction
{
private:
    /* Attributes */
    llvm::Function *function;
    std::set<std::string> words;
    std::map<llvm::BasicBlock*, std::set<std::string>> bb2words;
    std::map<llvm::Loop*, std::set<std::string>> loop2words;
    std::map<llvm::BasicBlock*, llvm::Loop*> bb2loop;

    /* Methods */
    bool isExitNode(llvm::BasicBlock *BB);
    void concatenate();
    std::set<std::string> concatenate_rec(llvm::BasicBlock*);
    std::set<std::string> header_concatenate_rec(llvm::BasicBlock*);
    void compute_basicblock(llvm::BasicBlock *BB);

    void treatLoops();
public:
    WordsFunction(llvm::Function *to_study);
    void compute();
    std::set<std::string> const& get() {return words;}
    virtual ~WordsFunction();
};


#endif//WORDS_FUNCTION_H