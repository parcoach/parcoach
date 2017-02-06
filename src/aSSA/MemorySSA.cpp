#include "MemorySSA.h"
#include "Utils.h"

#include "llvm/IR/InstIterator.h"

using namespace std;
using namespace llvm;

MemorySSA::MemorySSA(Module *m, Andersen *PTA)
  : m(m), PTA(PTA), curDF(NULL), curDT(NULL) {}
MemorySSA::~MemorySSA() {}

void
MemorySSA::buildSSA(const Function *F, DominatorTree &DT,
		    DominanceFrontier &DF, PostDominatorTree &PDT) {
  curDF = &DF;
  curDT = &DT;
  curPDT = &PDT;

  usedRegs.clear();
  regDefToBBMap.clear();

  computeMuChi(F);

  computePhi(F);

  rename(F);

  computePhiPredicates(F);

  dumpMSSA(F);
}

void
MemorySSA::computeMuChi(const Function *F) {
  // TODO: assert there is a single exit node.

  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    /* Call Site
     * We insert a Mu and a Chi function for each pointer parameter which
     * correspond to the memory value before/after entering/returning the
     * function.
     */
    if (isCallSite(inst)) {
      CallSite cs(const_cast<Instruction *>(inst));
      Function *called = cs.getCalledFunction();
      assert(called);

      assert(isa<CallInst>(inst)); // InvokeInst are not handled yet
      const CallInst *CI = cast<CallInst>(inst);


      // Mu and Chi for parameters
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

	for (MemReg * r : regs) {
	  callSiteToMuMap[cs].insert(new MSSACallMu(r, called, i));
	  callSiteToChiMap[cs].insert(new MSSACallChi(r, called, i));
	  regDefToBBMap[r].insert(inst->getParent());
	  usedRegs.insert(r);
	}
      }

      // Chi for return value if it is a pointer
      if (!called->isDeclaration() && CI->getType()->isPointerTy()) {
	vector<const Value *> ptsSet;
	assert(PTA->getPointsToSet(CI, ptsSet));
	vector<MemReg *> regs;
	MemReg::getValuesRegion(ptsSet, regs);

	for (MemReg *r : regs) {
	  callSiteToRetChiMap[cs].insert(new MSSARetCallChi(r, called));
	  regDefToBBMap[r].insert(inst->getParent());
	  usedRegs.insert(r);
	}
      }

      continue;
    }

    /* Load Instruction
     * We insert a Mu function for each region pointed-to by
     * the load instruction.
     */
    if (isa<LoadInst>(inst)) {
      const LoadInst *LI = cast<LoadInst>(inst);
      vector<const Value *> ptsSet;
      assert(PTA->getPointsToSet(LI->getPointerOperand(), ptsSet));
      vector<MemReg *> regs;
      MemReg::getValuesRegion(ptsSet, regs);

      for (MemReg * r : regs) {
	loadToMuMap[LI].insert(new MSSALoadMu(r, LI));
	usedRegs.insert(r);
      }

      continue;
    }

    /* Store Instruction
     * We insert a Chi function for each region pointed-to by
     * the store instruction.
     */
    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      vector<const Value *> ptsSet;
      assert(PTA->getPointsToSet(SI->getPointerOperand(), ptsSet));
      vector<MemReg *> regs;
      MemReg::getValuesRegion(ptsSet, regs);

      for (MemReg * r : regs) {
	storeToChiMap[SI].insert(new MSSAStoreChi(r, SI));
	usedRegs.insert(r);
	regDefToBBMap[r].insert(inst->getParent());
      }

      continue;
    }
  }

  /* Create an EntryChi and a ReturnMu for each memory region used by the
   * function.
   */
  for (MemReg *r : usedRegs) {
    errs() << "function : " << F->getName() << " used regs :\n";
    errs() << r->getName() << "\n";
    funToEntryChiMap[F].insert(new MSSAEntryChi(r, F));
    funToReturnMuMap[F].insert(new MSSARetMu(r, F));
  }

  for (MSSAChi *chi : funToEntryChiMap[F])
    funRegToEntryChiMap[F][chi->region] = chi;
  for (MSSAMu *mu : funToReturnMuMap[F])
    funRegToReturnMuMap[F][mu->region] = mu;
}

