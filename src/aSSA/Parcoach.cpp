#include "andersen/Andersen.h"
#include "DepGraph.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
#include "ModRefAnalysis.h"
#include "Parcoach.h"
#include "PTACallGraph.h"

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



std::vector<const char *> MPI_v_coll = {
  "MPI_Barrier", "MPI_Bcast", "MPI_Scatter", "MPI_Scatterv", "MPI_Gather",
  "MPI_Gatherv", "MPI_Allgather", "MPI_Allgatherv", "MPI_Alltoall",
  "MPI_Alltoallv", "MPI_Alltoallw", "MPI_Reduce", "MPI_Allreduce",
  "MPI_Reduce_scatter", "MPI_Scan", "MPI_Comm_split", "MPI_Comm_create",
  "MPI_Comm_dup", "MPI_Comm_dup_with_info", "MPI_Ibarrier", "MPI_Igather",
  "MPI_Igatherv", "MPI_Iscatter", "MPI_Iscatterv", "MPI_Iallgather",
  "MPI_Iallgatherv", "MPI_Ialltoall", "MPI_Ialltoallv", "MPI_Ialltoallw",
  "MPI_Ireduce", "MPI_Iallreduce", "MPI_Ireduce_scatter_block",
  "MPI_Ireduce_scatter", "MPI_Iscan", "MPI_Iexscan","MPI_Ibcast"
};



using namespace llvm;
using namespace std;

static cl::opt<bool> optDumpSSA("dump-ssa",
				cl::desc("Dump the all-inclusive SSA"));
static cl::opt<bool> optDotGraph("dot-depgraph",
				cl::desc("Dot the dependency graph to dg.dot"));
static cl::opt<bool> optDumpRegions("dump-regions",
				cl::desc("Dump the regions found by the "\
					 "Andersen PTA"));
static cl::opt<bool> optDumpModRef("dump-modref",
				cl::desc("Dump the mod/ref analysis"));
static cl::opt<bool> optTimeStats("timer",
				cl::desc("Print timers"));

ParcoachInstr::ParcoachInstr() : ModulePass(ID) {}

void
ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  au.addRequired<DominanceFrontierWrapperPass>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTreeWrapperPass>();
  au.addRequired<CallGraphWrapperPass>();
}



bool
ParcoachInstr::runOnModule(Module &M) {
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
  ModRefAnalysis MRA(PTACG, &AA);
  double endModRef = gettime();
  if (optDumpModRef)
    MRA.dump();

  errs() << "Mod/ref done\n";

  // Compute all-inclusive SSA.
  MemorySSA MSSA(&M, &AA, &PTACG, &MRA);

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

    if (F.isDeclaration()) {
      MSSA.buildExtSSA(&F);
      continue;
    }

    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    DominanceFrontier &DF =
      getAnalysis<DominanceFrontierWrapperPass>(F).getDominanceFrontier();
    PostDominatorTree &PDT =
      getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();

    MSSA.buildSSA(&F, DT, DF, PDT);
    if (optDumpSSA)
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

 
  // Parcoach analysis: use DG to find postdominance frontier and tainted nodes
  // Compute inter-procedural iPDF for all tainted collectives in the code
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  scc_iterator<CallGraph*> CGI = scc_begin(&CG);
  CallGraphSCC CurSCC(CG, &CGI);
 
  while (!CGI.isAtEnd()) { 
    const std::vector<CallGraphNode *> &NodeVec = *CGI;
    CurSCC.initialize(NodeVec.data(), NodeVec.data() + NodeVec.size()); 
    runOnSCC(CurSCC, DG);
    ++CGI;
  }


  return false;
}

