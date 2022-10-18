/*
This is PARCOACH
The project is licensed under the LGPL 2.1 license
*/

#include "Parcoach.h"
#include "Collectives.h"
#include "DepGraph.h"
#include "DepGraphDCF.h"
#include "ExtInfo.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
#include "ModRefAnalysis.h"
#include "Options.h"
#include "PTACallGraph.h"
#include "ParcoachAnalysisInter.h"
#include "Utils.h"
#include "andersen/Andersen.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Scalar/LowerAtomic.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include <llvm/Analysis/LoopInfo.h>

using namespace llvm;
using namespace std;

typedef llvm::UnifyFunctionExitNodesLegacyPass UnifyFunctionExitNodes;

ParcoachInstr::ParcoachInstr() : ModulePass(ID) {}

void ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  // FIXME: May raise assert in llvm with llvm-12
  // FIXME: this is actually a pass not an analysis; it should be fixable
  // by using our own pass manager and running it manually.
  // au.addRequiredID(UnifyFunctionExitNodes::ID);
  au.addRequired<DominanceFrontierWrapperPass>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTreeWrapperPass>();
  au.addRequired<CallGraphWrapperPass>();
  au.addRequired<LoopInfoWrapperPass>();
}

bool ParcoachInstr::doInitialization(Module &M) {
  PAInter = NULL;
  getOptions();
  initCollectives();

  tstart = gettime();

  return true;
}

bool ParcoachInstr::doFinalization(Module &M) {
  tend = gettime();

  unsigned intersectionSize;
  int WnbAdded = 0, CnbAdded = 0;
  int WnbRemoved = 0, CnbRemoved = 0;
  auto CyanErr = []() { return WithColor(errs(), raw_ostream::Colors::CYAN); };
  if (!optNoDataFlow) {
    CyanErr() << "==========================================\n";
    CyanErr() << "===  PARCOACH INTER WITH DEP ANALYSIS  ===\n";
    CyanErr() << "==========================================\n";
    errs() << "Module name: " << M.getModuleIdentifier() << "\n";
    errs() << PAInter->getNbCollectivesFound() << " collective(s) found\n";
    errs() << PAInter->getNbCollectivesCondCalled()
           << " collective(s) conditionally called\n";
    errs() << PAInter->getNbWarnings() << " warning(s) issued\n";
    errs() << PAInter->getNbConds() << " cond(s) \n";
    errs() << PAInter->getConditionSet().size() << " different cond(s)\n";
    errs() << PAInter->getNbCC() << " CC functions inserted \n";

    intersectionSize = getBBSetIntersectionSize(
        PAInter->getConditionSet(), PAInter->getConditionSetParcoachOnly());

    CnbAdded = PAInter->getConditionSet().size() - intersectionSize;
    CnbRemoved =
        PAInter->getConditionSetParcoachOnly().size() - intersectionSize;
    errs() << CnbAdded << " condition(s) added and " << CnbRemoved
           << " condition(s) removed with dep analysis.\n";

    intersectionSize = getInstSetIntersectionSize(
        PAInter->getWarningSet(), PAInter->getWarningSetParcoachOnly());

    WnbAdded = PAInter->getWarningSet().size() - intersectionSize;
    WnbRemoved = PAInter->getWarningSetParcoachOnly().size() - intersectionSize;
    errs() << WnbAdded << " warning(s) added and " << WnbRemoved
           << " warning(s) removed with dep analysis.\n";
  } else {

    CyanErr() << "================================================\n";
    CyanErr() << "===== PARCOACH INTER WITHOUT DEP ANALYSIS ======\n";
    CyanErr() << "================================================\n";
    errs() << PAInter->getNbCollectivesFound() << " collective(s) found\n";
    errs() << PAInter->getNbWarningsParcoachOnly() << " warning(s) issued\n";
    errs() << PAInter->getNbCondsParcoachOnly() << " cond(s) \n";
    errs() << PAInter->getConditionSetParcoachOnly().size()
           << " different cond(s)\n";
    errs() << PAInter->getNbCC() << " CC functions inserted \n";
  }

  /* if (!optNoDataFlow) {
     errs() << "app," << PAInter->getNbCollectivesFound() << ","
       << PAInter->getNbWarnings() << ","
       << PAInter->getConditionSet().size() << "," << WnbAdded << ","
       << WnbRemoved << "," << CnbAdded << "," << CnbRemoved << ","
       << PAInter->getNbWarningsParcoachOnly() << ","
       << PAInter->getConditionSetParcoachOnly().size() << "\n";
   }*/
  CyanErr() << "==========================================\n";

  if (optTimeStats) {
    errs() << "AA time : " << format("%.3f", (tend_aa - tstart_aa) * 1.0e3)
           << " ms\n";
    errs() << "Dep Analysis time : "
           << format("%.3f", (tend_flooding - tstart_pta) * 1.0e3) << " ms\n";
    errs() << "Parcoach time : "
           << format("%.3f", (tend_parcoach - tstart_parcoach) * 1.0e3)
           << " ms\n";
    errs() << "Total time : " << format("%.3f", (tend - tstart) * 1.0e3)
           << " ms\n\n";

    errs() << "detailed timers:\n";
    errs() << "PTA time : " << format("%.3f", (tend_pta - tstart_pta) * 1.0e3)
           << " ms\n";
    errs() << "Region creation time : "
           << format("%.3f", (tend_regcreation - tstart_regcreation) * 1.0e3)
           << " ms\n";
    errs() << "Modref time : "
           << format("%.3f", (tend_modref - tstart_modref) * 1.0e3) << " ms\n";
    errs() << "ASSA generation time : "
           << format("%.3f", (tend_assa - tstart_assa) * 1.0e3) << " ms\n";
    errs() << "Dep graph generation time : "
           << format("%.3f", (tend_depgraph - tstart_depgraph) * 1.0e3)
           << " ms\n";
    errs() << "Flooding time : "
           << format("%.3f", (tend_flooding - tstart_flooding) * 1.0e3)
           << " ms\n";
  }

  return true;
}

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
    ConstantExpr *op2AsCE = dyn_cast<ConstantExpr>(op2);
    assert(op2AsCE);
    Instruction *op2AsInst = op2AsCE->getAsInstruction();
    Function *outlinedFunc = dyn_cast<Function>(op2AsInst->getOperand(0));
    assert(outlinedFunc);
    op2AsInst->deleteValue();

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

