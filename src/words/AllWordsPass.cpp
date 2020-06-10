#include "AllWordsPass.h"
#include "WordsModule.h"
#include "../utils/Options.h"
#include "../aSSA/ExtInfo.h"
#include "../utils/Collectives.h"
#include "../aSSA/PTACallGraph.h"
#include "../aSSA/Utils.h"
#include "../aSSA/andersen/Andersen.h"
#include "globals.h"

#include <llvm/Transforms/Utils/UnifyFunctionExitNodes.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/Pass.h>

using namespace llvm;


AllWordsPass::AllWordsPass() : ModulePass(ID)
{
}

AllWordsPass::~AllWordsPass()
{
}

void AllWordsPass::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  /* All CFG will have an only one exit node */
  au.addRequiredID(UnifyFunctionExitNodes::ID);
}


bool AllWordsPass::doInitialization(llvm::Module& M) {
    getOptions();
    initCollectives();
    coll_repr.init();
    return true;
}

bool AllWordsPass::doFinalization(llvm::Module& M) {
    return true;
}

bool AllWordsPass::runOnModule(llvm::Module& M) {

    /* Be carefull, the entry may not be main */

    /* Compute for all function a map that associate 
     * to each function its BFS browsing it from the end. */

    /* 1) Create the PTACallGraph
     * 2) For each node in this graph  
     * 2.1) Compute the word set and store the object in a map */

    /* Create the call graph */
		errs() << "Compute Anderson and PTA\n";

		ExtInfo extInfo(M);
	  Andersen AA(M);
    PTACallGraph PTACG(M, &AA);

    WordsModule WM(&PTACG);
    WM.run();

    return true;
}

void AllWordsPass::compute_set() {

}

char AllWordsPass::ID = 0;

static RegisterPass<AllWordsPass> Z("allwords", "Module pass");
//static RegisterPass<AllWordsPass> pass_registrator("AllWords", "Module Pass");
