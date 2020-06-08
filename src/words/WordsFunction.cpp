#include "WordsFunction.h"
#include "../utils/Collectives.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <vector>

using namespace llvm;
using namespace std;

WordsFunction::WordsFunction(llvm::Function *to_study) : function(to_study), words(), bb2words()
{
}

WordsFunction::~WordsFunction()
{
}

void WordsFunction::compute() {
    /* BFS starting from the end.
     * 1 - Need to get all basic bloc being an ending bloc.
     * 2 - Create a map associating words per basic blocs.
     * 3 - Start the BFS from ending points.
     * 4 - For each basic bloc BB starting from the end:
     * 4.1 - For each next basic blocs next_BB after BB in the CFG:
     * 4.1.1 - Concatenates words of next_BB with those of BB and store it in the set 'words'. */
    vector<BasicBlock*> unvisited;

    /**/
}

bool WordsFunction::isExitNode(BasicBlock *BB) {
    if(isa<ReturnInst>(BB -> getTerminator())) {
        return true;
    }
    for (auto &I : *BB) {
        Instruction *i = &I;
        CallInst *CI = dyn_cast<CallInst>(i);
        if (!CI)
        continue;
        Function *f = CI->getCalledFunction();
        if (!f)
        continue;
        // if(f->getName().equals("exit")||f->getName().equals("MPI_Abort") ||
        // f->getName().equals("abort")){
        if (f->getName().equals("MPI_Finalize") ||
            f->getName().equals("MPI_Abort") || f->getName().equals("abort")) {
        return true;
        }
    }
    return false;
}