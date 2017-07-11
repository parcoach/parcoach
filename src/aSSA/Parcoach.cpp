#include "andersen/Andersen.h"
#include "DepGraph.h"
#include "ExtInfo.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
#include "ModRefAnalysis.h"
#include "Options.h"
#include "Parcoach.h"
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
}

bool
ParcoachInstr::doInitialization(Module &M) {
  getOptions();
  initCollectives();

  tstart = gettime();

  return true;
}


bool
ParcoachInstr::doFinalization(Module &M){
  tend = gettime();

  errs() << "\n\033[0;36m==========================================\033[0;0m\n";
  errs() << "\033[0;36m==========  PARCOACH STATISTICS ==========\033[0;0m\n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";
  errs() << "Module name: " << M.getModuleIdentifier() << "\n";
  errs() << ParcoachInstr::nbCollectivesFound << " collective(s) found\n";
  errs() << ParcoachInstr::nbWarnings << " warning(s) issued\n";
  errs() << ParcoachInstr::nbConds << " cond(s) \n";
  errs() << parcoachNodes.size() << " different cond(s)\n";

  errs() << "\n\033[0;36m==========================================\033[0;0m\n";
  errs() << "\033[0;36m============== PARCOACH ONLY =============\033[0;0m\n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";
  errs() << ParcoachInstr::nbWarningsParcoach << " warning(s) issued\n";
  errs() << ParcoachInstr::nbCondsParcoach << " cond(s) \n";
  errs() << ParcoachInstr::nbCC << " CC functions inserted \n";
  errs() << parcoachOnlyNodes.size() << " different cond(s)\n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";

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
ParcoachInstr::replaceOMPMicroFunctionCalls(Module &M) {
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

    //  op 3 to nbops-1
    for (unsigned i=3; i<callNbOps-1; i++) {
      NewArgs.push_back(ci->getOperand(i));
    }

    CallInst *NewCI = CallInst::Create(const_cast<Function *>(outlinedFunc), NewArgs);
    NewCI->setCallingConv(outlinedFunc->getCallingConv());
    ReplaceInstWithInst(const_cast<CallInst *>(ci), NewCI);
  }
}


void 
ParcoachInstr::instrumentFunction(Function *F) {
	Module* M = F->getParent();

  for(Function::iterator bb = F->begin(), e = F->end(); bb!=e; ++bb) {
		for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
			Instruction *Inst=&*i;
			string Warning = getWarning(*Inst);
			// Debug info (line in the source code, file)
			DebugLoc DLoc = i->getDebugLoc();
			string File="o"; int OP_line = -1;
			if(DLoc){
				OP_line = DLoc.getLine();
				File=DLoc->getFilename();
			}
			// call instruction
			if(CallInst *CI = dyn_cast<CallInst>(i)) {
				Function *callee = CI->getCalledFunction();
				if(callee==NULL) continue;
					string OP_name = callee->getName().str();
					int OP_color = getCollectiveColor(callee);

					// Before finalize or exit/abort
					if(callee->getName().equals("MPI_Finalize") || callee->getName().equals("MPI_Abort")){
						errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n";
						insertCC(M,Inst,v_coll.size()+1, OP_name, OP_line, Warning, File);
						nbCC++;
							continue;
					}
					// Before a collective
					if(OP_color>=0){
						errs() << "-> insert check before " << OP_name << " line " << OP_line << "\n";
						insertCC(M,Inst,OP_color, OP_name, OP_line, Warning, File);
						nbCC++;
					}
			}
		}
	}
}




bool
ParcoachInstr::runOnModule(Module &M) {
  if (optContextSensitive && optDotTaintPaths) {
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

  // Replace OpenMP Micro Function Calls
  replaceOMPMicroFunctionCalls(M);

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
      errs() << F.getName() << " is not reachable from entry\n";


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
  DepGraph *DG = new DepGraph(&MSSA, &PTACG, this);

  counter = 0;
  for (Function &F : M) {
    if (!PTACG.isReachableFromEntry(&F))
      continue;

    if (counter % 100 == 0)
      errs() << "DepGraph: visited " << counter << " functions over " << nbFunctions
	     << " (" << (((float) counter)/nbFunctions*100) << "%)\n";

    counter++;

    if (isIntrinsicDbgFunction(&F))
      continue;

    DG->buildFunction(&F);
  }

  errs() << "* Dep graph done\n";

  // Phi elimination pass.
  if (!optDisablePhiElim)
    DG->phiElimination();

  errs() << "* phi elimination done\n";
  tend_depgraph = gettime();

  tstart_flooding = gettime();

  // Compute tainted values
  if (optContextSensitive)
    DG->computeTaintedValuesContextSensitive();
  else
    DG->computeTaintedValuesContextInsensitive();
  tend_flooding = gettime();
  errs() << "* value contamination  done\n";

  // Dot dep graph.
  if (optDotGraph)
    DG->toDot("dg.dot");

  errs() << "* Starting Parcoach analysis ...\n";

  tstart_parcoach = gettime();
  // Parcoach analysis

  /* (1) BFS on each function of the Callgraph in reverse topological order
   *  -> set a function summary with sequence of collectives
   *  -> keep a set of collectives per BB and set the conditionals at NAVS if it can lead to a deadlock
   */
  errs() << " (1) BFS\n";
  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&PTACG);
  while(!cgSccIter.isAtEnd()) {
    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(F))
	continue;
      //DBG: //errs() << "Function: " << F->getName() << "\n";
      BFS(F,&PTACG);
    }
    ++cgSccIter;
  }

  /* (2) Check collectives */
  errs() << " (2) CheckCollectives\n";
  cgSccIter = scc_begin(&PTACG);
  while(!cgSccIter.isAtEnd()) {
    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(F))
	continue;
      //DBG: //errs() << "Function: " << F->getName() << "\n";
      checkCollectives(F,DG);
    }
    ++cgSccIter;
  }

	if(ParcoachInstr::nbWarnings !=0 && !optNoInstrum){
		errs() << "\033[0;35m=> Static instrumentation of the code ...\033[0;0m\n";
		for (Function &F : M) {
			instrumentFunction(&F);
		}
	}

  errs() << " ... Parcoach analysis done\n";

  tend_parcoach = gettime();

  return false;
}


