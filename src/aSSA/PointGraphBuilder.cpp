#include "PointGraphBuilder.h"

#include "llvm/Support/raw_ostream.h"

using namespace std;
using namespace llvm;

void

PointGraphBuilder::visitFunction(Function &F) {
  if (F.isDeclaration())
    return;

  pg.addFunction(&F);
}

void
PointGraphBuilder::visitInstruction(Instruction &I) {
  errs() << "Error: instruction " << I << " not implemented yet !\n";
  exit(EXIT_FAILURE);
}

void
PointGraphBuilder::visitLoadInst(LoadInst &I) {
  SequentialType *pointerTy =
    dyn_cast<SequentialType>(I.getPointerOperand()->getType());
  assert(pointerTy);
  if (pointerTy->getElementType()->isPointerTy())
    pg.addEdge(&I, I.getPointerOperand());
}

void
PointGraphBuilder::visitStoreInst(StoreInst &I) {
  if (I.getValueOperand()->getType()->isPointerTy())
    pg.addEdge(I.getPointerOperand(), I.getValueOperand());
}

void
PointGraphBuilder::visitMemSetInst(MemSetInst &I) {
}

void
PointGraphBuilder::visitMemTransferInst(MemTransferInst &I) {
}

void
PointGraphBuilder::visitCallInst(CallInst &I) {
}

void
PointGraphBuilder::visitBinaryOperator(BinaryOperator &I) {
}

void
PointGraphBuilder::visitCastInst(CastInst &I){
  // Add a backedge if casted value is a pointer.
  if (I.getSrcTy()->isPointerTy() || I.getDestTy()->isPointerTy())
    pg.addEdge(&I, I.getOperand(0));
}

void
PointGraphBuilder::visitPHINode(PHINode &I) {
  for (unsigned i=0; i<I.getNumIncomingValues(); ++i) {
    // Add a backedge if incoming value is a pointer.
    if (I.getIncomingValue(i)->getType()->isPointerTy())
      pg.addEdge(&I, I.getIncomingValue(i));
  }
}

void
PointGraphBuilder::visitSelectInst(SelectInst &I) {
  pg.addEdge(I.getCondition(), &I);

  const Value *trueValue = I.getTrueValue();
  const Value *falseValue = I.getFalseValue();
  if (trueValue->getType()->isPointerTy())
    pg.addEdge(&I, trueValue);
  if (falseValue->getType()->isPointerTy())
    pg.addEdge(&I, falseValue);
}

void
PointGraphBuilder::visitGetElementPtrInst(GetElementPtrInst &I) {
  pg.addEdge(&I, I.getPointerOperand());
}

void
PointGraphBuilder::visitCmpInst(CmpInst &I) {
}

void
PointGraphBuilder::visitReturnInst(ReturnInst &I) {
}

void
PointGraphBuilder::visitBranchInst(BranchInst &I) {
  /* Do nothing. */
  return;
}

void
PointGraphBuilder::visitAllocaInst(AllocaInst &I) {
  /* Do nothing. */
  return;
}

void
PointGraphBuilder::visitUnreachableInst(UnreachableInst &I) {
  /* Do nothing. */
  return;
}

void
PointGraphBuilder::visitSwitchInst(llvm::SwitchInst &I) {
  /* Do nothing. */
  return;
}
