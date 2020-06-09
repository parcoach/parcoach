#include "WordsFunction.h"
#include "WordsBasicBloc.h"
#include "../utils/Collectives.h"
#include "FunctionBFS.h"
#include "Concatenator.h"
#include "DebugSet.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>
#include <queue>
#include <iostream>
#include <stack>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/raw_os_ostream.h>


using namespace llvm;
using namespace std;

WordsFunction::WordsFunction(llvm::Function *to_study) : function(to_study), words(), bb2words()
{
    //words.insert("");
}

WordsFunction::~WordsFunction()
{
}

void WordsFunction::compute() {
    errs() << function -> getName() << "\n";
    /* BFS starting from the end.
     * 1 - Need to get all basic bloc being an ending bloc.
     * 2 - Create a map associating words per basic blocs.
     * 3 - Start the BFS from ending points.
     * 4 - For each basic bloc BB starting from the end:
     * 4.1 - For each next basic blocs next_BB after BB in the CFG:
     * 4.1.1 - Concatenates words of next_BB with those of BB and store it in the set 'words'. */
    //vector<BasicBlock*> unvisited;
    /*FunctionBFS bfs_manager(function);
    BasicBlock *end = bfs_manager.end();
    BasicBlock *curr;
    for(;*bfs_manager != end; ++bfs_manager) {
        curr  = *bfs_manager;
        //errs() << "Basic bloc name" << curr -> getName() << "\n";
        //if (bb2words.find(curr) != bb2words.end()) {
            WordsBasicBloc WBB(curr);
            WBB.compute();
            bb2words[curr] = WBB.get();
        //}
        //concatenate_insitu(&bb2words[curr], &words);
        //print_set(bb2words[curr]);
    }

    curr = *bfs_manager;
    //errs() << curr -> getName() << "\n";
    WordsBasicBloc WBB(curr);
    WBB.compute();
    bb2words[curr] = WBB.get();*/

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
    succ_iterator SI = succ_begin(curr), SE = succ_end(curr);
    for (;SI != SE;++SI) {
        auto BB = *SI;
        auto temp = concatenate_rec(BB);
        /* Compute basic bloc set */
        compute_basicblock(curr);
        /* Concatenate the result */
        concatenante(&res, &temp, &bb2words[curr]);
    }
    errs() << "Set before ret : ";print_set(res);
    return res;
}

void WordsFunction::compute_basicblock(BasicBlock* BB) {
    WordsBasicBloc WBB(BB);
    WBB.compute();
    bb2words[BB] = WBB.get();
}