#ifndef WORDS_FUNCTION_H
#define WORDS_FUNCTION_H

#include <llvm/IR/Function.h>
#include <set>
#include <string>
#include <map>

class WordsFunction
{
private:
    /* Attributes */
    llvm::Function *function;
    std::set<std::string> words;
    std::map<llvm::BasicBlock, std::set<std::string>> bb2words;

    /* Methods */
    bool isExitNode(llvm::BasicBlock *BB);
public:
    WordsFunction(llvm::Function *to_study);
    void compute();
    std::set<std::string> const& get() {return words;}
    virtual ~WordsFunction();
};


#endif//WORDS_FUNCTION_H