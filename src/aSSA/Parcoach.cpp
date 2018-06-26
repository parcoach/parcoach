#include "andersen/Andersen.h"
#include "DepGraph.h"
#include "DepGraphDCF.h"
#include "DepGraphUIDA.h"
#include "ExtInfo.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
#include "ModRefAnalysis.h"
#include "Options.h"
#include "Parcoach.h"
#include "ParcoachAnalysisInter.h"
#include "PTACallGraph.h"
#include "Collectives.h"
#include "Utils.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "llvm/Transforms/Scalar/LowerAtomic.h"
#include <llvm/Analysis/LoopInfo.h>
#include "llvm/Transforms/Utils/BasicBlockUtils.h"


using namespace llvm;
using namespace std;

ParcoachInstr::ParcoachInstr() : ModulePass(ID) {}

void
ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  au.addRequiredID(UnifyFunctionExitNodes::ID);
  au.addRequired<DominanceFrontierWrapperPass>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTreeWrapperPass>();
  au.addRequired<CallGraphWrapperPass>();
	au.addRequired<LoopInfoWrapperPass>();
}

bool
ParcoachInstr::doInitialization(Module &M) {
  PAInter = NULL;
  PAIntra = NULL;
  PAInterDCF = NULL;
  PAInterSVF = NULL;
  PAInterUIDA = NULL;

  getOptions();
  initCollectives();

  tstart = gettime();

  return true;
}

