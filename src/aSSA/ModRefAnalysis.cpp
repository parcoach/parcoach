#include "ModRefAnalysis.h"

#include "llvm/ADT/SCCIterator.h"

using namespace llvm;
using namespace std;

ModRefAnalysis::ModRefAnalysis(PTACallGraph &CG, Andersen *PTA)
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

  // indirect call
  if (!callee) {
    bool mayCallExternalFunction = false;
    for (const Function *mayCallee : CG.indirectCallMap[CI]) {
      if (mayCallee->isDeclaration()) {
	mayCallExternalFunction = true;
	break;
      }
    }
    if (!mayCallExternalFunction)
      return;
  }

  // direct call
  else {
    if (!callee->isDeclaration())
      return;
  }

  for (unsigned i=0; i<CI->getNumArgOperands(); ++i) {
    const Value *arg = CI->getArgOperand(i);
    if (arg->getType()->isPointerTy() == false)
      continue;

    // Case where argument is a inttoptr cast (e.g. MPI_IN_PLACE)
    const ConstantExpr *ce = dyn_cast<ConstantExpr>(arg);
    if (ce) {
      Instruction *inst = const_cast<ConstantExpr *>(ce)->getAsInstruction();
      assert(inst);
      if(isa<IntToPtrInst>(inst)) {
	delete inst;
	continue;
      }
      delete inst;
    }

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

void
ModRefAnalysis::analyze() {
  unsigned nbFunctions  = CG.getModule().getFunctionList().size();
  unsigned counter = 0;

  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&CG);
  while(!cgSccIter.isAtEnd()) {

    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;

    // For each function in the SCC add mod/ref from load/store.
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (F == NULL || isIntrinsicDbgFunction(F))
	continue;
      curFunc = F;
      visit(F);
    }

    // For each function in the SCC add mod/red from callee until reaching a
    // fixed point.
    bool changed = true;
    while (changed) {
      changed = false;
      for (PTACallGraphNode *node : nodeVec) {
	const Function *F = node->getFunction();
	if (F == NULL)
	  continue;

	unsigned modSize = funcModMap[F].size();
	unsigned refSize = funcRefMap[F].size();

	for (auto it : *node) {
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

    counter += nodeVec.size();

    if (counter%100 == 0) {
      errs() << "Mod/Ref: visited " << counter << " functions over " << nbFunctions
	     << " (" << (((float) counter)/nbFunctions*100) << "%)\n";
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
  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&CG);
  while(!cgSccIter.isAtEnd()){
    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;

    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (F == NULL || isIntrinsicDbgFunction(F))
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
