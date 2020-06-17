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
        res.swap(bb2set[BB]);
        return res;
    }

    auto sub_loop_it = bb2loop.find(BB);
    /* Don't compute loop, they've been already treated */
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
    for(;SI != SE; ++SI) {
        auto curr = *SI;
        set<string> SI_set;

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
        res . insert ("(" + word.substr(0, word.find_last_of("-")-1) + ")* -> ");
    }
    words.swap(res);
}