bool
ParcoachInstr::doFinalization(Module &M) {
  tend = gettime();

  if (!optCompareAll){
    if (PAInter) {
      if (!optNoDataFlow) {
	errs() << "\n\033[0;36m==========================================\033[0;0m\n";
	errs() << "\033[0;36m===  PARCOACH INTER WITH DEP ANALYSIS  ===\033[0;0m\n";
	errs() << "\033[0;36m==========================================\033[0;0m\n";
	errs() << "Module name: " << M.getModuleIdentifier() << "\n";
	errs() << PAInter->getNbCollectivesFound() << " collective(s) found\n";
	errs() << PAInter->getNbWarnings() << " warning(s) issued\n";
	errs() << PAInter->getNbConds() << " cond(s) \n";
	errs() << PAInter->getConditionSet().size() << " different cond(s)\n";

	unsigned intersectionSize;
	int nbAdded;
	int nbRemoved;
	intersectionSize
	  = getBBSetIntersectionSize(PAInter->getConditionSet(),
				     PAInter->getConditionSetParcoachOnly());

	nbAdded = PAInter->getConditionSet().size() - intersectionSize;
	nbRemoved = PAInter->getConditionSetParcoachOnly().size()
	  - intersectionSize;
	errs() << nbAdded << " condition(s) added and " << nbRemoved
	       << " condition(s) removed with dep analysis.\n";

	intersectionSize
	  = getInstSetIntersectionSize(PAInter->getWarningSet(),
				       PAInter->getWarningSetParcoachOnly());

	nbAdded = PAInter->getWarningSet().size() - intersectionSize;
	nbRemoved = PAInter->getWarningSetParcoachOnly().size()
	  - intersectionSize;
	errs() << nbAdded << " warning(s) added and " << nbRemoved
	       << " warning(s) removed with dep analysis.\n";
      }

      errs() << "\n\033[0;36m==========================================\033[0;0m\n";
      errs() << "\033[0;36m============== PARCOACH INTER ONLY =============\033[0;0m\n";
      errs() << "\033[0;36m==========================================\033[0;0m\n";
      errs() << PAInter->getNbCollectivesFound() << " collective(s) found\n";
      errs() << PAInter->getNbWarningsParcoachOnly() << " warning(s) issued\n";
      errs() << PAInter->getNbCondsParcoachOnly() << " cond(s) \n";
      errs() << PAInter->getConditionSetParcoachOnly().size() << " different cond(s)\n";
      errs() << PAInter->getNbCC() << " CC functions inserted \n";
      errs() << PAInter->getConditionSetParcoachOnly().size() << " different cond(s)\n";

     /* errs() << "\n\033[0;36m==========================================\033[0;0m\n";
      errs() << "\033[0;36m========= PARCOACH INTER SUMMARY-BASED =====\033[0;0m\n";
      errs() << "\033[0;36m==========================================\033[0;0m\n";
      */
      // TODO

      if (PAIntra) {
	unsigned intersectionSize;
	int nbAdded;
	int nbRemoved;
	intersectionSize
	  = getBBSetIntersectionSize(PAInter->getConditionSetParcoachOnly(),
				     PAIntra->getConditionSetParcoachOnly());

	nbAdded = PAInter->getConditionSetParcoachOnly().size() -
	  intersectionSize;
	nbRemoved = PAIntra->getConditionSetParcoachOnly().size()
	  - intersectionSize;
	errs() << nbAdded << " condition(s) added and " << nbRemoved
	       << " condition(s) removed compared to intra analysis.\n";

	intersectionSize
	  = getInstSetIntersectionSize(PAInter->getWarningSetParcoachOnly(),
				       PAIntra->getWarningSetParcoachOnly());

	nbAdded = PAInter->getWarningSetParcoachOnly().size() - intersectionSize;
	nbRemoved = PAIntra->getWarningSetParcoachOnly().size()
	  - intersectionSize;
	errs() << nbAdded << " warning(s) added and " << nbRemoved
	       << " warning(s) removed compared to intra analysis.\n";
      }

      //errs() << "\033[0;36m==========================================\033[0;0m\n";
    }

    if (PAIntra) {
      errs() << "\n\033[0;36m==========================================\033[0;0m\n";
      errs() << "\033[0;36m============== PARCOACH INTRA ONLY =============\033[0;0m\n";
      errs() << "\033[0;36m==========================================\033[0;0m\n";
      errs() << PAIntra->getNbWarningsParcoachOnly() << " warning(s) issued\n";
      errs() << PAIntra->getNbCondsParcoachOnly() << " cond(s) \n";
      errs() << PAIntra->getNbCC() << " CC functions inserted \n";
      errs() << PAIntra->getConditionSetParcoachOnly().size() << " different cond(s)\n";
      errs() << "\033[0;36m==========================================\033[0;0m\n";
    }
  } else {
    int nbcols,
      intraonlywarnings, intraonlyconds,
      interonlywarnings, interonlyconds,
      interwarningsadded,interwarningsremoved,
      intercondsadded,intercondsremoved,
      dcfwarnings, dcfconds,
      svfwarnings, svfconds,
      uidawarnings, uidaconds,
      svfwarningsadded, svfwarningsremoved,
      svfcondsadded, svfcondsremoved,
      uidawarningsadded, uidawarningsremoved,
      uidacondsadded, uidacondsremoved;
    unsigned intersectionSize;

    nbcols = PAIntra->getNbCollectivesFound();

    intraonlywarnings = PAIntra->getWarningSetParcoachOnly().size();
    intraonlyconds = PAIntra->getConditionSetParcoachOnly().size();

    interonlywarnings = PAInterDCF->getWarningSetParcoachOnly().size();
    interonlyconds = PAInterDCF->getConditionSetParcoachOnly().size();

    intersectionSize
      = getInstSetIntersectionSize(PAInterDCF->getWarningSetParcoachOnly(),
				   PAIntra->getWarningSetParcoachOnly());
    interwarningsadded = PAInterDCF->getWarningSetParcoachOnly().size() - intersectionSize;
    interwarningsremoved = PAIntra->getWarningSetParcoachOnly().size() - intersectionSize;

    intersectionSize
      = getBBSetIntersectionSize(PAInterDCF->getConditionSetParcoachOnly(),
				 PAIntra->getConditionSetParcoachOnly());
    intercondsadded = PAInterDCF->getConditionSetParcoachOnly().size() - intersectionSize;
    intercondsremoved = PAIntra->getConditionSetParcoachOnly().size() - intersectionSize;

    dcfwarnings = PAInterDCF->getWarningSet().size();
    dcfconds = PAInterDCF->getConditionSet().size();
    svfwarnings = PAInterSVF->getWarningSet().size();
    svfconds = PAInterSVF->getConditionSet().size();
    uidawarnings = PAInterUIDA->getWarningSet().size();
    uidaconds = PAInterUIDA->getConditionSet().size();

    intersectionSize
      = getInstSetIntersectionSize(PAInterDCF->getWarningSet(), PAInterSVF->getWarningSet());
    svfwarningsadded = PAInterDCF->getWarningSet().size() - intersectionSize;
    svfwarningsremoved = PAInterSVF->getWarningSet().size() - intersectionSize;
    intersectionSize
      = getBBSetIntersectionSize(PAInterDCF->getConditionSet(), PAInterSVF->getConditionSet());
    svfcondsadded = PAInterDCF->getConditionSet().size() - intersectionSize;
    svfcondsremoved = PAInterSVF->getConditionSet().size() - intersectionSize;

    intersectionSize
      = getInstSetIntersectionSize(PAInterDCF->getWarningSet(), PAInterUIDA->getWarningSet());
    uidawarningsadded = PAInterDCF->getWarningSet().size() - intersectionSize;
    uidawarningsremoved = PAInterUIDA->getWarningSet().size() - intersectionSize;
    intersectionSize
      = getBBSetIntersectionSize(PAInterDCF->getConditionSet(), PAInterUIDA->getConditionSet());
    uidacondsadded = PAInterDCF->getConditionSet().size() - intersectionSize;
    uidacondsremoved = PAInterUIDA->getConditionSet().size() - intersectionSize;

    errs() << "app," << nbcols << ","
	   << intraonlywarnings << "," <<  intraonlyconds << ","
	   << interonlywarnings << "," << interonlyconds << ","
	   << interwarningsadded << "," << interwarningsremoved << ","
	   << intercondsadded << "," << intercondsremoved << ","
	   << dcfwarnings << "," << dcfconds << ","
	   << svfwarnings << "," << svfconds << ","
	   << uidawarnings << "," << uidaconds << ","
	   << svfwarningsadded << "," << svfwarningsremoved << ","
	   << svfcondsadded << "," << svfcondsremoved << ","
	   << uidawarningsadded << "," << uidawarningsremoved << ","
	   << uidacondsadded << "," << uidacondsremoved << "\n";
  }

  if (optTimeStats) {
    errs()  << "AA time : "
    	    << format("%.3f", (tend_aa - tstart_aa)*1.0e3) << " ms\n";
    errs() << "Dep Analysis time : "
    	   << format("%.3f", (tend_flooding - tstart_pta)*1.0e3)
    	   << " ms\n";
    errs() << "Parcoach time : "
    	   << format("%.3f", (tend_parcoach - tstart_parcoach)*1.0e3)
    	   << " ms\n";
    errs() << "Total time : "
    	   << format("%.3f", (tend - tstart)*1.0e3) << " ms\n\n";

    errs() << "detailed timers:\n";
    errs() << "PTA time : "
    	   << format("%.3f", (tend_pta - tstart_pta)*1.0e3) << " ms\n";
    errs() << "Region creation time : "
    	   << format("%.3f", (tend_regcreation - tstart_regcreation)*1.0e3)
    	   << " ms\n";
    errs() << "Modref time : "
    	   << format("%.3f", (tend_modref - tstart_modref)*1.0e3)
    	   << " ms\n";
    errs() << "ASSA generation time : "
    	   << format("%.3f", (tend_assa- tstart_assa)*1.0e3)
    	   << " ms\n";
    errs() << "Dep graph generation time : "
    	   << format("%.3f", (tend_depgraph - tstart_depgraph)*1.0e3)
    	   << " ms\n";
    errs() << "Flooding time : "
    	   << format("%.3f", (tend_flooding - tstart_flooding)*1.0e3)
    	   << " ms\n";
  }

  return true;
}

