#ifndef WORDS_LOOP_H
#define WORDS_LOOP_H

#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <set>
#include <string>

class WordsLoop
{
private:
    llvm::Loop *loop;
    std::set<std::string> words;

    /* Information about basic block visited */
    // std::map<llvm::BasicBlock *, bool> visited;
    std::map<llvm::BasicBlock *, std::set<std::string>> bb2set;
    std::map<llvm::Loop*, std::set<std::string>> loop2set;
    std::map<llvm::BasicBlock *, llvm::Loop*> bb2loop;

    bool isLatchBlock(llvm::BasicBlock*) const;
    bool isExitBlock(llvm::BasicBlock*) const;
    bool isBreakBlock(llvm::BasicBlock*) const;
    std::set<std::string> compute_rec(llvm::BasicBlock *);
public:
    WordsLoop(llvm::Loop *to_study, std::map<llvm::Loop*, std::set<std::string>> const&);
    ~WordsLoop();

    void compute();
    std::set<std::string> get() {return words;}

};

void make_set_loop(std::set<std::string>&);

#endif//WORDS_LOOP_H