void
MemorySSA::computePhi(const Function *F) {
  // For each memory region used, compute basic blocks where phi must be
  // inserted.
  for (MemReg *r : usedRegs) {
    vector<const BasicBlock *> worklist;
    set<const BasicBlock *> domFronPlus;
    set<const BasicBlock *> work;

    for (const BasicBlock *X : regDefToBBMap[r]) {
      worklist.push_back(X);
      work.insert(X);
    }

    while (!worklist.empty()) {
      const BasicBlock *X = worklist.back();
      worklist.pop_back();

      auto it = curDF->find(const_cast<BasicBlock *>(X));
      if (it == curDF->end()) {
	errs() << "Error: basic block not in the dom frontier !\n";
	continue;
      }

      auto domSet = it->second;
      for (auto Y : domSet) {
	if (domFronPlus.find(Y) != domFronPlus.end())
	  continue;

	bbToPhiMap[Y].insert(new MSSAPhi(r));
	domFronPlus.insert(Y);

	if (work.find(Y) == work.end())
	  continue;

	work.insert(Y);
	worklist.push_back(Y);
      }
    }
  }
}

void
MemorySSA::rename(const Function *F) {
  DenseMap<MemReg *, unsigned> C;
  DenseMap<MemReg *, vector<MSSAVar *> > S;

  // Initialization:

  // C(*) <- 0
  for (MemReg *r : usedRegs) {
    C[r] = 0;
  }

  // Compute LHS version for each region.
  for (MSSAChi *chi : funToEntryChiMap[F]) {
    chi->var = new MSSAVar(chi, 0, &F->getEntryBlock());

    S[chi->region].push_back(chi->var);
    C[chi->region]++;
  }

  renameBB(F, &F->getEntryBlock(), C, S);
}

void
MemorySSA::renameBB(const Function *F, const llvm::BasicBlock *X,
		    DenseMap<MemReg *, unsigned> &C,
		    DenseMap<MemReg *, vector<MSSAVar *> > &S) {
  // Compute LHS for PHI
  for (MSSAPhi *phi : bbToPhiMap[X]) {
    MemReg *V = phi->region;
    unsigned i = C[V];
    phi->var = new MSSAVar(phi, i, X);
    S[V].push_back(phi->var);
    C[V]++;
  }

  // For each ordinary assignment A do
  //   For each variable V in RHS(A)
  //     Replace use of V by use of Vi where i = Top(S(V))
  //   Let V be LHS(A)
  //   i <- C(V)
  //   replace V by Vi
  //   push i onto S(V)
  //   C(V) <- i + 1
  for (auto I = X->begin(), E = X->end(); I != E; ++I) {
    const Instruction *inst = &*I;


    if (isCallSite(inst)) {
      CallSite cs(const_cast<Instruction *>(inst));

      for (MSSAMu *mu : callSiteToMuMap[cs])
	mu->var = S[mu->region].back();

      for (MSSAChi *chi : callSiteToChiMap[cs]) {
	MemReg *V = chi->region;
	unsigned i = C[V];
	chi->var = new MSSAVar(chi, i, inst->getParent());
	chi->opVar = S[V].back();
	S[V].push_back(chi->var);
	C[V]++;
      }

      for (MSSAChi *chi : callSiteToRetChiMap[cs]) {
	MemReg *V = chi->region;
	unsigned i = C[V];
	chi->var = new MSSAVar(chi, i, inst->getParent());
	chi->opVar = S[V].back();
	S[V].push_back(chi->var);
	C[V]++;
      }
    }

    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      for (MSSAChi *chi : storeToChiMap[SI]) {
	MemReg *V = chi->region;
	unsigned i = C[V];
	chi->var = new MSSAVar(chi, i, inst->getParent());
	chi->opVar = S[V].back();
	S[V].push_back(chi->var);
	C[V]++;
      }
    }

    if (isa<LoadInst>(inst)) {
      const LoadInst *LI = cast<LoadInst>(inst);
      for (MSSAMu *mu : loadToMuMap[LI])
	mu->var = S[mu->region].back();
    }

    if (isa<ReturnInst>(inst)) {
      for (MSSAMu *mu : funToReturnMuMap[F]) {
	mu->var = S[mu->region].back();
      }
    }
  }

  // For each successor Y of X
  //   For each Phi function F in Y
  //     Replace operands V by Vi  where i = Top(S(V))
  for (auto I = succ_begin(X), E= succ_end(X); I != E; ++I) {
    const BasicBlock *Y = *I;
    for (MSSAPhi *phi : bbToPhiMap[Y]) {
      unsigned index = whichPred(X, Y);
      phi->opsVar[index] = S[phi->region].back();
    }
  }

  // For each successor of X in the dominator tree
  DomTreeNode *DTnode = curDT->getNode(const_cast<BasicBlock *>(X));
  assert(DTnode);
  for (auto I = DTnode->begin(), E = DTnode->end(); I != E; ++I) {
    const BasicBlock *Y = (*I)->getBlock();
    renameBB(F, Y, C, S);
  }

  // For each assignment of A in X
  //   pop(S(A))
  for (MSSAPhi *phi : bbToPhiMap[X]) {
    MemReg *V = phi->region;
    S[V].pop_back();
  }
  for (auto I = X->begin(), E = X->end(); I != E; ++I) {
    const Instruction *inst = &*I;

    if (isa<CallInst>(inst)) {
      CallSite cs(const_cast<Instruction *>(inst));

      for (MSSAChi *chi : callSiteToChiMap[cs]) {
	MemReg *V = chi->region;
	S[V].pop_back();
      }

      for (MSSAChi *chi : callSiteToRetChiMap[cs]) {
	MemReg *V = chi->region;
	S[V].pop_back();
      }
    }

    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      for (MSSAChi *chi : storeToChiMap[SI]) {
	MemReg *V = chi->region;
	S[V].pop_back();
      }
    }
  }
}

