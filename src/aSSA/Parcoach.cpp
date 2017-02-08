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

  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
