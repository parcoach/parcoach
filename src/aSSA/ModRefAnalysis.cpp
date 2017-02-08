#include "ModRefAnalysis.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraphSCCPass.h"

using namespace llvm;
using namespace std;

ModRefAnalysis::ModRefAnalysis(CallGraph &CG, Andersen *PTA)
  : CG(CG), PTA(PTA) {
  analyze();
}

ModRefAnalysis::~ModRefAnalysis() {}

void
ModRefAnalysis::visitLoadInst(LoadInst &I) {
  vector<const Value *> ptsSet;
  assert(PTA->getPointsToSet(I.getPointerOperand(), ptsSet));
  vector<MemReg *> regs;
  MemReg::getValuesRegion(ptsSet, regs);

  for (MemReg *r : regs)
    funcRefMap[curFunc].insert(r);
}

void
ModRefAnalysis::visitStoreInst(StoreInst &I) {
  vector<const Value *> ptsSet;
  assert(PTA->getPointsToSet(I.getPointerOperand(), ptsSet));
  vector<MemReg *> regs;
  MemReg::getValuesRegion(ptsSet, regs);

  for (MemReg *r : regs)
    funcModMap[curFunc].insert(r);
}

void
ModRefAnalysis::visitCallSite(CallSite CS) {
  // For each external function called, add the region of each the pointer
  // parameters passed to the function to the mod/ref set of the calling
  // function.

  assert(CS.isCall());
  CallInst *CI = cast<CallInst>(CS.getInstruction());
  const Function *callee = CI->getCalledFunction();
  if (callee->isDeclaration()) {
    for (unsigned i=0; i<CI->getNumArgOperands(); ++i) {
      const Value *arg = CI->getArgOperand(i);
      if (arg->getType()->isPointerTy() == false)
	continue;

      vector<const Value *> ptsSet;
      vector<const Value *> argPtsSet;
      assert(PTA->getPointsToSet(arg, argPtsSet));
      ptsSet.insert(ptsSet.end(), argPtsSet.begin(), argPtsSet.end());

      vector<MemReg *> regs;
      MemReg::getValuesRegion(ptsSet, regs);

      for (MemReg *r : regs) {
	funcModMap[curFunc].insert(r);
	funcRefMap[curFunc].insert(r);
      }
    }
  }
}

void
ModRefAnalysis::analyze() {
  scc_iterator<CallGraph *> cgSccIter = scc_begin(&CG);
  CallGraphSCC curSCC(CG, &cgSccIter);
  while(!cgSccIter.isAtEnd()){
    const vector<CallGraphNode*> &nodeVec = *cgSccIter;
    curSCC.initialize(nodeVec.data(), nodeVec.data() + nodeVec.size());

    // For each function in the SCC add mod/ref from load/store.
    for (auto I = curSCC.begin(), E = curSCC.end(); I != E; ++I) {
      Function *F = (*I)->getFunction();
      if (F == NULL)
	continue;
      curFunc = F;
      visit(F);
    }

    // For each function in the SCC add mod/red from callee until reaching a
    // fixed point.
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto I = curSCC.begin(), E = curSCC.end(); I != E; ++I) {
	CallGraphNode *CGN = *I;
	const Function *F = CGN->getFunction();
	if (F == NULL)
	  continue;

	unsigned modSize = funcModMap[F].size();
	unsigned refSize = funcRefMap[F].size();

	for (auto it : *CGN) {
	  const Function *callee = it.second->getFunction();
	  if (callee == NULL || F == callee)
	    continue;

	  funcModMap[F].insert(funcModMap[callee].begin(),
			       funcModMap[callee].end());
	  funcRefMap[F].insert(funcRefMap[callee].begin(),
			       funcRefMap[callee].end());
	}

	if (funcModMap[F].size() > modSize || funcRefMap[F].size() > refSize)
	  changed = true;
      }
    }

    ++cgSccIter;
  }
}

MemRegSet
ModRefAnalysis::getFuncMod(const Function *F) {
  return funcModMap[F];
}

MemRegSet
ModRefAnalysis::getFuncRef(const Function *F) {
  return funcRefMap[F];
}

void
ModRefAnalysis::dump() {
  scc_iterator<CallGraph *> cgSccIter = scc_begin(&CG);
  CallGraphSCC curSCC(CG, &cgSccIter);
  while(!cgSccIter.isAtEnd()){
    const vector<CallGraphNode*> &nodeVec = *cgSccIter;
    curSCC.initialize(nodeVec.data(), nodeVec.data() + nodeVec.size());

    for (auto I = curSCC.begin(), E = curSCC.end(); I != E; ++I) {
      Function *F = (*I)->getFunction();
      if (F == NULL)
	continue;

      errs() << "Mod/Ref for function " << F->getName() << ":\n";
      errs() << "Mod(";
      for (MemReg *r : funcModMap[F])
	errs() << r->getName() << ", ";
      errs() << ")\n";
      errs() << "Ref(";
      for (MemReg *r : funcRefMap[F])
	errs() << r->getName() << ", ";
      errs() << ")\n";
    }

    ++cgSccIter;
  }
}
