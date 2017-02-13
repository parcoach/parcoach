#include "andersen/Andersen.h"
#include "DepGraph.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
#include "ModRefAnalysis.h"
#include "Parcoach.h"

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

  // Create regions from allocation sites.
  double startCreateReg = gettime();
  vector<const Value *> regions;
  AA.getAllAllocationSites(regions);

  for (const Value *r : regions)
    MemReg::createRegion(r);
  double endCreateReg = gettime();

  if (optDumpRegions)
    MemReg::dumpRegions();

  // Compute MOD/REF analysis
  double startModRef = gettime();
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  ModRefAnalysis MRA(CG, &AA);
  double endModRef = gettime();
  if (optDumpModRef)
    MRA.dump();

  // Compute all-inclusive SSA.
  MemorySSA MSSA(&M, &AA, &MRA);
  for (Function &F : M) {
    if (isIntrinsicDbgFunction(&F))
      continue;

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

  // Compute dep graph.
  DepGraph *DG = new DepGraph(&MSSA);

  for (Function &F : M) {
    if (isIntrinsicDbgFunction(&F))
      continue;

    PostDominatorTree *PDT = F.isDeclaration() ? NULL :
      &getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();

    DG->buildFunction(&F, PDT);
  }

  // Dot dep graph.
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

 
  // Compute inter-procedural iPDF for all collectives in the code
/* for (Function &F : M) {
    	PostDominatorTree *PDT = F.isDeclaration() ? NULL :
      		&getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();
	// Compute iPDF inside the function
	for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		Instruction *i=&*I;
                CallInst *CI = dyn_cast<CallInst>(i);
		if(!CI) continue;
		
		//if(CI->getCalledFunction()->getName().find("MPI_") ==0){
		errs() << "get pdf of " << CI->getCalledFunction()->getName() << "\n";
		vector<BasicBlock * > iPDF = iterated_postdominance_frontier(*PDT, i->getParent());
			
		//}

	}

	// If a function call is found, update the iPDF  !! if multiple calls
 }*/ 



  // Parcoach analysis: use DG to find postdominance frontier and tainted nodes

  for (Function &F : M) {

    PostDominatorTree *PDT = F.isDeclaration() ? NULL :
      &getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();
  
	for(inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I){
		Instruction *i=&*I;
		BasicBlock *BB = i->getParent();
                // Debug info (line in the source code, file)
                DebugLoc DLoc = i->getDebugLoc();
                StringRef File=""; unsigned OP_line=0;
                if(DLoc){
                	OP_line = DLoc.getLine();
                        File=DLoc->getFilename();
                }
		MDNode* mdNode;
                // Warning info
                StringRef WarningMsg;
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
                	if (funcName.equals(*vI)){ 
			 if(DG->isTaintedCall(&*CI) == true || DG->isTaintedFunc(f) ==true ){
				// Issue a warning
				WarningMsg = OP_name + " line " + to_string(OP_line) + " is tainted\n";
                                mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
                                i->setMetadata("inst.warning",mdNode);
                                Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
                                Diag.print(ProgName, errs(), 1,1);
				
				// TODO: get iPDF+ for a set of collectives and in inter-procedural and only tainted nodes in iPDF
				// Use the DG to get the tainted cond nodes?
				vector<BasicBlock * > iPDF = iterated_postdominance_frontier(*PDT, BB);
				vector<BasicBlock *>::iterator Bitr;
				if(iPDF.size()!=0){
                                	COND_lines="";
                                	for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
                                        	TerminatorInst* TI = (*Bitr)->getTerminator();
						DebugLoc BDLoc = TI->getDebugLoc();
                                        	COND_lines.append(" ").append(to_string(BDLoc.getLine()));
					}
					WarningMsg = OP_name + " line " + to_string(OP_line) + " is tainted BECAUSE OF conditional(s) line(s) " + COND_lines;
                                        mdNode = MDNode::get(i->getContext(),MDString::get(i->getContext(),WarningMsg));
                                        i->setMetadata("inst.warning",mdNode);
                                        Diag=SMDiagnostic(File,SourceMgr::DK_Warning,WarningMsg);
                                        //Diag.print(ProgName, errs(), 1,1);
				}
			   }
			}
		}
		// Get tainted conditionals
		// for the function F for now
		// We would like the conditionals in DG from the collective call
		//DG->getCondLines(&F);
	}
  }



  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
