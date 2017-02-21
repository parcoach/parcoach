#include "ModRefAnalysis.h"

#include "llvm/ADT/SCCIterator.h"

using namespace llvm;
using namespace std;

ModRefAnalysis::ModRefAnalysis(PTACallGraph &CG, Andersen *PTA,
			       ExtInfo *extInfo)
  : CG(CG), PTA(PTA), extInfo(extInfo) {
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
  // For each external function called, add the region of each pointer
  // parameters passed to the function to the ref set of the called
  // function. Regions are added to the Mod set only if the parameter is
  // modified in the callee.

  assert(CS.isCall());
  CallInst *CI = cast<CallInst>(CS.getInstruction());
  const Function *callee = CI->getCalledFunction();

  // indirect call
  if (!callee) {
    bool mayCallExternalFunction = false;
    for (const Function *mayCallee : CG.indirectCallMap[CI]) {
      if (mayCallee->isDeclaration() && !isIntrinsicDbgFunction(mayCallee)) {
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

    if (isIntrinsicDbgFunction(callee))
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

    vector<const Value *> argPtsSet;

    assert(PTA->getPointsToSet(arg, argPtsSet));
    vector<MemReg *> regs;
    MemReg::getValuesRegion(argPtsSet, regs);

    for (MemReg *r : regs)
      funcRefMap[curFunc].insert(r);

    // direct call
    if (callee) {
      const extModInfo *info = extInfo->getExtModInfo(callee);
      assert(info);

      // Variadic argument
      if (i >= info->nbArgs) {
	assert(callee->isVarArg());

	if (info->argIsMod[info->nbArgs-1]) {
	  for (MemReg *r : regs)
	    funcModMap[curFunc].insert(r);
	}
      }

      // Normal argument
      else {
	if (info->argIsMod[i]) {
	  for (MemReg *r : regs)
	    funcModMap[curFunc].insert(r);
	}
      }
    }

    // indirect call
    else {
      for (const Function *mayCallee : CG.indirectCallMap[CI]) {
    	if (!mayCallee->isDeclaration() || isIntrinsicDbgFunction(mayCallee))
    	  continue;

	const extModInfo *info = extInfo->getExtModInfo(mayCallee);
	assert(info);

	// Variadic argument
	if (i >= info->nbArgs) {
	  assert(mayCallee->isVarArg());

	  if (info->argIsMod[info->nbArgs-1]) {
	    for (MemReg *r : regs)
	      funcModMap[curFunc].insert(r);
	  }
	}

	// Normal argument
	else {
	  if (info->argIsMod[i]) {
	    for (MemReg *r : regs)
	      funcModMap[curFunc].insert(r);
	  }
	}
      }
    }
  }

  // Compute mof/ref for return value if it is a pointer.
  if (callee) {
    const extModInfo *info = extInfo->getExtModInfo(callee);
    errs() << "callee: " << callee->getName() << "\n";
    assert(info);

    if (callee->getReturnType()->isPointerTy()) {
      vector<const Value *> retPtsSet;
      assert(PTA->getPointsToSet(CI, retPtsSet));
      vector<MemReg *> regs;
      MemReg::getValuesRegion(retPtsSet, regs);
      for (MemReg *r : regs)
	funcRefMap[curFunc].insert(r);

      if (info->retIsMod) {
	for (MemReg *r : regs)
	  funcModMap[curFunc].insert(r);
      }
    }
  }

  else {
    for (const Function *mayCallee : CG.indirectCallMap[CI]) {
      if (!mayCallee->isDeclaration() || isIntrinsicDbgFunction(mayCallee))
	continue;

      const extModInfo *info = extInfo->getExtModInfo(callee);
      assert(info);

      if (mayCallee->getReturnType()->isPointerTy()) {
	vector<const Value *> retPtsSet;
	assert(PTA->getPointsToSet(CI, retPtsSet));
	vector<MemReg *> regs;
	MemReg::getValuesRegion(retPtsSet, regs);
	for (MemReg *r : regs)
	  funcRefMap[curFunc].insert(r);

	if (info->retIsMod) {
	  for (MemReg *r : regs)
	    funcModMap[curFunc].insert(r);
	}
      }
    }
  }
}

void
ModRefAnalysis::analyze() {
  unsigned nbFunctions  = CG.getModule().getFunctionList().size();
  unsigned counter = 0;

  // First compute the mod/ref sets of each function from its load/store
  // instructions and calls to external functions.

  for (Function &F : CG.getModule()) {
    curFunc = &F;
    visit(&F);
  }

  // Then iterate through the PTACallGraph with an SCC iterator
  // and add mod/ref sets from callee to caller.
  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&CG);
  while(!cgSccIter.isAtEnd()) {

    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;

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
