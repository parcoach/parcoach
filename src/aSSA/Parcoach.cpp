/*
This is PARCOACH
The project is licensed under the LGPL 2.1 license
*/

#include "Parcoach.h"
#include "Collectives.h"
#include "Config.h"
#include "Instrumentation.h"
#include "Options.h"
#include "PTACallGraph.h"
#include "ParcoachAnalysisInter.h"
#include "ShowPAInterResults.h"
#include "Utils.h"
#include "parcoach/DepGraphDCF.h"
#include "parcoach/ExtInfo.h"
#include "parcoach/MemoryRegion.h"
#include "parcoach/MemorySSA.h"
#include "parcoach/ModRefAnalysis.h"
#include "parcoach/Passes.h"
#include "parcoach/StatisticsAnalysis.h"
#include "parcoach/andersen/Andersen.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

using namespace llvm;
using namespace std;

namespace parcoach {

ParcoachInstr::ParcoachInstr(ModuleAnalysisManager &AM) : MAM(AM) {}

void ParcoachInstr::replaceOMPMicroFunctionCalls(
    Module &M, map<const Function *, set<const Value *>> &func2SharedVarMap) {
  // Get all calls to be replaced (only __kmpc_fork_call is handled for now).
  std::vector<CallInst *> callToReplace;

  for (const Function &F : M) {
    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        const CallInst *ci = dyn_cast<CallInst>(&I);
        if (!ci)
          continue;

        const Function *called = ci->getCalledFunction();
        if (!called)
          continue;

        if (called->getName().equals("__kmpc_fork_call")) {
          callToReplace.push_back(const_cast<CallInst *>(ci));
        }
      }
    }
  }

  // Replace each call to __kmpc_fork_call with a call to the outlined function.
  for (unsigned n = 0; n < callToReplace.size(); n++) {
    CallInst *ci = callToReplace[n];

    // Operand 2 contains the outlined function
    Value *op2 = ci->getOperand(2);
    Function *outlinedFunc = dyn_cast<Function>(op2);
    assert(outlinedFunc && "can't cast kmp_fork_call arg");

    errs() << outlinedFunc->getName() << "\n";

    unsigned callNbOps = ci->getNumOperands();

    SmallVector<Value *, 8> NewArgs;

    // map 2 firsts operands of CI to null
    for (unsigned i = 0; i < 2; i++) {
      Type *ArgTy = getFunctionArgument(outlinedFunc, i)->getType();
      Value *val = Constant::getNullValue(ArgTy);
      NewArgs.push_back(val);
    }

    //  op 3 to nbops-1 are shared variables
    for (unsigned i = 3; i < callNbOps - 1; i++) {
      func2SharedVarMap[outlinedFunc].insert(ci->getOperand(i));
      NewArgs.push_back(ci->getOperand(i));
    }

    CallInst *NewCI =
        CallInst::Create(const_cast<Function *>(outlinedFunc), NewArgs);
    NewCI->setCallingConv(outlinedFunc->getCallingConv());
    ompNewInst2oldInst[NewCI] = ci->clone();
    ReplaceInstWithInst(const_cast<CallInst *>(ci), NewCI);
  }
}

void ParcoachInstr::revertOmpTransformation() {
  for (auto I : ompNewInst2oldInst) {
    Instruction *newInst = I.first;
    Instruction *oldInst = I.second;
    ReplaceInstWithInst(newInst, oldInst);
  }
}

// FIXME: this is currently deactivated as it needs to be adapted to support
// opaque pointers.
#if 0
void ParcoachInstr::cudaTransformation(Module &M) {
  // Compute list of kernels
  set<Function *> kernels;

  NamedMDNode *mdnode = M.getNamedMetadata("nvvm.annotations");
  if (!mdnode)
    return;

  for (unsigned i = 0; i < mdnode->getNumOperands(); i++) {
    MDNode *op = mdnode->getOperand(i);

    if (op->getNumOperands() < 2)
      continue;

    Metadata *md1 = op->getOperand(1);
    if (!md1)
      continue;

    MDString *mds = dyn_cast<MDString>(md1);
    if (!mds)
      continue;

    if (!mds->getString().equals("kernel"))
      continue;

    Metadata *md0 = op->getOperand(0);
    if (!md0)
      continue;

    ConstantAsMetadata *mdc = dyn_cast<ConstantAsMetadata>(md0);
    if (!mdc)
      continue;

    Constant *constVal = mdc->getValue();
    Function *func = dyn_cast<Function>(constVal);
    if (!func)
      continue;

    errs() << func->getName() << " is a kernel !\n";
    kernels.insert(func);
  }

  // For each kernel, create a fake function allocating memory for all
  // arguments and calling the kernel.
  for (Function *kernel : kernels) {
    vector<Type *> funcArgs;
    // funcArgs.push_back(Type::getVoidTy(M.getContext()));
    FunctionType *FT =
        FunctionType::get(Type::getVoidTy(M.getContext()), funcArgs, false);
    string funcName = "fake_call_" + kernel->getName().str();
    Function *fakeFunc =
        Function::Create(FT, Function::ExternalLinkage, funcName, &M);

    BasicBlock *entryBB = BasicBlock::Create(M.getContext(), "entry", fakeFunc);

    IRBuilder<> Builder(M.getContext());
    Builder.SetInsertPoint(entryBB);

    map<unsigned, Instruction *> arg2alloca;

    SmallVector<Value *, 8> callArgs;

    for (Argument &arg : kernel->args()) {
      Type *argTy = arg.getType();
      if (argTy->isPointerTy()) {
        PointerType *PTY = cast<PointerType>(arg.getType());
        Value *val = Builder.CreateAlloca(PTY->getElementType());
        callArgs.push_back(val);
      } else if (isa<IntegerType>(argTy)) {
        IntegerType *intTy = cast<IntegerType>(argTy);
        Value *v = ConstantInt::get(intTy, 42);
        callArgs.push_back(v);
      } else if (argTy->isFloatTy() || argTy->isDoubleTy()) {
        Value *v = ConstantFP::get(argTy, 42.0);
        callArgs.push_back(v);
      } else {
        errs() << "Error: unhandled argument type for CUDA kernel: " << *argTy
               << ", exiting..\n";
        exit(0);
      }
    }

    Builder.CreateCall(kernel, callArgs);
    Builder.CreateRetVoid();
  }
}
#endif

