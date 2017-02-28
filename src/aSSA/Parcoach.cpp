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
  errs() << ParcoachInstr::nbCollectivesFound << " collective(s) found, and " << ParcoachInstr::nbCollectivesTainted << " are tainted\n";
  errs() << ParcoachInstr::nbWarnings << " warning(s) issued\n";
  errs() << "\033[0;36m==========================================\033[0;0m\n";
  return true;
}


bool
ParcoachInstr::runOnModule(Module &M) {
  //errs() << ">>> Module name: " << M.getModuleIdentifier() << "\n";

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
void ParcoachInstr::checkCollectives(Function *F, DepGraph *DG){

				StringRef FuncSummary;
				MDNode* mdNode;

				for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
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
								StringRef funcName = f->getName();

								// Is it a tainted collective call?
								for (vector<const char *>::iterator vI = MPI_v_coll.begin(), E = MPI_v_coll.end(); vI != E; ++vI) {
												if (!funcName.equals(*vI)) continue;
												nbCollectivesFound++;
												if (!DG->isTaintedCall(&*CI)) continue;
												nbCollectivesTainted++;
												//errs() << "!!!!" << OP_name + " line " + to_string(OP_line) + " File " + File + " is tainted! it is  possibly not called by all processes\n";

												// Get tainted conditionals from the callsite
												set<const BasicBlock *> callIPDF;
												DG->getTaintedCallInterIPDF(CI, callIPDF);

												for (const BasicBlock *BB : callIPDF) {
																const Value *cond = getBasicBlockCond(BB);
																if (!cond || !DG->isTaintedValue(cond)) continue;
																string Seq = getBBcollSequence(*(BB->getTerminator()));
																DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
																//DBG: //errs() << "Seq = " << Seq << " - Line " << BDLoc->getLine() << "\n";
																if(Seq!="NAVS") continue;

																const Instruction *inst = BB->getTerminator();
																DebugLoc loc = inst->getDebugLoc();
																COND_lines.append(" ").append(to_string(loc.getLine())).append(" (").append(loc->getFilename()).append(")");
																if (optDotTaintPaths)
																  DG->dotTaintPath(cond, string("taintpath-").append(loc->getFilename()).append("-").append(to_string(loc.getLine())).append(".dot"), i);
												}
												if(COND_lines =="")  continue;
												ParcoachInstr::nbWarnings ++;
												WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
												mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
												i->setMetadata("inst.warning",mdNode);
												Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
												Diag.print(ProgName, errs(), 1,1);
								}
				}
}


char ParcoachInstr::ID = 0;
unsigned ParcoachInstr::nbCollectivesFound = 0;
unsigned ParcoachInstr::nbCollectivesTainted = 0;
unsigned ParcoachInstr::nbWarnings = 0;

static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
