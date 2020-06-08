#include "WordsModule.h"
#include "globals.h"
#include <llvm/ADT/SCCIterator.h>
#include "llvm/IR/DebugInfo.h"
#include <vector>

using namespace llvm;
using namespace std;

WordsModule::WordsModule(PTACallGraph *PTACG) : PTACG(PTACG)
{
}

WordsModule::~WordsModule()
{
}

void WordsModule::run() {
    scc_iterator<PTACallGraph*> it = scc_begin(PTACG);

    while (!it.isAtEnd()) {
        const vector<PTACallGraphNode *> &nodeVec = *it;
        for (PTACallGraphNode *node : nodeVec) {
        Function *F = node->getFunction();
        if (!F || F->isDeclaration() || !PTACG -> isReachableFromEntry(F))
            continue;
        errs() << "Function: " << F->getName() << "\n";
        } // END FOR
        ++it;
    }
}