void
MemorySSA::computePhiPredicates(const llvm::Function *F) {
  ValueSet preds;

  for (const BasicBlock &bb : *F) {
    for (MSSAPhi *phi : bbToPhiMap[&bb]) {
      computeMSSAPhiPredicates(phi);
    }

    for (const Instruction &inst : bb) {
      const PHINode *phi = dyn_cast<PHINode>(&inst);
      if (!phi)
	continue;
      computeLLVMPhiPredicates(phi);
    }
  }
}

void
MemorySSA::computeLLVMPhiPredicates(const llvm::PHINode *phi) {
  // For each argument of the PHINode
  for (unsigned i=0; i<phi->getNumIncomingValues(); ++i) {
    // Get IPDF
    vector<BasicBlock *> IPDF =
      iterated_postdominance_frontier(*curPDT,
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

	llvmPhiToPredMap[phi].insert(cond);
      } else if(isa<SwitchInst>(ti)) {
	const SwitchInst *si = cast<SwitchInst>(ti);
	assert(si);
	const Value *cond = si->getCondition();
	llvmPhiToPredMap[phi].insert(cond);
      }
    }
  }
}

void
MemorySSA::computeMSSAPhiPredicates(MSSAPhi *phi) {
  // For each argument of the PHINode
  for (auto I : phi->opsVar) {
    MSSAVar *op = I.second;
    // Get IPDF
    vector<BasicBlock *> IPDF =
      iterated_postdominance_frontier(*curPDT,
				      const_cast<BasicBlock *>(op->bb));

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

  	phi->preds.insert(cond);
      } else if(isa<SwitchInst>(ti)) {
  	const SwitchInst *si = cast<SwitchInst>(ti);
  	assert(si);
  	const Value *cond = si->getCondition();
	phi->preds.insert(cond);
      }
    }
  }
}


unsigned
MemorySSA::whichPred(const BasicBlock *pred,
		     const BasicBlock *succ) const {
  unsigned index = 0;
  for (auto I = pred_begin(succ), E = pred_end(succ); I != E; ++I, ++index) {
    if (pred == *I)
      return index;
  }

  assert(false);
  return index;
}