bool ParcoachInstr::runOnModule(Module &M) {
  // Replace OpenMP Micro Function Calls and compute shared variable for
  // each function.
  map<const Function *, set<const Value *>> func2SharedOmpVar;
  if (optOmpTaint) {
    replaceOMPMicroFunctionCalls(M, func2SharedOmpVar);
  }

#if 0
  if (optCudaTaint)
    cudaTransformation(M);
#endif

  // Run Andersen alias analysis.
  Andersen const &AA = MAM.getResult<AndersenAA>(M);

  errs() << "* AA done\n";

  // Create PTA call graph
  auto &PTACG = MAM.getResult<PTACallGraphAnalysis>(M);
  assert(PTACG && "expected a PTACallGraph");
  errs() << "* PTA Call graph creation done\n";

  // Create regions from allocation sites.
  vector<const Value *> regions;
  AA.getAllAllocationSites(regions);

  errs() << regions.size() << " regions\n";
  unsigned regCounter = 0;
  for (const Value *r : regions) {
    if (regCounter % 100 == 0) {
      errs() << regCounter << " regions created ("
             << ((float)regCounter) / regions.size() * 100 << "%)\n";
    }
    regCounter++;
    MemReg::createRegion(r);
  }

  if (optDumpRegions)
    MemReg::dumpRegions();
  errs() << "* Regions creation done\n";

  // Compute shared regions for each OMP function.
  if (optOmpTaint) {
    map<const Function *, set<MemReg *>> func2SharedOmpReg;
    for (auto I : func2SharedOmpVar) {
      const Function *F = I.first;

      for (const Value *v : I.second) {
        vector<const Value *> ptsSet;
        if (AA.getPointsToSet(v, ptsSet)) {
          vector<MemReg *> regs;
          MemReg::getValuesRegion(ptsSet, regs);
          MemReg::setOmpSharedRegions(F, regs);
        }
      }
    }
  }

  // Compute dep graph.
  auto &DG = MAM.getResult<DepGraphDCFAnalysis>(M);

  errs() << "* Dep graph done\n";

  // Dot dep graph.
  if (optDotGraph) {
    DG->toDot("dg.dot");
  }

  errs() << "* Starting Parcoach analysis ...\n";
  // Parcoach analysis

  ParcoachInstrumentationPass P;
  P.run(M, MAM);

  // Revert OMP transformation.
  if (optOmpTaint)
    revertOmpTransformation();
  return false;
}

PreservedAnalyses ParcoachPass::run(Module &M, ModuleAnalysisManager &AM) {
  ParcoachInstr P(AM);
  return P.runOnModule(M) ? PreservedAnalyses::none()
                          : PreservedAnalyses::all();
}

namespace {
struct ShowStats : public PassInfoMixin<ShowStats> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    AM.getResult<StatisticsAnalysis>(M).print(outs());
    return PreservedAnalyses::all();
  }
};
} // namespace

void RegisterPasses(ModulePassManager &MPM) {
  if (optVersion) {
    outs() << "PARCOACH version " << PARCOACH_VERSION << "\n";
    return;
  }

  if (!optContextInsensitive && optDotTaintPaths) {
    errs() << "Error: you cannot use -dot-taint-paths option in context "
           << "sensitive mode.\n";
    exit(EXIT_FAILURE);
  }

  initCollectives();
  if (optStats) {
    MPM.addPass(ShowStats());
    return;
  }
  MPM.addPass(createModuleToFunctionPassAdaptor(UnifyFunctionExitNodesPass()));
  MPM.addPass(ParcoachPass());
  MPM.addPass(ShowPAInterResult());
}

void RegisterAnalysis(ModuleAnalysisManager &MAM) {
  MAM.registerPass([&]() { return AndersenAA(); });
  MAM.registerPass([&]() { return DepGraphDCFAnalysis(); });
  MAM.registerPass([&]() { return ExtInfoAnalysis(); });
  MAM.registerPass([&]() { return InterproceduralAnalysis(); });
  MAM.registerPass([&]() { return MemorySSAAnalysis(); });
  MAM.registerPass([&]() { return ModRefAnalysis(); });
  MAM.registerPass([&]() { return PTACallGraphAnalysis(); });
  MAM.registerPass([&]() { return StatisticsAnalysis(); });
}

} // namespace parcoach