bool ParcoachInstr::runOnSCC(CallGraphSCC &SCC, DepGraph *DG){
   for(CallGraphSCC::iterator CGit=SCC.begin(); CGit != SCC.end(); CGit++){
       CallGraphNode *CGN = *CGit;
       Function *F = CGN->getFunction();
       if (!F || F->isDeclaration()) continue; 

       for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
          Instruction *i=&*I;       
          // Debug info (line in the source code, file)
	  DebugLoc DLoc = i->getDebugLoc();
	  StringRef File=""; unsigned OP_line=0;
	  if(DLoc){
		  OP_line = DLoc.getLine();
		  File=DLoc->getFilename();
	  }
	  MDNode* mdNode;
	  // Warning info
	  string WarningMsg;
	  const char *ProgName="PARCOACH";
	  SMDiagnostic Diag;
	  std::string COND_lines;
	
	  // FOUND A CALL INSTRUCTION
	  CallInst *CI = dyn_cast<CallInst>(i);
	  if(!CI) continue;

	  Function *f = CI->getCalledFunction();
	  if(!f) continue;

	  string OP_name = f->getName().str();
	  StringRef funcName = f->getName();

	  // Is it a tainted collective call?
	  for (vector<const char *>::iterator vI = MPI_v_coll.begin(), E = MPI_v_coll.end(); vI != E; ++vI) {
		  if (funcName.equals(*vI) && DG->isTaintedCall(&*CI) == true){
			  //errs() << OP_name + " line " + to_string(OP_line) + " File " + File + " is tainted! it is  possibly not called by all processes\n";

	  		  // Get tainted conditionals from the callsite
		          set<const BasicBlock *> callIPDF;
			  DG->getTaintedCallInterIPDF(CI, callIPDF);

			  for (const BasicBlock *BB : callIPDF) {
			          const Value *cond = getBasicBlockCond(BB);
				  if (!cond || !DG->isTaintedValue(cond))
				    continue;
				  const Instruction *inst = BB->getTerminator();
				  DebugLoc loc = inst->getDebugLoc();
				  COND_lines.append(" ").append(to_string(loc.getLine()));
			  }

			  WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
			  mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
			  i->setMetadata("inst.warning",mdNode);
			  Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
			  Diag.print(ProgName, errs(), 1,1);
		  }
	  }
       }
   }
   return false;
}

/*
  unsigned nbCollectivesFound = 0;
  unsigned nbCollectivesTainted = 0;

  for (Function &F : M) {

    PostDominatorTree *PDT = F.isDeclaration() ? NULL :
      &getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();

  	for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
  		Instruction *i=&*I;
                // Debug info (line in the source code, file)
                DebugLoc DLoc = i->getDebugLoc();
                StringRef File=""; unsigned OP_line=0;
                if(DLoc){
                	OP_line = DLoc.getLine();
                        File=DLoc->getFilename();
                }
  		MDNode* mdNode;
                // Warning info
                string WarningMsg;
                const char *ProgName="PARCOACH";
                SMDiagnostic Diag;
                std::string COND_lines;


  		// FOUND A CALL INSTRUCTION
                CallInst *CI = dyn_cast<CallInst>(i);
                if(!CI) continue;

                Function *f = CI->getCalledFunction();
                if(!f) continue;

                string OP_name = f->getName().str();
  		StringRef funcName = f->getName();

  		//DG->isTaintedCalls(f);

  		// Is it a tainted collective call?
  		for (vector<const char *>::iterator vI = MPI_v_coll.begin(), E = MPI_v_coll.end(); vI != E; ++vI) {
  		  if (!funcName.equals(*vI))
		    continue;

		  nbCollectivesFound++;

  		  if (!DG->isTaintedCall(&*CI))
		    continue;

		  errs() << OP_name + " line " + to_string(OP_line) + " is tainted! it is  possibly not called by all processes\n";
		  nbCollectivesTainted++;

		  set<const Value *> conds;
		  DG->getTaintedCallConditions(CI, conds);

		  for (const Value *cond : conds) {
		    // FIXME: sometimes the condition of a branch
		    // instruction is a phi node and there is no valid DebugLog
		    // for phi nodes.
		    if (isa<PHINode>(cond))
		      continue;
		    if (!isa<Instruction>(cond))
		      continue;


		    if (!DG->isTaintedValue(cond))
		      continue;
		    const Instruction *inst = cast<Instruction>(cond);
		    DebugLoc loc = inst->getDebugLoc();
		    COND_lines.append(" ").append(to_string(loc.getLine()));
		  }

		  // FIXME: conditions responsibles for tainted call can be in another
		  // file.
		  WarningMsg = OP_name + " line " + to_string(OP_line) + " possibly not called by all processes because of conditional(s) line(s) " + COND_lines;
		  mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
		  i->setMetadata("inst.warning",mdNode);
		  Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
		  Diag.print(ProgName, errs(), 1,1);
		}
	}
  }


  errs() << nbCollectivesFound << " found, and " << nbCollectivesTainted << " are tainted\n";
*/



char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
