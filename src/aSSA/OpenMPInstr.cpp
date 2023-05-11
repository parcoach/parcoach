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
  TimeTraceScope TTS("PrepareOpenMPInstr");
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
      Value *Op2 = CI.getOperand(2);
      Function *OutlinedFunc = dyn_cast<Function>(Op2);
      assert(OutlinedFunc && "can't cast kmp_fork_call arg");

      LLVM_DEBUG(dbgs() << OutlinedFunc->getName() << "\n");

      unsigned CallNbOps = CI.getNumOperands();

      SmallVector<Value *, 8> NewArgs;

      // map 2 firsts operands of CI to null
      for (unsigned I = 0; I < 2; I++) {
        Type *ArgTy = OutlinedFunc->getArg(I)->getType();
        Value *Val = Constant::getNullValue(ArgTy);
        NewArgs.push_back(Val);
      }

      //  op 3 to nbops-1 are shared variables
      for (unsigned I = 3; I < CallNbOps - 1; I++) {
        MemReg::func2SharedOmpVar[OutlinedFunc].insert(CI.getOperand(I));
        NewArgs.push_back(CI.getOperand(I));
      }

      CallInst *NewCI = CallInst::Create(OutlinedFunc, NewArgs);
      NewCI->setCallingConv(OutlinedFunc->getCallingConv());
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
