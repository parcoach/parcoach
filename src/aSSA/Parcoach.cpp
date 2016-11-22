#include "ASSA.h"
#include "DepGraphBuilder.h"
#include "Parcoach.h"
#include "Utils.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/DependenceAnalysis.h"
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
  au.addRequired<PostDominatorTree>();
}

bool
ParcoachInstr::runOnModule(Module &M) {
  /* Compute Dep Graph */
  for (auto I = M.begin(), E = M.end(); I != E; ++I) {
    const Function &F = *I;
    if (F.isDeclaration())
      continue;

    runOnFunction(*I);
  }

  /* Compute tainted values */
  dg.computeTaintedValues();

  /* Dot graph */
  dg.toDot("dg.dot");

  return false;
}

bool
ParcoachInstr::runOnFunction(Function &F) {
  PostDominatorTree &PDT = getAnalysis<PostDominatorTree>(F);

  dg.addFunction(&F);

  /* Compute aSSA. */
  ASSA aSSA;
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *inst = &*I;

    PHINode *phi = dyn_cast<PHINode>(inst);
    if (!phi)
      continue;

    aSSA[phi] = new std::set<const Value *>();

    // For each argument of the PHINode
    for (unsigned i=0; i<phi->getNumIncomingValues(); ++i) {
      // Get IPDF
      vector<BasicBlock *> IPDF =
	iterated_postdominance_frontier(PDT,
					phi->getIncomingBlock(i));

      for (unsigned n = 0; n < IPDF.size(); ++n) {
	// Push conditions of each BB in the IPDF
	const TerminatorInst *ti = IPDF[n]->getTerminator();
	assert(ti);
	const BranchInst *bi = dyn_cast<BranchInst>(ti);
	assert(bi);

	if (bi->isUnconditional())
	  continue;

	const Value *cond = bi->getCondition();

	aSSA[phi]->insert(cond);
      }
    }
  }

  /* Compute Dep Graph */
  DepGraphBuilder DGB(PDT, aSSA, dg);
  DGB.visit(F);

  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Function pass");