bool ParcoachInstr::runOnModule(Module &M) {
  if (!optContextInsensitive && optDotTaintPaths) {
    errs() << "Error: you cannot use -dot-taint-paths option in context "
           << "sensitive mode.\n";
    exit(0);
  }

  if (optStats) {
    unsigned nbFunctions = 0;
    unsigned nbIndirectCalls = 0;
    unsigned nbDirectCalls = 0;
    for (const Function &F : M) {
      nbFunctions++;

      for (const BasicBlock &BB : F) {
        for (const Instruction &I : BB) {
          const CallInst *ci = dyn_cast<CallInst>(&I);
          if (!ci)
            continue;
          if (ci->getCalledFunction())
            nbDirectCalls++;
          else
            nbIndirectCalls++;
        }
      }
    }

    errs() << "nb functions : " << nbFunctions << "\n";
    errs() << "nb direct calls : " << nbDirectCalls << "\n";
    errs() << "nb indirect calls : " << nbIndirectCalls << "\n";

    exit(0);
  }

  ExtInfo extInfo(M);

  // Replace OpenMP Micro Function Calls and compute shared variable for
  // each function.
  map<const Function *, set<const Value *>> func2SharedOmpVar;
  if (optOmpTaint) {
    replaceOMPMicroFunctionCalls(M, func2SharedOmpVar);
  }

  if (optCudaTaint)
    cudaTransformation(M);

  // Run Andersen alias analysis.
  tstart_aa = gettime();
  Andersen AA(M);
  tend_aa = gettime();

  errs() << "* AA done\n";

  // Create PTA call graph
  tstart_pta = gettime();
  PTACallGraph PTACG(M, &AA);
  tend_pta = gettime();
  errs() << "* PTA Call graph creation done\n";

  // Create regions from allocation sites.
  tstart_regcreation = gettime();
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
  tend_regcreation = gettime();

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

  // Compute MOD/REF analysis
  tstart_modref = gettime();
  ModRefAnalysis MRA(PTACG, &AA, &extInfo);
  tend_modref = gettime();
  if (optDumpModRef)
    MRA.dump();

  errs() << "* Mod/ref done\n";

  // Compute all-inclusive SSA.
  tstart_assa = gettime();
  MemorySSA MSSA(&M, &AA, &PTACG, &MRA, &extInfo);

  unsigned nbFunctions = M.getFunctionList().size();
  unsigned counter = 0;
  for (Function &F : M) {
    if (!PTACG.isReachableFromEntry(&F)) {
      // errs() << F.getName() << " is not reachable from entry\n";

      continue;
    }

    if (counter % 100 == 0)
      errs() << "MSSA: visited " << counter << " functions over " << nbFunctions
             << " (" << (((float)counter) / nbFunctions * 100) << "%)\n";
    counter++;

    if (isIntrinsicDbgFunction(&F)) {
      continue;
    }

    if (F.isDeclaration())
      continue;

    // errs() << " + Fun: " << counter << " - " << F.getName() << "\n";
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    DominanceFrontier &DF =
        getAnalysis<DominanceFrontierWrapperPass>(F).getDominanceFrontier();
    PostDominatorTree &PDT =
        getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();
    // LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

    MSSA.buildSSA(&F, DT, DF, PDT);
    if (optDumpSSA)
      MSSA.dumpMSSA(&F);
    if (F.getName().equals(optDumpSSAFunc))
      MSSA.dumpMSSA(&F);
  }
  tend_assa = gettime();
  errs() << "* SSA done\n";

  // Compute dep graph.
  tstart_depgraph = gettime();
  DepGraph *DG = NULL;
  DG = new DepGraphDCF(&MSSA, &PTACG, this);
  DG->build();

  errs() << "* Dep graph done\n";

  tend_depgraph = gettime();

  tstart_flooding = gettime();

  // Compute tainted values
  if (optContextInsensitive)
    DG->computeTaintedValuesContextInsensitive();
  else
    DG->computeTaintedValuesContextSensitive();

  tend_flooding = gettime();
  errs() << "* value contamination  done\n";

  // Dot dep graph.
  if (optDotGraph) {
    DG->toDot("dg.dot");
  }

  errs() << "* Starting Parcoach analysis ...\n";

  tstart_parcoach = gettime();
  // Parcoach analysis

  PAInter = new ParcoachAnalysisInter(M, DG, PTACG, this, !optInstrumInter);
  PAInter->run();

  tend_parcoach = gettime();

  // Revert OMP transformation.
  if (optOmpTaint)
    revertOmpTransformation();

  return false;
}

