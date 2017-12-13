#ifndef DEPGRAPHUIDA_H
#define DEPGRAPHUIDA_H

#include "DepGraph.h"
#include "PTACallGraph.h"

#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

class DepGraphUIDA : public llvm::InstVisitor<DepGraphUIDA>, public DepGraph {
public:
  typedef std::set<const llvm::Value *> ValueSet;

  DepGraphUIDA(PTACallGraph *CG, llvm::Pass *pass);
  virtual ~DepGraphUIDA();

  void buildFunction(const llvm::Function *F);
  void toDot(std::string filename);
  void dotTaintPath(const llvm::Value *v, std::string filename,
		    const llvm::Instruction *collective);


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
  void visitExtractValueInst(llvm::ExtractValueInst &I);
  void visitExtractElementInst(llvm::ExtractElementInst &I);
  void visitInsertElementInst(llvm::InsertElementInst &I);
  void visitInsertValueInst(llvm::InsertValueInst &I);
  void visitShuffleVectorInst(llvm::ShuffleVectorInst &I);
  void visitInstruction(llvm::Instruction &I);

  void computeTaintedValuesContextInsensitive();
  void computeTaintedValuesContextSensitive();
  void computeTaintedValuesCSForEntry(PTACallGraphNode *entry);
  bool isTaintedValue(const llvm::Value *v);

  void getCallInterIPDF(const llvm::CallInst *call,
			       std::set<const llvm::BasicBlock *> &ipdf);
  // EMMA
  void getCallIntraIPDF(const llvm::CallInst *call,
			       std::set<const llvm::BasicBlock *> &ipdf);

  void printTimers() const;

private:
  PTACallGraph *CG;
  llvm::Pass *pass;

  const llvm::Function *curFunc;
  llvm::PostDominatorTree *curPDT;

  /* PHI predicates */
  std::map<const llvm::PHINode *, ValueSet> llvmPhiToPredMap;

  /* Graph nodes */

  // Map from a function to all its top-level variables nodes.
  std::map<const llvm::Function *, ValueSet> funcToLLVMNodesMap;
  std::set<const llvm::Function *> varArgNodes;

  /* Graph edges */

  // top-level to top-level edges
  std::map<const llvm::Value *, ValueSet> llvmToLLVMChildren;
  std::map<const llvm::Value *, ValueSet> llvmToLLVMParents;

  void enableMPI();
  void enableOMP();
  void enableUPC();
  void enableCUDA();

  void addEdge(const llvm::Value *s, const llvm::Value *d);
  void removeEdge(const llvm::Value *s, const llvm::Value *d);

  /* PDF+ call nodes and edges */

  // map from a function to all its call instructions
  std::map<const llvm::Function *, ValueSet> funcToCallNodes;
  // map from call instructions to called functions
  std::map<const llvm::Value *, const llvm::Function *> callToFuncEdges;
  // map from a condition to call instructions depending on that condition.
  std::map<const llvm::Value *, ValueSet> condToCallEdges;

  // map from a function to all the call sites calling this function.
  std::map<const llvm::Function *, ValueSet> funcToCallSites;
  // map from a callsite to all its conditions.
  std::map<const llvm::Value *, ValueSet> callsiteToConds;

  /* tainted nodes */
  ValueSet taintedLLVMNodes;
  ValueSet valueSources;

  void floodFunction(const llvm::Function *F);
  void floodFunctionFromFunction(const llvm::Function *to,
				 const llvm::Function *from);
  void resetFunctionTaint(const llvm::Function *F);
  void computeFunctionCSTaintedConds(const llvm::Function *F);
  ValueSet taintedConditions;


  /* Graph construction for call sites*/
  void connectCSEffectiveParameters(llvm::CallInst &I);
  void connectCSEffectiveParametersExt(llvm::CallInst &I,
				       const llvm::Function *callee);
  void connectCSCalledReturnValue(llvm::CallInst &I);

  void dotFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F);
  void dotExtFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F);
  std::string getNodeStyle(const llvm::Value *v);
  std::string getNodeStyle(const llvm::Function *f);
  std::string getCallNodeStyle(const llvm::Value *v);

  struct DGDebugLoc {
    const llvm::Function *F;
    std::string filename;
    int line;

    bool operator < (const DGDebugLoc &o) const {
      return line < o.line;
    }
  };

  bool getDGDebugLoc(const llvm::Value *v, DGDebugLoc &DL);
  std::string getStringMsg(const llvm::Value *v);
  bool getDebugTrace(std::vector<DGDebugLoc> &DLs, std::string &trace,
		     const llvm::Instruction *collective);
  void reorderAndRemoveDup(std::vector<DGDebugLoc> &DLs);

  /* stats */
  double buildGraphTime;
  double phiElimTime;
  double floodDepTime;
  double floodCallTime;
  double dotTime;
};

#endif /* DEPGRAPHUIDA_H */
