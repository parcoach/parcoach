#include "WordsModule.h"
#include "globals.h"
#include "WordsFunction.h"
#include <llvm/ADT/SCCIterator.h>
#include "llvm/IR/DebugInfo.h"
#include <vector>
#include <set>
#include <string>

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
            WordsFunction WF(F);
            WF.compute();
            fun2set[F] = WF.get();
            dbg(F);
        } // END FOR
        ++it;
    }
}

void WordsModule::dbg(llvm::Function* f) const {
    errs() << "{ ";
    int cpt = 0;
    for(string elt: fun2set[f]) {
        errs() << "\"" << elt << "\"";
        cpt ++;
        if (cpt < fun2set[f].size() - 1) {
            errs() << ", ";
        }
    }
    errs() << " }\n";
}