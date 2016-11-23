#include "DepGraphBuilder.h"

#include "llvm/Support/raw_ostream.h"

using namespace std;
using namespace llvm;

void
DepGraphBuilder::visitInstruction(Instruction &I) {
  errs() << "visit Instruction " << I << "\n";
}

void
DepGraphBuilder::visitLoadInst(LoadInst &I) {
  dg.addEdge(I.getPointerOperand(), &I);

  // Add backedge if the value loaded is a pointer of pointer.
  SequentialType *pointerTy =
    dyn_cast<SequentialType>(I.getPointerOperand()->getType());
  assert(pointerTy);
  if (pointerTy->getElementType()->isPointerTy())
    dg.addEdge(&I, I.getPointerOperand());
}

void
DepGraphBuilder::visitStoreInst(StoreInst &I) {
  dg.addEdge(I.getValueOperand(), I.getPointerOperand());

  // Add backedge if the value stored is a pointer.
  if (I.getValueOperand()->getType()->isPointerTy())
    dg.addEdge(I.getPointerOperand(), I.getValueOperand());

  // Add PDF+ predicates dependencies
  vector<BasicBlock *> IPDF =
    iterated_postdominance_frontier(PDT, I.getParent());

  for (unsigned n = 0; n < IPDF.size(); ++n) {
    const TerminatorInst *ti = IPDF[n]->getTerminator();
    assert(ti);
    const BranchInst *bi = dyn_cast<BranchInst>(ti);
    assert(bi);

    if (bi->isUnconditional())
      continue;

    const Value *cond = bi->getCondition();
    dg.addEdge(cond, I.getPointerOperand());
  }

  dg.addEdge(dg.getIPDFFuncNode(I.getParent()->getParent()),
	     I.getPointerOperand());
}

void
DepGraphBuilder::visitMemSetInst(MemSetInst &I) {
  assert(false);
}

void
DepGraphBuilder::visitMemCpyInst(MemCpyInst &I) {
  assert(false);
}

void
DepGraphBuilder::visitMemMoveInst(MemMoveInst &I) {
  assert(false);
}

void
DepGraphBuilder::visitMemTransferInst(MemTransferInst &I) {
  assert(false);
}

void
DepGraphBuilder::visitMemIntrinsic(MemIntrinsic &I) {
  assert(false);
}

void
DepGraphBuilder::visitCallInst(CallInst &I) {
  // F -> call
  const Function *called = I.getCalledFunction();
  dg.addEdge(I.getCalledFunction(), &I);

  // Add a backedge if the value returned by the function is a pointer.
  if (called->getReturnType()->isPointerTy())
    dg.addEdge(&I, I.getCalledFunction());

  // real arg -> formal arg
  if (called->isDeclaration()) {
    if (called->getName().equals("MPI_Comm_rank")) {
      dg.addEdge(called, I.getArgOperand(1));
      dg.addSource(I.getArgOperand(1));
    }
    return;
  }

  // Get call PDF+
  vector<BasicBlock *> IPDF =
    iterated_postdominance_frontier(PDT, I.getParent());

  for (unsigned n = 0; n < IPDF.size(); ++n) {
    const TerminatorInst *ti = IPDF[n]->getTerminator();
    assert(ti);
    const BranchInst *bi = dyn_cast<BranchInst>(ti);
    assert(bi);

    if (bi->isUnconditional())
      continue;

    const Value *cond = bi->getCondition();
    dg.addEdge(cond, dg.getIPDFFuncNode(called));
  }
  dg.addEdge(dg.getIPDFFuncNode(I.getParent()->getParent()),
	     dg.getIPDFFuncNode(called));


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
}

void
DepGraphBuilder::visitPHINode(PHINode &I) {
  for (unsigned i=0; i<I.getNumIncomingValues(); ++i) {
    dg.addEdge(I.getIncomingValue(i), &I);

    // Add backedge if incoming value is a pointer.
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

  // Add backedge if it the value returned is a pointer.
  if (I.getReturnValue()->getType()->isPointerTy())
    dg.addEdge(I.getParent()->getParent(), I.getReturnValue());

}
