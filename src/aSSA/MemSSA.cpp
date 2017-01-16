#include "MemSSA.h"
#include "Utils.h"

#include "llvm/IR/InstIterator.h"

#include <deque>

using namespace llvm;
using namespace std;

MemSSA::MemSSA(const Function *F, DepGraph *pg,
	       PostDominatorTree &PDT,
	       DominanceFrontier &DF, DominatorTree &DT,
	       vector<Region *> &regions)
  : F(F), pg(pg), PDT(PDT), DF(DF), DT(DT), regions(regions)
{
  for (Region *r : regions)
    regionMap[r->getValue()] = r;

  computeMuChi();
  computePhi();
  rename();
}

MemSSA::~MemSSA() {}

void
MemSSA::getValueRegions(vector<Region *> &regs, const Value *v) {
  std::set<const Value *> visited;
  std::deque<const Value *> tovisit;

  tovisit.push_back(v);

  while (tovisit.size() > 0) {
    const Value *v = tovisit.front();
    tovisit.pop_front();

    if (visited.insert(v).second == false)
      continue;

    if (isa<Argument>(v) || isa<AllocaInst>(v) || isa<CallInst>(v)) {
      auto it = regionMap.find(v);
      if (it != regionMap.end()) {
	Region *r = it->second;
	regs.push_back(r);
      } else {
	errs() << "No region found for " << *v << "\n";
      }
    }

    auto pIt = pg->graph.find(v);
    if (pIt == pg->graph.end())
      continue;

    std::set<const Value *> *children = pIt->second;
    for (auto CI  = children->begin(), CE = children->end(); CI != CE; ++CI)
      tovisit.push_back(*CI);
  }
}

void
MemSSA::computeMuChi() {
  // Create EntryCHI for each memory region
  for (unsigned i=0; i<regions.size(); ++i)
    funToEntryChiMap[F].insert(new MSSAEntryChi(regions[i],
						regions[i]->getValue()));

  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    const Instruction *inst = &*I;

    // Fixme: Use CallSite instead of CallInst
    // CallInst: mu and chi for each pointer argument
    if (isa<CallInst>(inst)) {
      const CallInst *CI = cast<CallInst>(inst);
      const Function *called = CI->getCalledFunction();

      for (unsigned i=0; i<CI->getNumArgOperands(); ++i) {
	const Value *arg = CI->getArgOperand(i);
	if (arg->getType()->isPointerTy() == false)
	  continue;

	vector<Region *> valueRegs;
	getValueRegions(valueRegs, arg);
	for (Region *r : valueRegs) {
	  callToMuMap[CI][i].insert(new MSSACallMu(r, called));
	  callToChiMap[CI][i].insert(new MSSACallChi(r, called));
	  regDefToBBMap[r].insert(inst->getParent());
	}
      }
    }

    // LoadInst: compute mu()
    if (isa<LoadInst>(inst)) {
      const LoadInst *LI = cast<LoadInst>(inst);
      vector<Region *> valueRegs;
      getValueRegions(valueRegs, LI->getPointerOperand());

      for (Region *r : valueRegs) {
	loadToMuMap[LI].insert(new MSSALoadMu(r, LI));
      }
    }

    // Store inst: comptute X()
    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      vector<Region *> valueRegs;
      getValueRegions(valueRegs, SI->getPointerOperand());

      for (Region *r : valueRegs) {
	storeToChiMap[SI].insert(new MSSAStoreChi(r, SI));
	regDefToBBMap[r].insert(inst->getParent());
      }
    }

    // ReturnInst
    if (isa<ReturnInst>(inst)) {
      const ReturnInst *RI = cast<ReturnInst>(inst);
      const Value *retVal = RI->getReturnValue();
      if (retVal == NULL || retVal->getType()->isPointerTy() == false)
	continue;
      vector<Region *> valueRegs;
      getValueRegions(valueRegs, retVal);

      for (Region *r : valueRegs) {
	funToReturnMuMap[F].insert(new MSSARetMu(r));
	retToMuMap[RI].insert(new MSSARetMu(r));
      }
    }
  }
}

