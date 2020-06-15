#include "WordsFunction.h"
#include "WordsBasicBloc.h"
#include "WordsLoop.h"
#include "LoopSorter.h"
#include "../utils/Collectives.h"
#include "FunctionBFS.h"
#include "Concatenator.h"
#include "DebugSet.h"
#include "globals.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>
#include <queue>
#include <iostream>
#include <stack>
#include <algorithm>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>


using namespace llvm;
using namespace std;

WordsFunction::WordsFunction(llvm::Function *to_study) : function(to_study), words(), bb2words() {
}

WordsFunction::~WordsFunction() {
}

void WordsFunction::compute() {
    /* DFS starting from the entry bloc.
     * 1 - Create a map associating words per basic blocs.
     * 2 - For each basic block 'curr' in the CFG:
     * 2.1 - Build its set of word
     * 2.2 - Concatenate with successors (successors already treated because its a DFS) */

    treatLoops();
    this -> concatenate();
}

bool WordsFunction::isExitNode(BasicBlock *BB) {
    if(isa<ReturnInst>(BB -> getTerminator())) {
        return true;
    }
    return false;
}

void WordsFunction::concatenate() {
    BasicBlock *BB = &function -> getEntryBlock();
    for (auto elt : concatenate_rec(BB)) {
        words.insert(elt);
    }
}

set<string> WordsFunction::concatenate_rec(BasicBlock *curr) {
    set<string> res;
    /* Manage last bloc case */
    if (isExitNode(curr)) {
        compute_basicblock(curr);
        for(auto elt : bb2words[curr]) {
            res.insert(elt);
        }
        return res;
    }

    /* If the node is inside a loop, return with empty set */
    auto loop_it = bb2loop.find(curr);
    if (loop_it != bb2loop.end()) {
        return res;
    }

    succ_iterator SI = succ_begin(curr), SE = succ_end(curr);
    /* Compute basic bloc set */
    compute_basicblock(curr);
    for (;SI != SE;++SI) {
        auto BB = *SI;
        set<string> temp;
        auto loop_it = bb2loop.find(BB);

        /* Don't enter the block if it's a loop */
        if (loop_it != bb2loop.end()) {
            temp = header_concatenate_rec(BB);
        } else {
            temp = concatenate_rec(BB);
        }
        /* Concatenate the result */
        concatenante(&res, &bb2words[curr], &temp);
    }
    return res;
}

void WordsFunction::compute_basicblock(BasicBlock* BB) {
    WordsBasicBloc WBB(BB);
    WBB.compute();
    bb2words[BB] = WBB.get();
}

void WordsFunction::treatLoops() {
    auto curLoop = &pass->getAnalysis<LoopInfoWrapperPass>(*const_cast<Function *>(function))
                 .getLoopInfo();
    vector<Loop*> all_loop;
    for(auto loop : *curLoop) {
        all_loop.push_back(loop);
    }
    errs() << "In " << function->getName() << " there are " << all_loop.size() << " functions.\n";

    /* Treat loop from  the most nested to the least one */
    sort(all_loop.begin(), all_loop.end(), compare_loop);

    for(auto loop : all_loop) {
        /* compute set */
        WordsLoop WL(loop, loop2words);
        WL.compute();
        loop2words[loop] = WL.get();
        print_set(loop2words[loop]);

        /* associate header bloc to loop */
        bb2loop[loop -> getHeader()] = loop;
    }
}

set<string> WordsFunction::header_concatenate_rec(llvm::BasicBlock* BB) {
    set<string> res;
    Loop *loop_in = bb2loop[BB];

    succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
    bb2words[BB] = loop2words[loop_in];
    make_set_loop(bb2words[BB]);
    for(;SI != SE; ++SI) {
        auto curr = *SI;
        if (!loop_in->contains(curr)) {
            auto temp = concatenate_rec(curr);
            concatenante(&res, &bb2words[BB], &temp);
        }
    }
    return res;
}