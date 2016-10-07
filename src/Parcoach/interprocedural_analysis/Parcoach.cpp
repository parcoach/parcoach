// Parcoach.cpp - implements LLVM Compile Pass which checks errors caused by
// MPI collective operations
//
// This pass inserts functions

#include "Parcoach.h"

#include "Collectives.h"
#include "FunctionSummary.h"
#include "GlobalDepGraph.h"
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

  FuncSummaryMapTy *funcMap = new FuncSummaryMapTy();
  vector<FunctionSummary *> summaries;
  InterDepGraph *interDeps = new InterDepGraph();

  string dotFilename = "global.dot";
  for (auto I = M.begin(), E = M.end(); I != E; ++I) {
    Function *F = I;
    dotFilename = string(getFunctionFilename(*F) + ".dot");
    break;
  }

  // Create all summaries (this is necessary before running the first pass).
  for (auto I = M.begin(), E = M.end(); I != E; ++I) {
    Function *F = I;
    if (F->isDeclaration())
      continue;

    FunctionSummary *summary = new FunctionSummary(F, funcMap, interDeps, this);
    summaries.push_back(summary);
    (*funcMap)[F] = summary;
  }

  // Run the first pass for each summary.
  for (unsigned i=0; i<summaries.size(); i++)
    summaries[i]->firstPass();

  // Compute inverse order of scc.
  list<vector<CallGraphNode *> > inverseOrder;
  while (!I.isAtEnd()) {
    vector<CallGraphNode *> nodeVec = *I;
    inverseOrder.push_front(nodeVec);
    ++I;
  }

  // Iterate over the graph updating dependencies until reaching a fixed point.
  bool changed = true;
  while (changed) {
    changed = false;

    for (auto I = inverseOrder.begin(), E = inverseOrder.end(); I != E; ++I) {
      vector<CallGraphNode *> nodeVec = *I;
      vector<FunctionSummary *> sccSummaries;

      // Get a vector a summaries inside the scc.
      for (unsigned i=0; i<nodeVec.size(); ++i) {
	Function *F = nodeVec[i]->getFunction();
	if (!F || F->isDeclaration())
	  continue;
	FunctionSummary *FS = (*funcMap)[F];
	if (!FS) {
	  errs() << F->getName();
	  abort();
	}
	sccSummaries.push_back(FS);
      }

      // Update the scc dependencies until reaching a fixed point.
      bool sccChanged = true;
      while (sccChanged) {
	sccChanged = false;
	for (unsigned i = 0; i< sccSummaries.size(); ++i) {
	  sccChanged = sccSummaries[i]->updateDeps() || sccChanged;
	}
	changed = sccChanged || changed;
      }
    }
  }

  // Check collectives.
  for (unsigned i=0; i<summaries.size(); ++i)
    summaries[i]->checkCollectives();

  // Build a dependency graph.
  GlobalDepGraph *graph = new GlobalDepGraph(interDeps, &summaries);
  graph->toDot(dotFilename);
  errs() << "Dot graph created to file " << dotFilename << "\n";

  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Function pass");
