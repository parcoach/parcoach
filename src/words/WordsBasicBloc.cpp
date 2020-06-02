#include <set>
#include <string>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Pass.h>
#include "llvm/IR/IRBuilder.h"

#include "../utils/Collectives.h"
#include "WordsBasicBloc.h"
#include "WordsFunction.h"

using namespace llvm;
using namespace std;

WordsBasicBloc::WordsBasicBloc(BasicBlock *BB): to_study(BB), words()
{
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
        } else {
            WordsFunction words_func(callee);
            words_func.compute();
            set<string> func_words = words_func.get();
        }
    }
}