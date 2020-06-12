#include "AllWordsPass.h"
#include "WordsModule.h"
#include "../utils/Options.h"
#include "../aSSA/ExtInfo.h"
#include "../utils/Collectives.h"
#include "../aSSA/PTACallGraph.h"
#include "../aSSA/Utils.h"
#include "../aSSA/andersen/Andersen.h"
#include "globals.h"
#include "DebugSet.h"

#include <llvm/Transforms/Utils/UnifyFunctionExitNodes.h>
#include <llvm/ADT/SCCIterator.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Transforms/Utils/LoopSimplify.h>

#include <set>
#include <string>
#include <limits>
#include <vector>

using namespace llvm;
using namespace std;


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
  au.addRequiredID(LoopInfoWrapperPass::ID);
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

    pass = this;

    WordsModule WM(&PTACG);
    WM.run();

    /* Print some stats */
    print_stats(M);

    return true;
}

void AllWordsPass::compute_set() {

}

void AllWordsPass::print_stats(llvm::Module &M) const {
    set<string> allwords = fun2set[M.getFunction("main")];
    errs() << "All words are :\n";
    print_set(allwords);
    /* To improve */
    int max_length = 0;
    int min_length = numeric_limits<int>::max();
    set<string> longest_word, shortest_word;
    for (string word : allwords) {
      int length = 0;
      for (char c : word) {
        if(c == '-') length ++;
      }
      
      if (length > max_length) {
        max_length   = length;
        longest_word.clear();
        longest_word.insert(word);
      } else if (length == max_length) {
        longest_word.insert(word);
      }
      
      if (length == min_length) {
        shortest_word.insert(word);
      } else if (length < min_length) {
        min_length    = length;
        shortest_word.clear();
        shortest_word.insert(word);
      }
    }
    errs() << "The longest word has " << max_length << " collective(s) and are :\n";
    print_set(longest_word);
    errs() << "The shortest word has " << min_length << " collective(s) and are :\n";
    print_set(shortest_word);
}

char AllWordsPass::ID = 0;

static RegisterPass<AllWordsPass> Z("allwords", "Module pass");
//static RegisterPass<AllWordsPass> pass_registrator("AllWords", "Module Pass");