void
MemSSA::computePhi() {
  // For each region compute basic blocks where phi must be inserted.
  for (Region *r : regions) {
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

      auto it = DF.find(const_cast<BasicBlock *>(X));
      if (it == DF.end()) {
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
MemSSA::rename() {
  DenseMap<Region *, unsigned> C;
  DenseMap<Region *, vector<unsigned> > S;

  // Initialization:

  // C(*) <- 0
  for (auto I : C) {
    I.second = 0;
  }

  // Compute LHS version for each region.
  for (Region *r : regions) {
    S[r].push_back(C[r]);
    C[r]++;
  }

  renameBB(&F->getEntryBlock(), C, S);
}


void
MemSSA::renameBB(const llvm::BasicBlock *X,
		 DenseMap<Region *, unsigned> &C,
		 DenseMap<Region *, vector<unsigned> > &S) {
  // Compute LHS for PHI
  for (MSSAPhi *phi : bbToPhiMap[X]) {
    Region *V = phi->region;
    unsigned i = C[V];
    phi->version = i;
    S[V].push_back(i);
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

    if (isa<CallInst>(inst)) {
      const CallInst *CI = cast<CallInst>(inst);
      for (auto ptrParam : callToMuMap[CI]) {
	for (MSSAMu *mu : ptrParam.second)
	  mu->version = S[mu->region].back();
      }

      for (auto ptrParam : callToChiMap[CI]) {
	for (MSSAChi *chi : ptrParam.second) {
	  Region *V = chi->region;
	  unsigned i = C[V];
	  chi->version = i;
	  S[V].push_back(i);
	  C[V]++;
	}
      }
    }

    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      for (MSSAChi *chi : storeToChiMap[SI]) {
	Region *V = chi->region;
	unsigned i = C[V];
	chi->version = i;
	S[V].push_back(i);
	C[V]++;
      }
    }

    if (isa<LoadInst>(inst)) {
      const LoadInst *LI = cast<LoadInst>(inst);
      for (MSSAMu *mu : loadToMuMap[LI])
	mu->version = S[mu->region].back();
    }

    if (isa<ReturnInst>(inst)) {
      const ReturnInst *RI = cast<ReturnInst>(inst);
      for (MSSAMu *mu : retToMuMap[RI])
    	mu->version = S[mu->region].back();
    }
  }

  // For each successor Y of X
  //   For each Phi function F in Y
  //     Replace operands V by Vi  where i = Top(S(V))
  for (auto I = succ_begin(X), E= succ_end(X); I != E; ++I) {
    const BasicBlock *Y = *I;
    for (MSSAPhi *phi : bbToPhiMap[Y]) {
      unsigned index = whichPred(X, Y);
      phi->opsVersion[index] = S[phi->region].back();
    }
  }

  // For each successor of X in the dominator tree
  DomTreeNode *DTnode = DT.getNode(const_cast<BasicBlock *>(X));
  assert(DTnode);
  for (auto I = DTnode->begin(), E = DTnode->end(); I != E; ++I) {
    const BasicBlock *Y = (*I)->getBlock();
    renameBB(Y, C, S);
  }

  // For each assignment of A in X
  //   pop(S(A))
  for (MSSAPhi *phi : bbToPhiMap[X]) {
    Region *V = phi->region;
    S[V].pop_back();
  }
  for (auto I = X->begin(), E = X->end(); I != E; ++I) {
    const Instruction *inst = &*I;

    if (isa<CallInst>(inst)) {
      const CallInst *CI = cast<CallInst>(inst);
      for (auto ptrParam : callToChiMap[CI]) {
	for (MSSAChi *chi : ptrParam.second) {
	  Region *V = chi->region;
	S[V].pop_back();
	}
      }
    }

    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = cast<StoreInst>(inst);
      for (MSSAChi *chi : storeToChiMap[SI]) {
	Region *V = chi->region;
	S[V].pop_back();
      }
    }
  }
}

unsigned
MemSSA::whichPred(const BasicBlock *pred, const BasicBlock *succ) const {
  unsigned index = 0;
  for (auto I = pred_begin(succ), E = pred_end(succ); I != E; ++I, ++index) {
    if (pred == *I)
      return index;
  }

  assert(false);
  return index;
}

void
MemSSA::dump() {
  // Function name
  errs() << "Function " << F->getName() << "\n";

  // For each basic block
  for (auto BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
    const BasicBlock *bb = &*BI;

    // BB name
    errs() << bb->getName() << ":\n";

    // Phi functions
    for (MSSAPhi *phi : bbToPhiMap[bb]) {
      errs() << phi->region->getName() << phi->version << " = PHI( ";
      for (auto I : phi->opsVersion)
	errs() << phi->region->getName() << I.second << ", ";
      errs() << "\n";
    }

    // For each instruction
    for (auto I = bb->begin(), E = bb->end(); I != E; ++I) {
      const Instruction *inst = &*I;

      if (isa<GetElementPtrInst>(inst) ||
	  isa<CastInst>(inst))
	continue;

      if (const LoadInst *LI = dyn_cast<LoadInst>(inst)) {
	errs() << getValueLabel(LI) << " = op(";

	for (MSSAMu *mu : loadToMuMap[LI])
	  errs() << mu->region->getName() << mu->version << ", ";
	errs() << ")\n";
	continue;
      }

      if (const StoreInst *SI = dyn_cast<StoreInst>(inst)) {
	for (MSSAChi *chi : storeToChiMap[SI]) {
	  errs() << chi->region->getName() << chi->version << " = op(";
	  errs() << chi->region->getName() << chi->opVersion << ",";
	  errs() << getValueLabel(SI->getValueOperand()) << ")\n";
	}
	continue;
      }

      if (const CallInst *CI = dyn_cast<CallInst>(inst)) {
	const Function *called = CI->getCalledFunction();
	errs() << getValueLabel(CI) << " = call " << called->getName() << "(";

	for (unsigned i=0; i<CI->getNumArgOperands(); ++i) {
	  const Value *arg = CI->getArgOperand(i);
	  if (arg->getType()->isPointerTy() == false || isa<Constant>(arg)) {
	    errs() << *arg << ",";
	    continue;
	  }

	  errs() << " op( ";
	  for (MSSAMu *mu : callToMuMap[CI][i])
	    errs() << mu->region->getName() << mu->version << ",";
	  errs() << "),";
	  continue;
	}
	errs() << "\n";

	for (auto paramPtr : callToChiMap[CI]) {
	  for (MSSAChi *chi : paramPtr.second)
	    errs() << chi->region->getName() << chi->version << " = "
		   << called->getName() << "_arg_" << paramPtr.first << "\n";
	}
	continue;
      }

      if (const ReturnInst *RI = dyn_cast<ReturnInst>(inst)) {
	const Value *retVal = RI->getReturnValue();
	if (retVal == NULL || retVal->getType()->isPointerTy() == false) {
	  errs() << *RI << "\n";
	  continue;
	}

	errs() << "return op(";
	for (MSSAMu *mu : retToMuMap[RI])
	  errs() << mu->region->getName() << mu->version << ",";
	errs() << ")\n";
	continue;
      }

      if (const PHINode *P = dyn_cast<PHINode>(inst)) {
	errs() << *P << " predicate = {";
	for (auto pred : llvmPhiASSA[P])
	  errs() << getValueLabel(pred) << ",";
	errs() << "}\n";
	continue;
      }

      errs() << *inst << "\n";
    }
  }
}

void
MemSSA::computeASSA() {
  // Compute augmented SSA for LLVM PhiNode
  for (inst_iterator I = inst_begin(const_cast<Function *>(F)),
	 E = inst_end(const_cast<Function *>(F)); I != E; ++I) {
    Instruction *inst = &*I;

    PHINode *phi = dyn_cast<PHINode>(inst);
    if (!phi)
      continue;

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

	  llvmPhiASSA[phi].insert(cond);
	} else if(isa<SwitchInst>(ti)) {
	  const SwitchInst *si = cast<SwitchInst>(ti);
	  assert(si);
	  const Value *cond = si->getCondition();
	  llvmPhiASSA[phi].insert(cond);
	}
      }
    }

    // Compute augmented SSA for MemorySSA Phi nodes
    // TODO
  }
}
