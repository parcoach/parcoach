#include <set>
#include <string>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Pass.h>
#include "llvm/IR/IRBuilder.h"
#include <llvm/Support/raw_ostream.h>

#include "../utils/Collectives.h"
#include "globals.h"

#include "WordsBasicBloc.h"
#include "WordsFunction.h"
#include "Concatenator.h"
#include "DebugSet.h"

using namespace llvm;
using namespace std;

WordsBasicBloc::WordsBasicBloc(BasicBlock *BB): to_study(BB), words()
{
    words.insert("");
}

WordsBasicBloc::~WordsBasicBloc()
{
}

void WordsBasicBloc::compute() {
    /* For all instruction :
     * Check if it's a MPI Collective call. */

    for(auto inst_it = to_study -> rbegin(); inst_it != to_study -> rend(); ++inst_it) {
        /* Do something only with function call */
        const Instruction   *inst = &*inst_it;
        const CallInst *call_inst = dyn_cast<CallInst>(inst);
        if(!call_inst) continue;
        
        Function *callee = call_inst -> getCalledFunction();
        if(!callee) continue;

        if (isCollective(callee)) {
            concatenate(callee);
            errs() << callee -> getName() << "\n";
        } else if (fun2set.find(callee) != fun2set.end()) {
            set<string> func_words = fun2set[callee];
            concatenate_insitu(  &words, &func_words );
            print_set(func_words);
        }
    }
    //print_set(words);
}

void WordsBasicBloc::concatenate(Function* func) {
    set<string> temp;
    for(auto word : words) {
        temp . insert(word + " " + func -> getName().str());
    }
    words.swap(temp);
}