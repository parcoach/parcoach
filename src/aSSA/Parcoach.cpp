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

  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Module pass");
