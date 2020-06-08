#ifndef WORDS_BASIC_BLOC_H
#define WORDS_BASIC_BLOC_H

#include <llvm/IR/BasicBlock.h>
#include <set>
#include <string>

class WordsBasicBloc
{
private:
    llvm::BasicBlock *to_study;
    std::set<std::string> words;

    void concatenate(llvm::Function*);
public:
    WordsBasicBloc(llvm::BasicBlock *BB);

    void compute();

    std::set<std::string> const& get() {return words;}

    ~WordsBasicBloc();
};

#endif//WORDS_BASIC_BLOC_H