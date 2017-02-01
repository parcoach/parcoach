#ifndef DEPGRAPH_H
#define DEPGRAPH_H

#include "MSSAMuChi.h"
#include "MemorySSA.h"

#include "llvm/IR/InstVisitor.h"
#include "llvm/Support/raw_ostream.h"

class DepGraph : public llvm::InstVisitor<DepGraph> {
public:
  typedef std::set<MSSAVar *> VarSet;
  typedef std::set<const MSSAVar *> ConstVarSet;
  typedef std::set<const llvm::Value *> ValueSet;

  DepGraph(MemorySSA *mssa);
  virtual ~DepGraph();

  void buildFunction(const llvm::Function *F, llvm::PostDominatorTree *PDT);
  void toDot(std::string filename);

  void visitBasicBlock(llvm::BasicBlock &BB);
  void visitAllocaInst(llvm::AllocaInst &I);
  void visitTerminatorInst(llvm::TerminatorInst &I);
  void visitCmpInst(llvm::CmpInst &I);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitGetElementPtrInst(llvm::GetElementPtrInst &I);
  void visitPHINode(llvm::PHINode &I);
  void visitCastInst(llvm::CastInst &I);
  void visitSelectInst(llvm::SelectInst &I);
  void visitBinaryOperator(llvm::BinaryOperator &I);
  void visitCallInst(llvm::CallInst &I);
  void visitInstruction(llvm::Instruction &I);

private:
  MemorySSA *mssa;

  const llvm::Function *curFunc;
  llvm::PostDominatorTree *curPDT;

  // SSA
  llvm::DenseMap<const llvm::Function *, ValueSet> funcToLLVMNodesMap;
  llvm::DenseMap<const llvm::Function *, VarSet> funcToSSANodesMap;
  llvm::DenseMap<const llvm::Value *, ValueSet> llvmToLLVMEdges;
  llvm::DenseMap<const llvm::Value *, VarSet> llvmToSSAEdges;
  llvm::DenseMap<MSSAVar *, ValueSet> ssaToLLVMEdges;
  llvm::DenseMap<MSSAVar *, VarSet> ssaToSSAEdges;

  // PDF+ call
  std::set<const llvm::Function *> funcNodes;
  llvm::DenseMap<const llvm::Function *, ValueSet> funcToCallNodes;
  // Calls inside a function
  llvm::DenseMap<const llvm::Value *, const llvm::Function *> callToFuncEdges;
  llvm::DenseMap<const llvm::Value *, ValueSet> condToCallEdges;

  ValueSet taintedLLVMNodes;
  ValueSet taintedCallNodes;
  std::set<const llvm::Function *> taintedFunctions;
  ConstVarSet taintedSSANodes;
  ConstVarSet ssaSources;

  void computeTaintedValues();
  void computeTaintedValuesRec(const llvm::Value *v);
  void computeTaintedValuesRec(MSSAVar *v);
  // void computeTaintedValuesRec(const llvm::Function *F);

  void computeTaintedCalls();
  void computeTaintedCalls(const llvm::Value *v);
  void computeTaintedCalls(const llvm::Function *F);

  void dotFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F);
  void dotExtFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F);
  std::string getNodeStyle(const llvm::Value *v);
  std::string getNodeStyle(const MSSAVar *v);
  std::string getNodeStyle(const llvm::Function *f);
  std::string getCallNodeStyle(const llvm::Value *v);
};

#endif /* DEPGRAPH_H */
