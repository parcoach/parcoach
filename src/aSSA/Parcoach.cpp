/*
This is PARCOACH
The project is licensed under the LGPL 2.1 license
*/

#include "Config.h"
#include "Instrumentation.h"
#include "OpenMPInstr.h"
#include "PTACallGraph.h"
#include "ShowPAInterResults.h"
#include "Utils.h"
#include "parcoach/CollListFunctionAnalysis.h"
#include "parcoach/Collectives.h"
#include "parcoach/DepGraphDCF.h"
#include "parcoach/ExtInfo.h"
#include "parcoach/MPICommAnalysis.h"
#include "parcoach/MemoryRegion.h"
#include "parcoach/MemorySSA.h"
#include "parcoach/ModRefAnalysis.h"
#include "parcoach/Options.h"
#include "parcoach/Passes.h"
#include "parcoach/RMAPasses.h"
#include "parcoach/StatisticsAnalysis.h"
#include "parcoach/andersen/Andersen.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
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

namespace parcoach {

namespace {
cl::opt<bool> optStats("statistics", cl::desc("print statistics"),
                       cl::cat(ParcoachCategory));
cl::opt<bool>
    optInstrumInter("instrum-inter",
                    cl::desc("Instrument code with inter-procedural parcoach"),
                    cl::cat(ParcoachCategory));

cl::opt<bool> optContextInsensitive("context-insensitive",
                                    cl::desc("Context insensitive version of "
                                             "flooding."),
                                    cl::cat(ParcoachCategory));

cl::opt<bool> optDotGraph("dot-depgraph",
                          cl::desc("Dot the dependency graph to dg.dot"),
                          cl::cat(ParcoachCategory));

cl::opt<bool> optDotTaintPaths("dot-taint-paths",
                               cl::desc("Dot taint path of each "
                                        "conditions of tainted "
                                        "collectives."),
                               cl::cat(ParcoachCategory));

struct ShowStats : public PassInfoMixin<ShowStats> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    AM.getResult<StatisticsAnalysis>(M).print(outs());
    return PreservedAnalyses::all();
  }
};
struct EmitDG : public PassInfoMixin<EmitDG> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    AM.getResult<DepGraphDCFAnalysis>(M)->toDot("dg.dot");
    return PreservedAnalyses::all();
  }
};
} // namespace

void RegisterPasses(ModulePassManager &MPM) {
  if (!optContextInsensitive && optDotTaintPaths) {
    errs() << "Error: you cannot use -dot-taint-paths option in context "
           << "sensitive mode.\n";
    exit(EXIT_FAILURE);
  }

  // Let's make sure we have a single exit node in all our functions.
  MPM.addPass(createModuleToFunctionPassAdaptor(UnifyFunctionExitNodesPass()));

  if (optStats) {
    MPM.addPass(ShowStats());
    return;
  }
#ifdef PARCOACH_ENABLE_OPENMP
  if (Options::get().isActivated(Paradigm::OMP)) {
    // Replace OpenMP Micro Function Calls and compute shared variable for
    // each function.
    MPM.addPass(PrepareOpenMPInstr());
  }
#endif
  if (optDotGraph) {
    // We want to print the dot *after* the preparation pass.
    MPM.addPass(EmitDG());
  }

#ifdef PARCOACH_ENABLE_RMA
  if (Options::get().isActivated(Paradigm::RMA)) {
    // Add the RMA passes and that's it.
    MPM.addPass(rma::RMAInstrumentationPass());
    return;
  }
#endif
  MPM.addPass(ShowPAInterResult());
  if (optInstrumInter) {
    MPM.addPass(ParcoachInstrumentationPass());
  }
}

void RegisterFunctionAnalyses(FunctionAnalysisManager &FAM) {
  AAManager AA;
  AA.registerFunctionAnalysis<BasicAA>();
  FAM.registerPass([&]() { return std::move(AA); });
#ifdef PARCOACH_ENABLE_RMA
  FAM.registerPass([&]() { return rma::LocalConcurrencyAnalysis(); });
  FAM.registerPass([&]() { return rma::RMAStatisticsAnalysis(); });
#endif
}

void RegisterModuleAnalyses(ModuleAnalysisManager &MAM) {
  MAM.registerPass([&]() { return AndersenAA(); });
  MAM.registerPass([&]() { return CollectiveAnalysis(optDotTaintPaths); });
  MAM.registerPass([&]() { return CollListFunctionAnalysis(); });
  MAM.registerPass(
      [&]() { return DepGraphDCFAnalysis(optContextInsensitive); });
  MAM.registerPass([&]() { return ExtInfoAnalysis(); });
  MAM.registerPass([&]() { return MemorySSAAnalysis(); });
  MAM.registerPass([&]() { return MemRegAnalysis(); });
  MAM.registerPass([&]() { return ModRefAnalysis(); });
  MAM.registerPass([&]() { return MPICommAnalysis(); });
  MAM.registerPass([&]() { return PTACallGraphAnalysis(); });
  MAM.registerPass([&]() { return StatisticsAnalysis(); });
}

void PrintVersion(raw_ostream &Out) {
  Out << "PARCOACH version " << PARCOACH_VERSION << "\n";
}

} // namespace parcoach

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
