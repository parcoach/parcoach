#include "DepGraphBuilder.h"

#include "llvm/Support/raw_ostream.h"

using namespace std;
using namespace llvm;

void
DepGraphBuilder::visitInstruction(Instruction &I) {
  errs() << "Error: instruction " << I << " not implemented yet !\n";
  exit(EXIT_FAILURE);
}

void
DepGraphBuilder::visitLoadInst(LoadInst &I) {
  dg.addEdge(I.getPointerOperand(), &I);

  // Add a backedge if the value loaded is a pointer of pointer.
  SequentialType *pointerTy =
    dyn_cast<SequentialType>(I.getPointerOperand()->getType());
  assert(pointerTy);
  if (pointerTy->getElementType()->isPointerTy())
    dg.addEdge(&I, I.getPointerOperand());
}

void
DepGraphBuilder::visitStoreInst(StoreInst &I) {
  dg.addEdge(I.getValueOperand(), I.getPointerOperand());

  // Add a backedge if the value stored is a pointer.
  if (I.getValueOperand()->getType()->isPointerTy())
    dg.addEdge(I.getPointerOperand(), I.getValueOperand());

  // Add PDF+ predicates dependencies
  addIPDFDeps(I, I.getPointerOperand());

  // Add func PDF+ predicates dependencies.
  dg.addEdge(dg.getIPDFFuncNode(I.getParent()->getParent()),
	     I.getPointerOperand());
}

void
DepGraphBuilder::visitMemSetInst(MemSetInst &I) {
  dg.addEdge(I.getValue(), I.getDest());

  // Add PDF+ predicates dependencies.
  addIPDFDeps(I, I.getDest());

  // Add func PDF+ predicates dependencies.
  dg.addEdge(dg.getIPDFFuncNode(I.getParent()->getParent()),
	     I.getDest());
}

void
DepGraphBuilder::addIPDFDeps(Instruction &I, const Value *dest) {
  vector<BasicBlock *> IPDF =
    iterated_postdominance_frontier(PDT, I.getParent());

  for (unsigned n = 0; n < IPDF.size(); ++n) {
    const TerminatorInst *ti = IPDF[n]->getTerminator();
    assert(ti);

    if (isa<BranchInst>(ti)) {
      const BranchInst *bi = cast<BranchInst>(ti);

      if (bi->isUnconditional())
	continue;

      const Value *cond = bi->getCondition();
      dg.addEdge(cond, dest);
    } else if (isa<SwitchInst>(ti)) {
      const SwitchInst *si = cast<SwitchInst>(ti);
      const Value *cond = si->getCondition();
      dg.addEdge(cond, dest);
    } else {
      assert(false);
    }
  }
}

void
DepGraphBuilder::visitMemTransferInst(MemTransferInst &I) {
  dg.addEdge(I.getSource(), I.getDest());

  // Add a backedge is the value is a pointer.
  SequentialType *pointerTy =
    dyn_cast<SequentialType>(I.getSource()->getType());
  assert(pointerTy);
  if (pointerTy->getElementType()->isPointerTy())
    dg.addEdge(I.getDest(), I.getSource());

  // Add PDF+ predicates dependencies.
  addIPDFDeps(I, I.getDest());

  // Add func PDF+ predicates dependencies.
  dg.addEdge(dg.getIPDFFuncNode(I.getParent()->getParent()),
	     I.getDest());
}