// (2) Check collectives
void ParcoachInstr::checkCollectives(Function *F, DepGraph *DG) {
  StringRef FuncSummary;
  MDNode* mdNode;

  for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *i=&*I;
    // Debug info (line in the source code, file)
    DebugLoc DLoc = i->getDebugLoc();
    StringRef File=""; unsigned OP_line=0;
    if(DLoc){
      OP_line = DLoc.getLine();
      File=DLoc->getFilename();
    }
    // Warning info
    string WarningMsg;
    const char *ProgName="PARCOACH";
    SMDiagnostic Diag;
    std::string COND_lines;

    CallInst *CI = dyn_cast<CallInst>(i);
    if(!CI) continue;

    Function *f = CI->getCalledFunction();
    if(!f) continue;

    string OP_name = f->getName().str();

    // Is it a collective call?
    if (!isCollective(f)){
      continue;
		}

    nbCollectivesFound++;

    bool isColWarning = false;
    bool isColWarningParcoach = false;

    // Get conditionals from the callsite
    set<const BasicBlock *> callIPDF;
    DG->getCallInterIPDF(CI, callIPDF);

    for (const BasicBlock *BB : callIPDF) {

      // Is this node detected as potentially dangerous by parcoach?
      string Seq = getBBcollSequence(*(BB->getTerminator()));
      if(Seq!="NAVS") continue;

      isColWarningParcoach = true;
      nbCondsParcoach++;
      parcoachOnlyNodes.insert(BB);

      // Is this condition tainted?
      const Value *cond = getBasicBlockCond(BB);

      if (!cond || (!optNoDataFlow && DG->isTaintedValue(cond)) ) {
	const Instruction *instE = BB->getTerminator();
	DebugLoc locE = instE->getDebugLoc();
	//errs() << " -> Condition not tainted for a conditional with NAVS line " << locE.getLine() << " in " << locE->getFilename() << "\n";
	continue;
      }

      isColWarning = true;
      nbConds++;

      parcoachNodes.insert(BB);

      DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
      const Instruction *inst = BB->getTerminator();
      DebugLoc loc = inst->getDebugLoc();
      COND_lines.append(" ").append(to_string(loc.getLine()));
      COND_lines.append(" (").append(loc->getFilename()).append(")");

      if (optDotTaintPaths) {
       string dotfilename("taintedpath-");
       string cfilename = loc->getFilename();
       size_t lastpos_slash = cfilename.find_last_of('/');
       if (lastpos_slash != cfilename.npos)
        cfilename = cfilename.substr(lastpos_slash+1, cfilename.size());
       dotfilename.append(cfilename).append("-");
       dotfilename.append(to_string(loc.getLine())).append(".dot");
       DG->dotTaintPath(cond, dotfilename, i);
      }
    }

    // Is there at least one node from the IPDF+ detected as potentially
    // dangerous by parcoach
    if (isColWarningParcoach)
      nbWarningsParcoach++;

    // Is there at least one node from the IPDF+ tainted
    if (!isColWarning)
      continue;
    nbWarnings++;

    WarningMsg = OP_name + " line " + to_string(OP_line) +
      " possibly not called by all processes because of conditional(s) " \
      "line(s) " + COND_lines;
    mdNode = MDNode::get(i->getContext(),
			 MDString::get(i->getContext(), WarningMsg));
    i->setMetadata("inst.warning",mdNode);
    Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
    Diag.print(ProgName, errs(), 1,1);
  }
}




char ParcoachInstr::ID = 0;

unsigned ParcoachInstr::nbCollectivesFound = 0;
unsigned ParcoachInstr::nbWarnings = 0;
unsigned ParcoachInstr::nbConds = 0;
unsigned ParcoachInstr::nbWarningsParcoach = 0;
unsigned ParcoachInstr::nbCondsParcoach = 0;
unsigned ParcoachInstr::nbCC = 0;


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
