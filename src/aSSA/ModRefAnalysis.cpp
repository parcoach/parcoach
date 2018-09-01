#include "ModRefAnalysis.h"
#include "Options.h"

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
ModRefAnalysis::visitAllocaInst(AllocaInst &I) {
  MemReg *r = MemReg::getValueRegion(&I);
  assert(r);
  funcLocalMap[curFunc].insert(r);
}


void
ModRefAnalysis::visitLoadInst(LoadInst &I) {
  vector<const Value *> ptsSet;
  assert(PTA->getPointsToSet(I.getPointerOperand(), ptsSet));
  vector<MemReg *> regs;
  MemReg::getValuesRegion(ptsSet, regs);

  for (MemReg *r : regs) {
    if (globalKillSet.find(r) != globalKillSet.end())
      continue;
    funcRefMap[curFunc].insert(r);
  }
}

void
ModRefAnalysis::visitStoreInst(StoreInst &I) {
  vector<const Value *> ptsSet;
  assert(PTA->getPointsToSet(I.getPointerOperand(), ptsSet));
  vector<MemReg *> regs;
  MemReg::getValuesRegion(ptsSet, regs);

  for (MemReg *r : regs) {
    if (globalKillSet.find(r) != globalKillSet.end())
      continue;
    funcModMap[curFunc].insert(r);
  }
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


  // In CUDA after a synchronization, all region in shared memory are written.
  if (optCudaTaint) {
    if (callee && callee->getName().equals("llvm.nvvm.barrier0")) {
      for (MemReg *r : MemReg::getCudaSharedRegions()) {
	if (globalKillSet.find(r) != globalKillSet.end())
	  continue;
	funcModMap[curFunc].insert(r);
      }
    }
  }

  // In OpenMP after a synchronization, all region in shared memory are written.
  if (optOmpTaint) {
    if (callee && callee->getName().equals("__kmpc_barrier")) {
      for (MemReg *r :
	     MemReg::getOmpSharedRegions(CI->getParent()->getParent())) {
	if (globalKillSet.find(r) != globalKillSet.end())
	  continue;
	funcModMap[curFunc].insert(r);
      }
    }
  }

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

    for (MemReg *r : regs) {
      if (globalKillSet.find(r) != globalKillSet.end())
	continue;
      funcRefMap[curFunc].insert(r);
    }

    // direct call
    if (callee) {
      const extModInfo *info = extInfo->getExtModInfo(callee);
      assert(info);

      // Variadic argument
      if (i >= info->nbArgs) {
			//errs() << "Function: " << callee->getName() << " in " << callee->getParent()->getName() << "\n";
				assert(callee->isVarArg());

	if (info->argIsMod[info->nbArgs-1]) {
	  for (MemReg *r : regs) {
	    if (globalKillSet.find(r) != globalKillSet.end())
	      continue;
	    funcModMap[curFunc].insert(r);
	  }
	}
      }

      // Normal argument
      else {
	if (info->argIsMod[i]) {
	  for (MemReg *r : regs) {
	    if (globalKillSet.find(r) != globalKillSet.end())
	      continue;
	    funcModMap[curFunc].insert(r);
	  }
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
	    for (MemReg *r : regs) {
	      if (globalKillSet.find(r) != globalKillSet.end())
		continue;
	      funcModMap[curFunc].insert(r);
	    }
	  }
	}

	// Normal argument
	else {
	  if (info->argIsMod[i]) {
	    for (MemReg *r : regs) {
	      if (globalKillSet.find(r) != globalKillSet.end())
		continue;
	      funcModMap[curFunc].insert(r);
	    }
	  }
	}
      }
    }
  }

  // Compute mof/ref for return value if it is a pointer.
  if (callee) {
    const extModInfo *info = extInfo->getExtModInfo(callee);
    assert(info);

    if (callee->getReturnType()->isPointerTy()) {
      vector<const Value *> retPtsSet;
      assert(PTA->getPointsToSet(CI, retPtsSet));
      vector<MemReg *> regs;
      MemReg::getValuesRegion(retPtsSet, regs);
      for (MemReg *r : regs) {
	if (globalKillSet.find(r) != globalKillSet.end())
	  continue;
	funcRefMap[curFunc].insert(r);
      }

      if (info->retIsMod) {
	for (MemReg *r : regs) {
	  if (globalKillSet.find(r) != globalKillSet.end())
	    continue;
	  funcModMap[curFunc].insert(r);
	}
      }
    }
  }

  else {
    for (const Function *mayCallee : CG.indirectCallMap[CI]) {
      if (!mayCallee->isDeclaration() || isIntrinsicDbgFunction(mayCallee))
	continue;

      const extModInfo *info = extInfo->getExtModInfo(mayCallee);
      assert(info);

      if (mayCallee->getReturnType()->isPointerTy()) {
	vector<const Value *> retPtsSet;
	assert(PTA->getPointsToSet(CI, retPtsSet));
	vector<MemReg *> regs;
	MemReg::getValuesRegion(retPtsSet, regs);
	for (MemReg *r : regs) {
	  if (globalKillSet.find(r) != globalKillSet.end())
	    continue;
	  funcRefMap[curFunc].insert(r);
	}

	if (info->retIsMod) {
	  for (MemReg *r : regs) {
	    if (globalKillSet.find(r) != globalKillSet.end())
	      continue;
	    funcModMap[curFunc].insert(r);
	  }
	}
      }
    }
  }
}