void
DepGraphBuilder::visitCallInst(CallInst &I) {
  const Function *called = I.getCalledFunction();

  // Handle malloc special case.
  if (isMemoryAlloc(called)) {
    for (unsigned i=0; i<I.getNumOperands(); ++i)
      dg.addEdge(I.getArgOperand(i), &I);

    addIPDFDeps(I, &I);

    return;
  }

  // F -> call
  dg.addEdge(I.getCalledFunction(), &I);

  // Add a backedge if the value returned by the function is a pointer.
  if (called->getReturnType()->isPointerTy())
    dg.addEdge(&I, I.getCalledFunction());

  // MPI_Barrier
  if (called->getName().equals("MPI_Barrier"))
    dg.addSink(&I);

  // MPI_Comm_rank
  if (called->isDeclaration()) {
    if (called->getName().equals("MPI_Comm_rank")) {
      dg.addEdge(called, I.getArgOperand(1));
      dg.addSource(I.getArgOperand(1));
    }
    return;
  }

  // Add PDF+ predicates dependencies.
  addIPDFDeps(I, dg.getIPDFFuncNode(called));

  // Link func PDF+ of caller with func PDF+ of callee.
  dg.addEdge(dg.getIPDFFuncNode(I.getParent()->getParent()),
	     dg.getIPDFFuncNode(called));

  // Connect real parameters to formal parameters.
  for (unsigned i=0; i<I.getNumArgOperands(); ++i) {
    const Value *realArg = I.getArgOperand(i);
    const Value *formalArg = getFunctionArgument(called, i);
    dg.addEdge(realArg, formalArg);

    if (formalArg->getType()->isPointerTy())
      dg.addEdge(formalArg, realArg);
  }
}

void
DepGraphBuilder::visitBinaryOperator(BinaryOperator &I) {
  dg.addEdge(I.getOperand(0), &I);
  dg.addEdge(I.getOperand(1), &I);
}

void
DepGraphBuilder::visitCastInst(CastInst &I){
  dg.addEdge(I.getOperand(0), &I);

  // Add a backedge if casted value is a pointer.
  if (I.getSrcTy()->isPointerTy() || I.getDestTy()->isPointerTy())
    dg.addEdge(&I, I.getOperand(0));
}

void
DepGraphBuilder::visitPHINode(PHINode &I) {
  for (unsigned i=0; i<I.getNumIncomingValues(); ++i) {
    dg.addEdge(I.getIncomingValue(i), &I);

    // Add a backedge if incoming value is a pointer.
    if (I.getIncomingValue(i)->getType()->isPointerTy())
      dg.addEdge(&I, I.getIncomingValue(i));
  }

  set<const Value *> *predicates = aSSA[&I];
  for (auto i = predicates->begin(), e = predicates->end(); i != e; ++i) {
    const Value *p = *i;
    dg.addEdge(p, &I);
  }
}

void
DepGraphBuilder::visitSelectInst(SelectInst &I) {
  dg.addEdge(I.getCondition(), &I);

  const Value *trueValue = I.getTrueValue();
  const Value *falseValue = I.getFalseValue();
  dg.addEdge(trueValue, &I);
  if (trueValue->getType()->isPointerTy())
    dg.addEdge(&I, trueValue);
  dg.addEdge(falseValue, &I);
  if (falseValue->getType()->isPointerTy())
    dg.addEdge(&I, falseValue);
}

void
DepGraphBuilder::visitGetElementPtrInst(GetElementPtrInst &I) {
  for (unsigned i=0; i<I.getNumOperands(); ++i)
    dg.addEdge(I.getOperand(i), &I);
  dg.addEdge(&I, I.getPointerOperand());
}

void
DepGraphBuilder::visitCmpInst(CmpInst &I) {
  dg.addEdge(I.getOperand(0), &I);
  dg.addEdge(I.getOperand(1), &I);
}

void
DepGraphBuilder::visitReturnInst(ReturnInst &I) {
  if (I.getReturnValue() == NULL)
    return;

  dg.addEdge(I.getReturnValue(), I.getParent()->getParent());

  // Add a backedge if it the value returned is a pointer.
  if (I.getReturnValue()->getType()->isPointerTy())
    dg.addEdge(I.getParent()->getParent(), I.getReturnValue());

}

void
DepGraphBuilder::visitBranchInst(BranchInst &I) {
  /* Do nothing. */
  return;
}

void
DepGraphBuilder::visitAllocaInst(AllocaInst &I) {
  /* Do nothing. */
  return;
}

void
DepGraphBuilder::visitUnreachableInst(UnreachableInst &I) {
  /* Do nothing. */
  return;
}

void
DepGraphBuilder::visitSwitchInst(llvm::SwitchInst &I) {
  /* Do nothing. */
  return;
}
