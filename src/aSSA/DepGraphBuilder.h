#ifndef DEPGRAPHBUILDER_H
#define DEPGRAPHBUILDER_H

#include "ASSA.h"
#include "DepGraph.h"
#include "Utils.h"

#include "llvm/IR/InstVisitor.h"

class DepGraphBuilder : public llvm::InstVisitor<DepGraphBuilder> {
public: DepGraphBuilder(llvm::PostDominatorTree &PDT, ASSA &aSSA, DepGraph &dg)
  : PDT(PDT), aSSA(aSSA), dg(dg) {}

  void visitInstruction(llvm::Instruction &I);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitMemSetInst(llvm::MemSetInst &I);
  void visitMemCpyInst(llvm::MemCpyInst &I);
  void visitMemMoveInst(llvm::MemMoveInst &I);
  void visitMemTransferInst(llvm::MemTransferInst &I);
  void visitMemIntrinsic(llvm::MemIntrinsic &I);
  void visitCallInst(llvm::CallInst &I);
  void visitBinaryOperator(llvm::BinaryOperator &I);
  void visitCastInst(llvm::CastInst &I);
  void visitPHINode(llvm::PHINode &I);
  void visitGetElementPtrInst(llvm::GetElementPtrInst &I);
  void visitCmpInst(llvm::CmpInst &I);
  void visitReturnInst(llvm::ReturnInst &I);

 private:
  llvm::PostDominatorTree &PDT;
  ASSA &aSSA;
  DepGraph &dg;
};

#endif /* DEPGRAPHBUILDER_H */
