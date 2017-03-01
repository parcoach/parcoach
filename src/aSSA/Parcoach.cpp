#include "andersen/Andersen.h"
#include "DepGraph.h"
#include "ExtInfo.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
#include "ModRefAnalysis.h"
#include "Parcoach.h"
#include "PTACallGraph.h"
#include "Collectives.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include <llvm/Analysis/LoopInfo.h>

using namespace llvm;
using namespace std;

static cl::OptionCategory ParcoachCategory("Parcoach options");

static cl::opt<bool> optDumpSSA("dump-ssa",
				cl::desc("Dump the all-inclusive SSA"),
				cl::cat(ParcoachCategory));
static cl::opt<string> optDumpSSAFunc("dump-ssa-func",
				      cl::desc("Dump the all-inclusive SSA " \
					       "for a particular function."),
				      cl::cat(ParcoachCategory));
static cl::opt<bool> optDotGraph("dot-depgraph",
				 cl::desc("Dot the dependency graph to dg.dot"),
				 cl::cat(ParcoachCategory));
static cl::opt<bool> optDumpRegions("dump-regions",
				    cl::desc("Dump the regions found by the " \
					     "Andersen PTA"),
				    cl::cat(ParcoachCategory));
static cl::opt<bool> optDumpModRef("dump-modref",
				   cl::desc("Dump the mod/ref analysis"),
				   cl::cat(ParcoachCategory));
static cl::opt<bool> optTimeStats("timer",
				  cl::desc("Print timers"),
				  cl::cat(ParcoachCategory));
static cl::opt<bool> optDisablePhiElim("disable-phi-elim",
				       cl::desc("Disable Phi elimination pass"),
				       cl::cat(ParcoachCategory));
static cl::opt<bool> optDotTaintPaths("dot-taint-paths",
				      cl::desc("Dot taint path of each " \
					       "conditions of tainted "	\
					       "collectives."),
				      cl::cat(ParcoachCategory));
static cl::opt<bool> optStats("statistics", cl::desc("print statistics"),
			      cl::cat(ParcoachCategory));

cl::opt<bool> optNoRegName("no-reg-name",
				  cl::desc("Do not compute names of regions"),
				  cl::cat(ParcoachCategory));

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
ParcoachInstr::doFinalization(Module &M){
  errs() << "\n\033[0;36m==========================================\033[0;0m\n";
  errs() << "\033[0;36m==========  PARCOACH STATISTICS ==========\033[0;0m\n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";
  errs() << "Module name: " << M.getModuleIdentifier() << "\n";
  errs() << ParcoachInstr::nbCollectivesFound << " collective(s) found, and "
	 << ParcoachInstr::nbCollectivesTainted << " are tainted\n";
  errs() << ParcoachInstr::nbWarnings << " warning(s) issued\n";
  errs() << ParcoachInstr::nbConds << " cond(s) \n";
  errs() << "\n\033[0;36m==========================================\033[0;0m\n";
  errs() << "\033[0;36m============== PARCOACH ONLY =============\033[0;0m\n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";
  errs() << ParcoachInstr::nbWarningsParcoach << " warning(s) issued\n";
  errs() << ParcoachInstr::nbCondsParcoach << " cond(s) \n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";
  return true;
}