void
MemorySSA::dumpMSSA(const llvm::Function *F) {
  // Function header
  errs() << "define " << *F->getReturnType() << " @" << F->getName() << "(";
  for (const Argument &arg : F->getArgumentList())
    errs() << arg << ", ";
  errs() << ") {\n";

  // Dump entry chi
  for (MSSAChi *chi : funToEntryChiMap[F])
    errs() << chi->region->getName() << chi->var->version << "\n";

  // For each basic block
  for (auto BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
    const BasicBlock *bb = &*BI;

    // BB name
    errs() << bb->getName() << ":\n";

    // Phi functions
    for (MSSAPhi *phi : bbToPhiMap[bb]) {
      errs() << phi->region->getName() << phi->var->version << " = phi( ";
      for (auto I : phi->opsVar)
	errs() << phi->region->getName() << I.second->version << ", ";
      for (const Value *v : phi->preds)
	errs() << getValueLabel(v) << ", ";
      errs() << ")\n";
    }

    // For each instruction
    for (auto I = bb->begin(), E = bb->end(); I != E; ++I) {
      const Instruction *inst = &*I;

      // LLVM Phi node: print predicates
      if (const PHINode *PHI = dyn_cast<PHINode>(inst)) {
	errs() << getValueLabel(PHI) << " = phi(";
	for (const Value *incoming : PHI->incoming_values())
	  errs() << getValueLabel(incoming) << ", ";
	for (const Value *pred : llvmPhiToPredMap[PHI])
	  errs() << getValueLabel(pred) << ", ";
	errs() << ")\n";
	continue;
      }

      // Load inst
      if (const LoadInst *LI = dyn_cast<LoadInst>(inst)) {
	errs() << getValueLabel(LI) << " = mu(";

	for (MSSAMu *mu : loadToMuMap[LI])
	  errs() << mu->region->getName() << mu->var->version << ", ";

	errs() << getValueLabel(LI->getPointerOperand()) << ")\n";
	continue;
      }

      // Store inst
      if (const StoreInst *SI = dyn_cast<StoreInst>(inst)) {
	for (MSSAChi *chi : storeToChiMap[SI]) {
	  errs() << chi->region->getName() << chi->var->version << " = X("
		 << chi->region->getName() << chi->opVar->version << ", "
		 << getValueLabel(SI->getValueOperand()) << ", "
		 << getValueLabel(SI->getPointerOperand()) << ")\n";
	}
	continue;
      }

      // Call inst
      if (const CallInst *CI = dyn_cast<CallInst>(inst)) {
	CallSite cs(const_cast<CallInst *>(CI));
	errs() << *CI << "\n";
	for (MSSAMu *mu : callSiteToMuMap[cs])
	  errs() << "  mu(" << mu->region->getName() << mu->var->version
		 << ")\n";

	for (MSSAChi *chi : callSiteToChiMap[cs])
	  errs() << chi->region->getName() << chi->var->version << " = "
		 << "  X(" << chi->region->getName() << chi->opVar->version
		 << ")\n";

	for (MSSAChi *chi : callSiteToRetChiMap[cs])
	  errs() << chi->region->getName() << chi->var->version << " = "
		 << "  X(" << chi->region->getName() << chi->opVar->version
		 << ")\n";
	continue;
      }

      errs() << *inst << "\n";
    }
  }

  // Dump return mu
  for (MSSAMu *mu : funToReturnMuMap[F])
    errs() << mu->region->getName() << "OUT = u("
	   << mu->region->getName() << mu->var->version << ")\n";

  errs() << "}\n";
}

void
MemorySSA::buildExtSSA(const llvm::Function *F) {
  for (const Argument &arg : F->getArgumentList()) {
    if (!arg.getType()->isPointerTy())
      continue;

    MemReg::createRegion(&arg);
    MemReg *reg = MemReg::getValueRegion(&arg);
    extArgToRegMap[&arg] = reg;

    errs() << "region created for ext function : "  << reg->getName() << "\n";

    MSSAChi *entryChi = new MSSAEntryChi(reg, F);
    funToEntryChiMap[F].insert(entryChi);
    funRegToEntryChiMap[F][reg] = entryChi;
    //    entryChi->opVersion = new MSSAVar(NULL, 0, NULL);
    entryChi->var = new MSSAVar(entryChi, 0, NULL);

    MSSAChi *outChi = new MSSAEntryChi(reg, NULL);
    outChi->var = new MSSAVar(outChi, 1, NULL);
    outChi->opVar = entryChi->var;

    funToEntryChiMap[F].insert(outChi);

    MSSAMu *returnMu = new MSSARetMu(reg, F);
    funToReturnMuMap[F].insert(returnMu);
    funRegToReturnMuMap[F][reg] = returnMu;
    returnMu->var = outChi->var;
  }
}
