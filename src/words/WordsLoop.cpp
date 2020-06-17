#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>

#include <set>
#include "WordsLoop.h"
#include "WordsBasicBloc.h"
#include "globals.h"
#include "Concatenator.h"
#include "DebugSet.h"

using namespace std;
using namespace llvm;

WordsLoop::WordsLoop(Loop *to_study, map<Loop*, set<string>> const& l2s) : words(), loop(to_study), bb2set(), loop2set(l2s), bb2loop() {
    for(auto sub_loop : loop -> getSubLoops()) {
        bb2loop[sub_loop -> getHeader()] = sub_loop;
    }
}

WordsLoop::~WordsLoop()
{
}

void WordsLoop::compute() {
    auto BB = loop -> getHeader();
    auto BB_set = compute_rec(BB);
    words.swap(BB_set);
}

std::set<std::string> WordsLoop::compute_rec(llvm::BasicBlock *BB) {
    set<string> res;
    /* Is this block the last bloc ? */
    if(isLatchBlock(BB)) {
        WordsBasicBloc WBB(BB);
        WBB.compute();
        bb2set[BB] = WBB.get();
        for(auto word : bb2set[BB]) {
            res.insert(word);
        }
        return res;
    }

    auto sub_loop_it = bb2loop.find(BB);
    /* Don't compute inner loop, they've been already treated */
    if(sub_loop_it != bb2loop.end()) {
        res = loop2set[sub_loop_it->second];
        make_set_loop(res);
        print_set(res);
        return res;
    }

    /* Own Basic Block Set */
    WordsBasicBloc WBB(BB);
    WBB.compute();
    bb2set[BB] = WBB.get();

    succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
    auto BB_set = bb2set[BB];
    /* Make sure the result is not null, in case of exit for instance */
    if(SI == SE) {
        for(auto word : BB_set) {
            res.insert(word);
        }
    }
    for(;SI != SE; ++SI) {
        auto curr = *SI;
        set<string> SI_set;

        if(loop->getHeader() == BB && !loop->contains(curr)) {continue;}

        SI_set = compute_rec(curr);

        concatenante(&res, &BB_set, &SI_set);
        print_set(res);
    }
    return res;
}

bool WordsLoop::isLatchBlock(BasicBlock* BB) const {
    return BB == loop->getLoopLatch();
}

void make_set_loop(std::set<std::string>& words) {
    set<string> res;
    for(string word : words) {
        if(word.find("exit") == string::npos) {
            res . insert ("(" + word.substr(0, word.find_last_of("-")-1) + ")* -> ");
        } else {
            for(auto w : words) {
                if(w.find("exit") == string::npos) {
                    res.insert( "(" + w.substr(0,w.find_last_of("-")-1) + ")* -> " + word);
                }   
            }
        }
    }
    words.swap(res);
}