bool
ParcoachInstr::runOnModule(Module &M) {
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

  // Run Andersen alias analysis.
  double startPTA = gettime();
  Andersen AA(M);
  double endPTA = gettime();

  errs() << "AA done\n";

  // Create PTA call graph
  PTACallGraph PTACG(M, &AA);
  errs() << "PTA Call graph creation done\n";

  // Create regions from allocation sites.
  double startCreateReg = gettime();
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
  double endCreateReg = gettime();

  if (optDumpRegions)
    MemReg::dumpRegions();
  errs() << "Regions creation done\n";

  // Compute MOD/REF analysis
  double startModRef = gettime();
  ModRefAnalysis MRA(PTACG, &AA, &extInfo);
  double endModRef = gettime();
  if (optDumpModRef)
    MRA.dump();

  errs() << "Mod/ref done\n";

  // Compute all-inclusive SSA.
  MemorySSA MSSA(&M, &AA, &PTACG, &MRA, &extInfo);

  unsigned nbFunctions = M.getFunctionList().size();
  unsigned counter = 0;
  for (Function &F : M) {
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

  errs() << "SSA done\n";

  // Compute dep graph.
  DepGraph *DG = new DepGraph(&MSSA, &PTACG, this);
  counter = 0;
  for (Function &F : M) {
    if (counter % 100 == 0)
      errs() << "DepGraph: visited " << counter << " functions over " << nbFunctions
	     << " (" << (((float) counter)/nbFunctions*100) << "%)\n";

    counter++;

    if (isIntrinsicDbgFunction(&F))
      continue;

    DG->buildFunction(&F);
  }

  errs() << "Dep graph done\n";

  // Phi elimination pass.
  if (!optDisablePhiElim)
    DG->phiElimination();

  errs() << "phi elimination done\n";

  // Compute tainted values
  DG->computeTaintedValues();
  errs() << "value contamination  done\n";

  DG->computeTaintedCalls();
  errs() << "call contamination  done\n";

  // Dot dep graph.
  if (optDotGraph)
    DG->toDot("dg.dot");

  if (optTimeStats) {
    errs() << "Andersen PTA time : " << (endPTA - startPTA)*1.0e3 << " ms\n";
    errs() << "Create regions time : " << (endCreateReg - startCreateReg)*1.0e3
	   << " ms\n";
    errs() << "Mod/Ref analysis time : " << (endModRef - startModRef)*1.0e3
	   << " ms\n";
    MSSA.printTimers();
    DG->printTimers();
  }

  errs() << "Starting Parcoach analysis\n";

  // Parcoach analysis

  /* (1) BFS on each function of the Callgraph in reverse topological order
   *  -> set a function summary with sequence of collectives
   *  -> keep a set of collectives per BB and set the conditionals at NAVS if it can lead to a deadlock
   */
  errs() << " - BFS\n";
  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&PTACG);
  while(!cgSccIter.isAtEnd()) {
    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration())
	continue;
      //DBG: //errs() << "Function: " << F->getName() << "\n";
      BFS(F,&PTACG);
    }
    ++cgSccIter;
  }

  /* (2) Check collectives */
  errs() << " - CheckCollectives\n";
  cgSccIter = scc_begin(&PTACG);
  while(!cgSccIter.isAtEnd()) {
    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration())
	continue;
      //DBG: //errs() << "Function: " << F->getName() << "\n";
      checkCollectives(F,DG);
    }
    ++cgSccIter;
  }

  errs() << "Parcoach analysis done\n";

  return false;
}


// (2) Check MPI collectives
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
    if (!isCollective(f))
      continue;
    nbCollectivesFound++;

    // Is the collective tainted?
    if (DG->isTaintedCall(&*CI))
      nbCollectivesTainted++;

    bool isColWarning = false;
    bool isColWarningParcoach = false;

    // Get conditionals from the callsite
    set<const BasicBlock *> callIPDF;
    DG->getTaintedCallInterIPDF(CI, callIPDF);

    for (const BasicBlock *BB : callIPDF) {

      // Is this node detected as potentially dangerous by parcoach?
      string Seq = getBBcollSequence(*(BB->getTerminator()));
      if(Seq!="NAVS") continue;

      isColWarningParcoach = true;
      nbCondsParcoach++;

      // Is this condition tainted?
      const Value *cond = getBasicBlockCond(BB);
      if (!cond || !DG->isTaintedValue(cond)) continue;
      isColWarning = true;
      nbConds++;

      DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
      const Instruction *inst = BB->getTerminator();
      DebugLoc loc = inst->getDebugLoc();
      COND_lines.append(" ").append(to_string(loc.getLine()));
      COND_lines.append(" (").append(loc->getFilename()).append(")");

      if (optDotTaintPaths) {
	string dotfilename("taintedpath-");
	dotfilename.append(loc->getFilename()).append("-");
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
unsigned ParcoachInstr::nbCollectivesTainted = 0;
unsigned ParcoachInstr::nbWarnings = 0;
unsigned ParcoachInstr::nbConds = 0;
unsigned ParcoachInstr::nbWarningsParcoach = 0;
unsigned ParcoachInstr::nbCondsParcoach = 0;

static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
