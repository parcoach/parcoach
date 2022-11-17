#include "OpenMPInstr.h"

#include "Utils.h"
#include "parcoach/MemoryRegion.h"

#include "llvm/IR/InstIterator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "openmp-instr"

using namespace llvm;
namespace parcoach {

// ValueMap<Instruction *, Instruction *> PrepareOpenMPInstr::NewInst2oldInst;

PreservedAnalyses PrepareOpenMPInstr::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  LLVM_DEBUG(dbgs() << "Running PrepareOpenMPInstr\n");

  auto IsCandidateCall = [&](Instruction &I) {
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      auto *Callee = CI->getCalledFunction();
      if (Callee && Callee->getName() == "__kmpc_fork_call") {
        return true;
      }
    }
    return false;
  };

  for (Function &F : M) {
    // Get all calls to be replaced (only __kmpc_fork_call is handled for now).
    auto Instructions = make_filter_range(instructions(F), IsCandidateCall);
    // Use early inc range to be able to replace the instruction on the fly.
    for (auto &I : make_early_inc_range(Instructions)) {
      CallInst &CI = cast<CallInst>(I);
      // Operand 2 contains the outlined function
      Value *op2 = CI.getOperand(2);
      Function *outlinedFunc = dyn_cast<Function>(op2);
      assert(outlinedFunc && "can't cast kmp_fork_call arg");

      errs() << outlinedFunc->getName() << "\n";

      unsigned callNbOps = CI.getNumOperands();

      SmallVector<Value *, 8> NewArgs;

      // map 2 firsts operands of CI to null
      for (unsigned i = 0; i < 2; i++) {
        Type *ArgTy = getFunctionArgument(outlinedFunc, i)->getType();
        Value *val = Constant::getNullValue(ArgTy);
        NewArgs.push_back(val);
      }

      //  op 3 to nbops-1 are shared variables
      for (unsigned i = 3; i < callNbOps - 1; i++) {
        MemReg::func2SharedOmpVar[outlinedFunc].insert(CI.getOperand(i));
        NewArgs.push_back(CI.getOperand(i));
      }

      CallInst *NewCI = CallInst::Create(outlinedFunc, NewArgs);
      NewCI->setCallingConv(outlinedFunc->getCallingConv());
      // NewInst2oldInst[NewCI] = ci->clone();
      ReplaceInstWithInst(&CI, NewCI);
    }
  }
  return PreservedAnalyses::none();
}

// We use to do the following to revert OpenMP transformations, but I have yet
// to understand why we would bother reverting.
/*
void ParcoachInstr::revertOmpTransformation() {
  for (auto I : ompNewInst2oldInst) {
    Instruction *newInst = I.first;
    Instruction *oldInst = I.second;
    ReplaceInstWithInst(newInst, oldInst);
  }
}
*/
} // namespace parcoach
