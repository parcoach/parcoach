#include "andersen/Andersen.h"
#include "DepGraph.h"
#include "MemoryRegion.h"
#include "MemorySSA.h"
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

ParcoachInstr::ParcoachInstr() : ModulePass(ID) {}

void
ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  au.addRequired<DominanceFrontierWrapperPass>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTreeWrapperPass>();
}

bool
ParcoachInstr::runOnModule(Module &M) {
  // Run Andersen alias analysis.
  Andersen AA(M);

  // Create regions from allocation sites.
  vector<const Value *> regions;
  AA.getAllAllocationSites(regions);

  for (const Value *r : regions)
    MemReg::createRegion(r);
  MemReg::dumpRegions();

  MemorySSA MSSA(&M, &AA);


  // Compute all-inclusive SSA.
  for (Function &F : M) {
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
  }

  // Compute dep graph.
  DepGraph *DG = new DepGraph(&MSSA);

  for (Function &F : M) {
    PostDominatorTree *PDT = F.isDeclaration() ? NULL :
      &getAnalysis<PostDominatorTreeWrapperPass>(F).getPostDomTree();

    DG->buildFunction(&F, PDT);
  }

  // Dot dep graph.
  DG->toDot("dg.dot");


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
                	if (funcName.equals(*vI) && DG->isTainted(&*CI) == true){
				errs() << OP_name + " line " + to_string(OP_line) + " is tainted! it is  possibly not called by all processes\n";

				// TODO: get iPDF+ for a set of collectives and in inter-procedural
				// Use the DG to get the nodes?
				vector<BasicBlock * > iPDF = iterated_postdominance_frontier(*PDT, BB);
				vector<BasicBlock *>::iterator Bitr;
				if(iPDF.size()!=0){
                                	COND_lines="";
                                	for (Bitr = iPDF.begin(); Bitr != iPDF.end(); Bitr++) {
                                        	TerminatorInst* TI = (*Bitr)->getTerminator();
						DebugLoc BDLoc = TI->getDebugLoc();
                                        	COND_lines.append(" ").append(to_string(BDLoc.getLine()));
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
  }


  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