char ParcoachInstr::ID = 0;

double ParcoachInstr::tstart = 0;
double ParcoachInstr::tend = 0;
double ParcoachInstr::tstart_aa = 0;
double ParcoachInstr::tend_aa = 0;
double ParcoachInstr::tstart_pta = 0;
double ParcoachInstr::tend_pta = 0;
double ParcoachInstr::tstart_regcreation = 0;
double ParcoachInstr::tend_regcreation = 0;
double ParcoachInstr::tstart_modref = 0;
double ParcoachInstr::tend_modref = 0;
double ParcoachInstr::tstart_assa = 0;
double ParcoachInstr::tend_assa = 0;
double ParcoachInstr::tstart_depgraph = 0;
double ParcoachInstr::tend_depgraph = 0;
double ParcoachInstr::tstart_flooding = 0;
double ParcoachInstr::tend_flooding = 0;
double ParcoachInstr::tstart_parcoach = 0;
double ParcoachInstr::tend_parcoach = 0;

static RegisterPass<ParcoachInstr> X("parcoach", "Parcoach pass", true, false);

static RegisterStandardPasses Y(
    //    PassManagerBuilder::EP_ModuleOptimizerEarly,
    PassManagerBuilder::EP_EarlyAsPossible,
    [](const PassManagerBuilder &Builder, legacy::PassManagerBase &PM) {
      PM.add(new ParcoachInstr());
    });
