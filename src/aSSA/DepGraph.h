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

  // Phi elimination pass.
  // A ssa Phi function can be eliminated if its operands are equivalent.
  // In this case operands are merged into a single node and the phi is replaced
  // with this single node. The phi elimination pass allows us to break the
  // dependency with phi predicates when its operands are the same, e.g.:
  // if (rank)
  //   a = 0;
  // else
  //   a = 0;
  void phiElimination();

  void computeTaintedValues();
  void computeTaintedCalls();

  bool isTaintedCall(const llvm::CallInst *CI);
  bool isTaintedValue(const llvm::Value *v);
  void getTaintedCallConditions(const llvm::CallInst *call,
				std::set<const llvm::Value *> &conditions);
  void printTimers() const;

private:
  MemorySSA *mssa;

  const llvm::Function *curFunc;
  llvm::PostDominatorTree *curPDT;

  /* Graph nodes */

  // Map from a function to all its top-level variables nodes.
  llvm::DenseMap<const llvm::Function *, ValueSet> funcToLLVMNodesMap;
  // Map from a function to all its address taken ssa nodes.
  llvm::DenseMap<const llvm::Function *, VarSet> funcToSSANodesMap;
  std::set<const llvm::Function *> varArgNodes;

  /* Graph edges */

  // top-level to top-level edges
  llvm::DenseMap<const llvm::Value *, ValueSet> llvmToLLVMEdges;
  // top-level to address-taken ssa edges
  llvm::DenseMap<const llvm::Value *, VarSet> llvmToSSAEdges;
  // address-taken ssa to top-level edges
  llvm::DenseMap<MSSAVar *, ValueSet> ssaToLLVMEdges;
  // address-top ssa to address-taken ssa edges
  llvm::DenseMap<MSSAVar *, VarSet> ssaToSSAEdges;

  /* PDF+ call nodes and edges */

  // map from a function to all its call instructions
  llvm::DenseMap<const llvm::Function *, ValueSet> funcToCallNodes;
  // map from call instructions to called functions
  llvm::DenseMap<const llvm::Value *, const llvm::Function *> callToFuncEdges;
  // map from a condition to call instructions depending on that condition.
  llvm::DenseMap<const llvm::Value *, ValueSet> condToCallEdges;

  // map from a function to all the call sites calling this function.
  llvm::DenseMap<const llvm::Function *, ValueSet> funcToCallSites;
  // map from a callsite to all its conditions.
  llvm::DenseMap<const llvm::Value *, ValueSet> callsiteToConds;

  /* tainted nodes */
  ValueSet taintedLLVMNodes;
  ValueSet taintedCallNodes;
  std::set<const llvm::Function *> taintedFunctions;
  ConstVarSet taintedSSANodes;
  ConstVarSet ssaSources;

  // Two nodes are equivalent if they have exactly the same incoming and
  // outgoing edges and if none of them are phi nodes.
  bool areSSANodesEquivalent(MSSAVar *var1, MSSAVar *var2);

  // This function replaces phi with op1 and removes op2.
  void eliminatePhi(MSSAPhi *phi, MSSAVar *op1, MSSAVar *op2);

  void dotFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F);
  void dotExtFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F);
  std::string getNodeStyle(const llvm::Value *v);
  std::string getNodeStyle(const MSSAVar *v);
  std::string getNodeStyle(const llvm::Function *f);
  std::string getCallNodeStyle(const llvm::Value *v);

  /* stats */
  double buildGraphTime;
  double phiElimTime;
  double floodDepTime;
  double floodCallTime;
  double dotTime;
};

#endif /* DEPGRAPH_H */
