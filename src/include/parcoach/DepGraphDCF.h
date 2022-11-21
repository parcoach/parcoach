#pragma once

#include "parcoach/MemorySSA.h"

#include "llvm/IR/InstVisitor.h"

#include <optional>

class PTACallGraphNode;
namespace llvm {
class raw_fd_ostream;
}

namespace parcoach {

class DepGraphDCF : public llvm::InstVisitor<DepGraphDCF> {
public:
  using VarSet = std::set<MSSAVar *>;
  using ConstVarSet = std::set<const MSSAVar *>;
  using ValueSet = std::set<const llvm::Value *>;

  DepGraphDCF(parcoach::MemorySSA *mssa, PTACallGraph const &CG,
              llvm::FunctionAnalysisManager &AM, llvm::Module &M,
              bool noPtrDep = false, bool noPred = false,
              bool disablePhiElim = false);
  virtual ~DepGraphDCF();

  void toDot(llvm::StringRef filename) const;
  void dotTaintPath(const llvm::Value *v, llvm::StringRef filename,
                    const llvm::Instruction *collective) const;

  // FIXME: ideally we would have two classes: one analysis result, and one
  // visitor constructing this analysis result (so that the user can't "visit"
  // stuff manually and change the analysis' result).
  void visitBasicBlock(llvm::BasicBlock &BB);
  void visitAllocaInst(llvm::AllocaInst &I);
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
  void visitTerminator(llvm::Instruction &I);
  void visitInstruction(llvm::Instruction &I);

  bool isTaintedValue(const llvm::Value *v) const;

  void getCallInterIPDF(const llvm::CallInst *call,
                        std::set<const llvm::BasicBlock *> &ipdf) const;

private:
  void build();
  void buildFunction(const llvm::Function *F);
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

  void computeTaintedValuesContextInsensitive();
  void computeTaintedValuesContextSensitive();
  void computeTaintedValuesCSForEntry(PTACallGraphNode const *entry);

  parcoach::MemorySSA *mssa;
  PTACallGraph const &CG;

  const llvm::Function *curFunc;
  llvm::FunctionAnalysisManager &FAM;
  llvm::Module const &M;
  llvm::PostDominatorTree *PDT;

  /* Graph nodes */

  // Map from a function to all its top-level variables nodes.
  llvm::ValueMap<const llvm::Function *, ValueSet> funcToLLVMNodesMap;
  // Map from a function to all its address taken ssa nodes.
  llvm::ValueMap<const llvm::Function *, VarSet> funcToSSANodesMap;
  std::set<const llvm::Function *> varArgNodes;

  /* Graph edges */

  // top-level to top-level edges
  llvm::ValueMap<const llvm::Value *, ValueSet> llvmToLLVMChildren;
  llvm::ValueMap<const llvm::Value *, ValueSet> llvmToLLVMParents;

  // top-level to address-taken ssa edges
  llvm::ValueMap<const llvm::Value *, VarSet> llvmToSSAChildren;
  llvm::ValueMap<const llvm::Value *, VarSet> llvmToSSAParents;
  // address-taken ssa to top-level edges
  llvm::DenseMap<MSSAVar *, ValueSet> ssaToLLVMChildren;
  llvm::DenseMap<MSSAVar *, ValueSet> ssaToLLVMParents;

  // address-top ssa to address-taken ssa edges
  llvm::DenseMap<MSSAVar *, VarSet> ssaToSSAChildren;
  llvm::DenseMap<MSSAVar *, VarSet> ssaToSSAParents;

  void enableMPI();
  void enableOMP();
  void enableUPC();
  void enableCUDA();

  void addEdge(const llvm::Value *s, const llvm::Value *d);
  void addEdge(const llvm::Value *s, MSSAVar *d);
  void addEdge(MSSAVar *s, const llvm::Value *d);
  void addEdge(MSSAVar *s, MSSAVar *d);
  void removeEdge(const llvm::Value *s, const llvm::Value *d);
  void removeEdge(const llvm::Value *s, MSSAVar *d);
  void removeEdge(MSSAVar *s, const llvm::Value *d);
  void removeEdge(MSSAVar *s, MSSAVar *d);

