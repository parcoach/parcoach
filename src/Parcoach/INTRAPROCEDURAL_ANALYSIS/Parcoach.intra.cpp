// Parcoach.cpp - implements LLVM Compile Pass which checks errors caused by
// MPI collective operations
//
// This pass inserts functions

#include "Parcoach.intra.h"

#include "Collectives.h"
#include "FunctionSummary.h"
#include "GlobalGraph.h"
#include "Utils.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;
using namespace std;

ParcoachInstr::ParcoachInstr() : ModulePass(ID), ProgName("PARCOACH") {}

void
ParcoachInstr::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
  au.addRequired<CallGraphWrapperPass>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<LoopInfoWrapperPass>();
  au.addRequired<AliasAnalysis>();
  au.addRequired<TargetLibraryInfoWrapperPass>();
}

bool
ParcoachInstr::doInitialization(Module &M) {
  errs() << "\033[0;36m=============================================\033[0;0m\n"
	 << "\033[0;36m=============  STARTING PARCOACH  ===========\033[0;0m\n"
	 << "\033[0;36m============================================\033[0;0m\n";
  return true;
}

bool
ParcoachInstr::runOnModule(Module &M) {
  std::sort(MPI_v_coll.begin(), MPI_v_coll.end());
  std::sort(UPC_v_coll.begin(), UPC_v_coll.end());
  std::merge(MPI_v_coll.begin(),MPI_v_coll.end(),
	     UPC_v_coll.begin(), UPC_v_coll.end(),v_coll.begin());


  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  scc_iterator<CallGraph *> I = scc_begin(&CG);
  CallGraphSCC SCC(&I);

  // Compute inverse order of scc.
  vector<vector<CallGraphNode *> > inverseOrder;
  while (!I.isAtEnd()) {
    vector<CallGraphNode *> nodeVec = *I;
    inverseOrder.push_back(nodeVec);
    ++I;
  }

  vector<FunctionSummary *> summaries;

  while (!inverseOrder.empty()) {
    vector<CallGraphNode *> nodeVec = inverseOrder.back();
    inverseOrder.pop_back();

    vector<FunctionSummary *> sccSummaries;

    // Create a FunctionSummary for each function in the scc node.
    for (unsigned i=0; i<nodeVec.size(); ++i) {
      Function *F = nodeVec[i]->getFunction();
      if (!F || F->isDeclaration())
	continue;

      FunctionSummary *FS = new FunctionSummary(F, "Parcoach", depMap, this);
      sccSummaries.push_back(FS);
    }

    // Update interdependencies until reaching a fixed point.
    if (sccSummaries.size() > 1) {
      bool changed = true;
      while (changed) {
	changed = false;
	for (unsigned i=0; i<sccSummaries.size(); i++)
	  changed = sccSummaries[i]->updateInterDep() || changed;
      }
    }

    // Check collectives
    for (unsigned i=0; i<sccSummaries.size(); ++i) {
      sccSummaries[i]->checkCollectives();
    }

    summaries.insert(summaries.end(), sccSummaries.begin(), sccSummaries.end());
  }

  // Create a global graph with all summaries.
  GlobalGraph graph(&summaries, depMap);
  graph.toDot("global.dot");

  errs() << "Dot Graph written to file global.dot\n";

  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Function pass");
