#ifndef POINTGRAPHBUILDER_H
#define POINTGRAPHBUILDER_H

#include "ASSA.h"
#include "DepGraph.h"
#include "Utils.h"

#include "llvm/IR/InstVisitor.h"

class PointGraphBuilder : public llvm::InstVisitor<PointGraphBuilder> {
public: PointGraphBuilder(DepGraph &pg) : pg(pg) {}

  void visitFunction(llvm::Function &F);
  void visitInstruction(llvm::Instruction &I);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitMemSetInst(llvm::MemSetInst &I);
  void visitMemTransferInst(llvm::MemTransferInst &I);
  void visitCallInst(llvm::CallInst &I);
  void visitBinaryOperator(llvm::BinaryOperator &I);
  void visitCastInst(llvm::CastInst &I);
  void visitPHINode(llvm::PHINode &I);
  void visitSelectInst(llvm::SelectInst &I);
  void visitGetElementPtrInst(llvm::GetElementPtrInst &I);
  void visitCmpInst(llvm::CmpInst &I);
  void visitReturnInst(llvm::ReturnInst &I);
  void visitBranchInst(llvm::BranchInst &I);
  void visitAllocaInst(llvm::AllocaInst &I);
  void visitUnreachableInst(llvm::UnreachableInst &I);
  void visitSwitchInst(llvm::SwitchInst &I);

 private:
  DepGraph &pg;
};

#endif /* POINTGRAPHBUILDER_H */