  /* PDF+ call nodes and edges */

  // map from a function to all its call instructions
  llvm::ValueMap<const llvm::Function *, ValueSet> funcToCallNodes;
  // map from call instructions to called functions
  llvm::ValueMap<const llvm::Value *, const llvm::Function *> callToFuncEdges;
  // map from a condition to call instructions depending on that condition.
  llvm::ValueMap<const llvm::Value *, ValueSet> condToCallEdges;

  // map from a function to all the call sites calling this function.
  llvm::ValueMap<const llvm::Function *, ValueSet> funcToCallSites;
  // map from a callsite to all its conditions.
  llvm::ValueMap<const llvm::Value *, ValueSet> callsiteToConds;

  /* tainted nodes */
  ValueSet taintedLLVMNodes;
  ConstVarSet taintedSSANodes;

  ConstVarSet taintResetSSANodes;
  ConstVarSet ssaSources;
  ValueSet valueSources;

  void floodFunction(const llvm::Function *F);
  void floodFunctionFromFunction(const llvm::Function *to,
                                 const llvm::Function *from);
  void resetFunctionTaint(const llvm::Function *F);
  void computeFunctionCSTaintedConds(const llvm::Function *F);
  ValueSet taintedConditions;

  /* Graph construction for call sites*/
  void connectCSMus(llvm::CallInst &I);
  void connectCSChis(llvm::CallInst &I);
  void connectCSEffectiveParameters(llvm::CallInst &I);
  void connectCSEffectiveParametersExt(llvm::CallInst &I,
                                       const llvm::Function *callee);
  void connectCSCalledReturnValue(llvm::CallInst &I);
  void connectCSRetChi(llvm::CallInst &I);

  // Two nodes are equivalent if they have exactly the same incoming and
  // outgoing edges and if none of them are phi nodes.
  bool areSSANodesEquivalent(MSSAVar *var1, MSSAVar *var2);

  // This function replaces phi with op1 and removes op2.
  void eliminatePhi(MSSAPhi *phi, std::vector<MSSAVar *> ops);

  void dotFunction(llvm::raw_fd_ostream &stream, const llvm::Function *F) const;
  void dotExtFunction(llvm::raw_fd_ostream &stream,
                      const llvm::Function *F) const;
  std::string getNodeStyle(const llvm::Value *v) const;
  std::string getNodeStyle(const MSSAVar *v) const;
  std::string getNodeStyle(const llvm::Function *f) const;
  std::string getCallNodeStyle(const llvm::Value *v) const;

  struct DGDebugLoc {
    const llvm::Function *F;
    std::string filename;
    int line;

    bool operator<(const DGDebugLoc &o) const { return line < o.line; }
  };

  bool getDGDebugLoc(const llvm::Value *v, DGDebugLoc &DL) const;
  bool getDGDebugLoc(MSSAVar *v, DGDebugLoc &DL) const;
  std::string getStringMsg(const llvm::Value *v) const;
  std::string getStringMsg(MSSAVar *v) const;
  bool getDebugTrace(std::vector<DGDebugLoc> &DLs, std::string &trace,
                     const llvm::Instruction *collective) const;
  void reorderAndRemoveDup(std::vector<DGDebugLoc> &DLs) const;

  /* stats */
  double buildGraphTime;
  double phiElimTime;
  double floodDepTime;

  /* options */
  bool noPtrDep;
  bool noPred;
  bool disablePhiElim;
};

class DepGraphDCFAnalysis
    : public llvm::AnalysisInfoMixin<DepGraphDCFAnalysis> {
  friend llvm::AnalysisInfoMixin<DepGraphDCFAnalysis>;
  static llvm::AnalysisKey Key;

public:
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<DepGraphDCF>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
} // namespace parcoach
