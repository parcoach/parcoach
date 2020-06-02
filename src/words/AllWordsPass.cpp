#include "AllWordsPass.h"
#include "../aSSA/ExtInfo.h"
#include "../utils/Collectives.h"
#include "../aSSA/PTACallGraph.h"
#include "../aSSA/Utils.h"
#include "../aSSA/andersen/Andersen.h"

#include "llvm/Pass.h"

using namespace llvm;


AllWordsPass::AllWordsPass() : ModulePass(ID)
{
}

AllWordsPass::~AllWordsPass()
{
}

void AllWordsPass::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
}


bool AllWordsPass::doInitialization(llvm::Module& M) {
    initCollectives();
    return true;
}

bool AllWordsPass::doFinalization(llvm::Module& M) {
    return true;
}

bool AllWordsPass::runOnModule(llvm::Module& M) {

    /* Create the call graph */
		errs() << "La passe fonctionne!!\n";

		ExtInfo extInfo(M);
	  Andersen AA(M);
//    PTACallGraph PTACG(M, &AA);


    return true;
}

char AllWordsPass::ID = 0;

static RegisterPass<AllWordsPass> Z("allwords", "Module pass");
//static RegisterPass<AllWordsPass> pass_registrator("AllWords", "Module Pass");
