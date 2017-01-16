#include "ASSA.h"
#include "DepGraphBuilder.h"
#include "MemSSA.h"
#include "Parcoach.h"
#include "PointGraphBuilder.h"
#include "Region.h"
#include "RegionBuilder.h"
#include "Utils.h"

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
  au.addRequired<DominanceFrontier>();
  au.addRequired<DominatorTreeWrapperPass>();
  au.addRequired<PostDominatorTree>();
}

void
ParcoachInstr::computeMemorySSA(Module &M) {
  DepGraph pg;

  // Compute point-to graph.
  PointGraphBuilder PGB(pg);
  PGB.visit(M);
  pg.toDot("pg.dot");

  // Compute regions
  map<const Function *, vector<Region *> > regions;
  RegionBuilder RB(regions);
  RB.visit(M);

  for (auto I : regions) {
    errs() << "Function " << I.first->getName() << "\n";
    for (Region *r : I.second)
      r->print();
  }

  // Compute MemorySSA
  for (auto I  = M.begin(), E = M.end(); I != E; ++I) {
    llvm::Function *F = &*I;
    if (F->isDeclaration())
      continue;

    PostDominatorTree &PDT = getAnalysis<PostDominatorTree>(*F);
    DominanceFrontier &DF = getAnalysis<DominanceFrontier>(*F);
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(*F).getDomTree();

    MemSSA mSSA(F, &pg, PDT, DF, DT, regions[F]);

    // Compute Augmented Memory SSA
    mSSA.computeASSA();

    // Dump MemorySSA
    mSSA.dump();
  }
}

bool
ParcoachInstr::runOnModule(Module &M) {
  module = &M;

  computeMemorySSA(M);

  /* Create IPDF Node for each Function */
  for (auto I = M.begin(), E = M.end(); I != E; ++I) {
    const Function &F = *I;
    if (F.isDeclaration())
      continue;

    Function *IPDF_func = createFunctionWithName(string(F.getName()) + "_IPDF",
					       module);
    dg.addIPDFFuncNode(&F, IPDF_func);

    for (unsigned i=0; i<getNumArgs(&F); ++i)
      dg.addEdge(IPDF_func, getFunctionArgument(&F, i));
  }

  /* Compute Dep Graph */
  for (auto I = M.begin(), E = M.end(); I != E; ++I) {
    const Function &F = *I;
    if (F.isDeclaration())
      continue;

    runOnFunction(*I);
  }

  /* Compute tainted values */
  dg.computeTaintedValues(this);

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

	if (isa<BranchInst>(ti)) {
	  const BranchInst *bi = cast<BranchInst>(ti);
	  assert(bi);

	  if (bi->isUnconditional())
	    continue;

	  const Value *cond = bi->getCondition();

	  aSSA[phi]->insert(cond);
	} else if(isa<SwitchInst>(ti)) {
	  const SwitchInst *si = cast<SwitchInst>(ti);
	  assert(si);
	  const Value *cond = si->getCondition();
	  aSSA[phi]->insert(cond);
	}
      }
    }

    errs() << getValueLabel(phi) << " predicates = { ";
    std::set<const Value *> *predicates = aSSA[phi];
    for (auto I = predicates->begin(), E = predicates->end(); I != E; ++I)
      errs() << getValueLabel(*I) << " , ";
    errs() << " }\n";

  }

  /* Compute Dep Graph */
  DepGraphBuilder DGB(PDT, aSSA, dg);
  DGB.visit(F);

  return false;
}

char ParcoachInstr::ID = 0;
static RegisterPass<ParcoachInstr> Z("parcoach", "Function pass");