void
ParcoachInstr::replaceOMPMicroFunctionCalls(Module &M,
					    map<const Function *,
					    set<const Value *> > &
					    func2SharedVarMap) {
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
  for (unsigned n=0; n<callToReplace.size(); n++) {
    CallInst *ci = callToReplace[n];

    // Operand 2 contains the outlined function
    Value *op2 = ci->getOperand(2);
    ConstantExpr *op2AsCE = dyn_cast<ConstantExpr>(op2);
    assert(op2AsCE);
    Instruction *op2AsInst = op2AsCE->getAsInstruction();
    Function *outlinedFunc = dyn_cast<Function>(op2AsInst->getOperand(0));
    assert(outlinedFunc);
    delete op2AsInst;

    errs() << outlinedFunc->getName() << "\n";

    unsigned callNbOps = ci->getNumOperands();

    SmallVector<Value *, 8> NewArgs;

    // map 2 firsts operands of CI to null
    for (unsigned i=0; i<2; i++) {
      Type *ArgTy = getFunctionArgument(outlinedFunc, i)->getType();
      Value *val = Constant::getNullValue(ArgTy);
      NewArgs.push_back(val);
    }

    //  op 3 to nbops-1 are shared variables
    for (unsigned i=3; i<callNbOps-1; i++) {
      func2SharedVarMap[outlinedFunc].insert(ci->getOperand(i));
      NewArgs.push_back(ci->getOperand(i));
    }

    CallInst *NewCI = CallInst::Create(const_cast<Function *>(outlinedFunc), NewArgs);
    NewCI->setCallingConv(outlinedFunc->getCallingConv());
    ompNewInst2oldInst[NewCI] = ci->clone();
    ReplaceInstWithInst(const_cast<CallInst *>(ci), NewCI);
  }
}

