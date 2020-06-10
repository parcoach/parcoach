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

WordsFunction::WordsFunction(llvm::Function *to_study) : function(to_study), words(), bb2words() {
}

WordsFunction::~WordsFunction() {
}

void WordsFunction::compute() {
    errs() << function -> getName() << "\n";
    /* DFS starting from the entry bloc.
     * 1 - Create a map associating words per basic blocs.
     * 2 - For each basic block 'curr' in the CFG:
     * 2.1 - Build its set of word
     * 2.2 - Concatenate with successors (successors already treated because its a DFS) */

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