void
ModRefAnalysis::analyze() {
  unsigned nbFunctions  = CG.getModule().getFunctionList().size();
  unsigned counter = 0;

  // Compute global kill set containing regions whose allocation sites are
  // in functions not reachable from prog entry.
  vector<const Value *> allocSites;
  PTA->getAllAllocationSites(allocSites);
  for (const Value *v : allocSites) {
    const Instruction *inst = dyn_cast<Instruction>(v);
    if (!inst)
      continue;
    if (CG.isReachableFromEntry(inst->getParent()->getParent()))
      continue;
    globalKillSet.insert(MemReg::getValueRegion(v));
  }

  // First compute the mod/ref sets of each function from its load/store
  // instructions and calls to external functions.

  for (Function &F : CG.getModule()) {
    if (!CG.isReachableFromEntry(&F))
      continue;

    curFunc = &F;
    visit(&F);
  }

  // Then iterate through the PTACallGraph with an SCC iterator
  // and add mod/ref sets from callee to caller.
  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&CG);
  while(!cgSccIter.isAtEnd()) {

    const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;

    // For each function in the SCC compute kill sets
    // from callee not in the SCC and update mod/ref sets accordingly.
    for (PTACallGraphNode *node : nodeVec) {
      const Function *F = node->getFunction();
      if (F == NULL)
	continue;

      for (auto it : *node) {
	const Function *callee = it.second->getFunction();
	if (callee == NULL || F == callee)
	  continue;

	// If callee is not in the scc
	// kill(F) = kill(F) U kill(callee) U local(callee)
	if (find(nodeVec.begin(), nodeVec.end(), it.second)
	    == nodeVec.end()) {
	  for (MemReg *r : funcLocalMap[callee])
	    funcKillMap[F].insert(r);

	  // Here we have to use a vector to store regions we want to add into
	  // the funcKillMap because iterators in a DenseMap are invalidated
	  // whenever an insertion occurs unlike map.
	  vector<MemReg *> killToAdd;
	  for (MemReg *r : funcKillMap[callee])
	    killToAdd.push_back(r);
	  for (MemReg *r : killToAdd)
	    funcKillMap[F].insert(r);
	}
      }

      // Mod(F) = Mod(F) \ kill(F)
      // Ref(F) = Ref(F) \ kill(F)
      vector<MemReg *> toRemove;
      for (MemReg *r: funcModMap[F]) {
	if (funcKillMap[F].find(r) != funcKillMap[F].end())
	  toRemove.push_back(r);
      }
      for (MemReg *r : toRemove) {
	funcModMap[F].erase(r);
      }
      toRemove.clear();
      for (MemReg *r: funcRefMap[F]) {
	if (funcKillMap[F].find(r) != funcKillMap[F].end())
	  toRemove.push_back(r);
      }
      for (MemReg *r : toRemove) {
	funcRefMap[F].erase(r);
      }
    }

    // For each function in the SCC, update mod/ref sets until reaching a fixed
    // point.
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

	  // Mod(caller) = Mod(caller) U (Mod(callee) \ Kill(caller)
	  // Ref(caller) = Ref(caller) U (Ref(callee) \ Kill(caller)
	  set<MemReg *> modToAdd;
	  set<MemReg *> refToAdd;
	  modToAdd.insert(funcModMap[callee].begin(), funcModMap[callee].end());
	  refToAdd.insert(funcRefMap[callee].begin(), funcRefMap[callee].end());
	  for (MemReg *r : modToAdd) {
	    if (funcKillMap[F].find(r) == funcKillMap[F].end())
	      funcModMap[F].insert(r);
	  }
	  for (MemReg *r : refToAdd) {
	    if (funcKillMap[F].find(r) == funcKillMap[F].end())
	      funcRefMap[F].insert(r);
	  }
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

MemRegSet
ModRefAnalysis::getFuncKill(const Function *F) {
  return funcKillMap[F];
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
      errs() << "Local(";
      for (MemReg *r : funcLocalMap[F])
	errs() << r->getName() << ", ";
      errs() << ")\n";
      errs() << "Kill(";
      for (MemReg *r : funcKillMap[F])
	errs() << r->getName() << ", ";
      errs() << ")\n";
    }

    ++cgSccIter;
  }
}