void
ParcoachInstr::revertOmpTransformation() {
  for (auto I : ompNewInst2oldInst) {
    Instruction *newInst = I.first;
    Instruction *oldInst = I.second;
    ReplaceInstWithInst(newInst, oldInst);
  }
}

void
ParcoachInstr::cudaTransformation(Module &M) {
  // Compute list of kernels
  set<Function *> kernels;

  NamedMDNode *mdnode = M.getNamedMetadata("nvvm.annotations");
  if (!mdnode)
    return;

  for (unsigned i=0; i<mdnode->getNumOperands(); i++) {
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

    Metadata *md0  = op->getOperand(0);
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
    //funcArgs.push_back(Type::getVoidTy(M.getContext()));
    FunctionType *FT = FunctionType::get(Type::getVoidTy(M.getContext()),
					 funcArgs, false);
    string funcName = "fake_call_" + kernel->getName().str();
    Function *fakeFunc = Function::Create(FT,
					  Function::ExternalLinkage,
					  funcName,
					  &M);

    Function *funcFromModule = M.getFunction(funcName);
    assert(funcFromModule);

    BasicBlock *entryBB = BasicBlock::Create(M.getContext(), "entry", funcFromModule);

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
	errs() << "Error: unhandled argument type for CUDA kernel: "
	       << *argTy << ", exiting..\n";
	exit(0);
      }
    }

    Builder.CreateCall(kernel, callArgs);

    ReturnInst *RI = Builder.CreateRetVoid();
  }
}

bool
ParcoachInstr::runOnModule(Module &M) {
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
	for( const Instruction &I : BB) {
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
  map<const Function *, set<const Value *> > func2SharedOmpVar;
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
    if (regCounter%100 == 0) {
      errs() << regCounter << " regions created ("
	     << ((float) regCounter) / regions.size() * 100<< "%)\n";
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
    map<const Function *, set<MemReg *> > func2SharedOmpReg;
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
	     << " (" << (((float) counter)/nbFunctions*100) << "%)\n";
    counter++;

    if (isIntrinsicDbgFunction(&F)) {
      continue;
    }

    if (F.isDeclaration())
      continue;

    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    DominanceFrontier &DF =
      getAnalysis<DominanceFrontierWrapperPass>(F).getDominanceFrontier();
    PostDominatorTree &PDT =
      getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();
		//LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

	// LOOPS

 /* for(Loop *L: LI){
		BasicBlock *B = L->getHeader();
		pred_iterator PI=pred_begin(B), E=pred_end(B);
    for(; PI!=E; ++PI){
			BasicBlock *PH = *PI;
			if(L->contains(PH))
				bbPreheaderMap[PH]=true;
		 		//errs() << F.getName() << "BB " << PH->getName() << " is preheader in a loop\n";
		}
  }*/


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
  DepGraph *DGDCF = NULL;
  DepGraph *DGSVF = NULL;
  DepGraph *DGUIDA = NULL;
  if (!optCompareAll) {
    if (optDGUIDA)
      DG = new DepGraphUIDA(&PTACG, this);
    else if (optDGSVF)
      DG = new DepGraphDCF(&MSSA, &PTACG, this, true, true, true);
    else
      DG = new DepGraphDCF(&MSSA, &PTACG, this);
    DG->build();
  } else {
    DGDCF = new DepGraphDCF(&MSSA, &PTACG, this);
    DGDCF->build();
    DGSVF = new DepGraphDCF(&MSSA, &PTACG, this, true, true, true);
    DGSVF->build();
    DGUIDA = new DepGraphUIDA(&PTACG, this);
    DGUIDA->build();
  }

  errs() << "* Dep graph done\n";

  tend_depgraph = gettime();

  tstart_flooding = gettime();

  // Compute tainted values
  if (!optCompareAll) {
    if (optContextInsensitive)
      DG->computeTaintedValuesContextInsensitive();
    else
      DG->computeTaintedValuesContextSensitive();
  } else {
    if (optContextInsensitive) {
      DGDCF->computeTaintedValuesContextInsensitive();
      DGSVF->computeTaintedValuesContextInsensitive();
      DGUIDA->computeTaintedValuesContextInsensitive();
    } else {
      DGDCF->computeTaintedValuesContextSensitive();
      DGSVF->computeTaintedValuesContextSensitive();
      DGUIDA->computeTaintedValuesContextSensitive();
    }
  }
  tend_flooding = gettime();
  errs() << "* value contamination  done\n";

  // Dot dep graph.
  if (optDotGraph) {
    DG->toDot("dg.dot");
  }

  errs() << "* Starting Parcoach analysis ...\n";

  tstart_parcoach = gettime();
  // Parcoach analysis

  if (!optCompareAll) {
    if (!optIntraOnly) {
      PAInter = new ParcoachAnalysisInter(M, DG, PTACG,this, !optInstrumInter);
      PAInter->run();
    }

    if (!optInterOnly) {
      PAIntra = new ParcoachAnalysisIntra(M, NULL, this, !optInstrumIntra);
      PAIntra->run();
    }
  } else {
      errs() << "\033[0;36m= PARCOACH INTRA =\033[0;0m\n";
      PAIntra = new ParcoachAnalysisIntra(M, NULL, this, !optInstrumIntra);
      PAIntra->run();
      errs() << "\033[0;36m= PARCOACH INTER =\033[0;0m\n";
      PAInterDCF = new ParcoachAnalysisInter(M, DGDCF, PTACG,this, !optInstrumInter);
      PAInterDCF->run();
      errs() << "\033[0;36m= PARCOACH + SVF =\033[0;0m\n";
      PAInterSVF = new ParcoachAnalysisInter(M, DGSVF, PTACG,this, !optInstrumInter);
      PAInterSVF->run();
      errs() << "\033[0;36m= PARCOACH + UIDA =\033[0;0m\n";
      PAInterUIDA = new ParcoachAnalysisInter(M, DGUIDA, PTACG,this, !optInstrumInter);
      PAInterUIDA->run();
  }

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

static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
