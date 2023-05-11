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
  using ConstVarSet = std::set<MSSAVar const *>;
  using ValueSet = std::set<llvm::Value const *>;

  DepGraphDCF(parcoach::MemorySSA *mssa, PTACallGraph const &CG,
              llvm::FunctionAnalysisManager &AM, llvm::Module &M,
              bool ContextInsensitive, bool noPtrDep = false,
              bool noPred = false, bool disablePhiElim = false);
  virtual ~DepGraphDCF();

  void toDot(llvm::StringRef filename) const;
  void dotTaintPath(llvm::Value const *v, llvm::StringRef filename,
                    llvm::Instruction const *collective) const;

  // FIXME: ideally we would have two classes: one analysis result, and one
  // visitor constructing this analysis result (so that the user can't "visit"
  // stuff manually and change the analysis' result).
  void visitBasicBlock(llvm::BasicBlock &BB);
  void visitAllocaInst(llvm::AllocaInst &I);
  void visitCmpInst(llvm::CmpInst &I);
  void visitFreezeInst(llvm::FreezeInst &I);
  void visitUnaryOperator(llvm::UnaryOperator &I);
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
  static void visitInstruction(llvm::Instruction &I);

  bool isTaintedValue(llvm::Value const *v) const;

  void getCallInterIPDF(llvm::CallInst const *call,
                        std::set<llvm::BasicBlock const *> &ipdf) const;

private:
  void build();
  void buildFunction(llvm::Function const *F);
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

  llvm::Function const *curFunc;
  llvm::FunctionAnalysisManager &FAM;
  llvm::Module const &M;
  bool const ContextInsensitive;
  llvm::PostDominatorTree *PDT;

  /* Graph nodes */

  // Map from a function to all its top-level variables nodes.
  llvm::ValueMap<llvm::Function const *, ValueSet> funcToLLVMNodesMap;
  // Map from a function to all its address taken ssa nodes.
  llvm::ValueMap<llvm::Function const *, VarSet> funcToSSANodesMap;
  std::set<llvm::Function const *> varArgNodes;

  /* Graph edges */

  // top-level to top-level edges
  llvm::ValueMap<llvm::Value const *, ValueSet> llvmToLLVMChildren;
  llvm::ValueMap<llvm::Value const *, ValueSet> llvmToLLVMParents;

  // top-level to address-taken ssa edges
  llvm::ValueMap<llvm::Value const *, VarSet> llvmToSSAChildren;
  llvm::ValueMap<llvm::Value const *, VarSet> llvmToSSAParents;
  // address-taken ssa to top-level edges
  llvm::DenseMap<MSSAVar *, ValueSet> ssaToLLVMChildren;
  llvm::DenseMap<MSSAVar *, ValueSet> ssaToLLVMParents;

  // address-top ssa to address-taken ssa edges
  llvm::DenseMap<MSSAVar *, VarSet> ssaToSSAChildren;
  llvm::DenseMap<MSSAVar *, VarSet> ssaToSSAParents;

  static void enableMPI();
  static void enableOMP();
  static void enableUPC();
  static void enableCUDA();

  void addEdge(llvm::Value const *s, llvm::Value const *d);
  void addEdge(llvm::Value const *s, MSSAVar *d);
  void addEdge(MSSAVar *s, llvm::Value const *d);
  void addEdge(MSSAVar *s, MSSAVar *d);
  void removeEdge(llvm::Value const *s, llvm::Value const *d);
  void removeEdge(llvm::Value const *s, MSSAVar *d);
  void removeEdge(MSSAVar *s, llvm::Value const *d);
  void removeEdge(MSSAVar *s, MSSAVar *d);

  /* PDF+ call nodes and edges */

  // map from a function to all its call instructions
  llvm::ValueMap<llvm::Function const *, ValueSet> funcToCallNodes;
  // map from call instructions to called functions
  llvm::ValueMap<llvm::Value const *, llvm::Function const *> callToFuncEdges;
  // map from a condition to call instructions depending on that condition.
  llvm::ValueMap<llvm::Value const *, ValueSet> condToCallEdges;

  // map from a function to all the call sites calling this function.
  llvm::ValueMap<llvm::Function const *, ValueSet> funcToCallSites;
  // map from a callsite to all its conditions.
  llvm::ValueMap<llvm::Value const *, ValueSet> callsiteToConds;

  /* tainted nodes */
  ValueSet taintedLLVMNodes;
  ConstVarSet taintedSSANodes;

  ConstVarSet taintResetSSANodes;
  ConstVarSet ssaSources;
  ValueSet valueSources;

  void floodFunction(llvm::Function const *F);
  void floodFunctionFromFunction(llvm::Function const *to,
                                 llvm::Function const *from);
  void resetFunctionTaint(llvm::Function const *F);
  void computeFunctionCSTaintedConds(llvm::Function const *F);
  ValueSet taintedConditions;

  /* Graph construction for call sites*/
  void connectCSMus(llvm::CallInst &I);
  void connectCSChis(llvm::CallInst &I);
  void connectCSEffectiveParameters(llvm::CallInst &I);
  void connectCSEffectiveParametersExt(llvm::CallInst &I,
                                       llvm::Function const *callee);
  void connectCSCalledReturnValue(llvm::CallInst &I);
  void connectCSRetChi(llvm::CallInst &I);

  // Two nodes are equivalent if they have exactly the same incoming and
  // outgoing edges and if none of them are phi nodes.
  bool areSSANodesEquivalent(MSSAVar *var1, MSSAVar *var2);

  // This function replaces phi with op1 and removes op2.
  void eliminatePhi(MSSAPhi *phi, std::vector<MSSAVar *> ops);

  void dotFunction(llvm::raw_fd_ostream &stream, llvm::Function const *F) const;
  void dotExtFunction(llvm::raw_fd_ostream &stream,
                      llvm::Function const *F) const;
  std::string getNodeStyle(llvm::Value const *v) const;
  std::string getNodeStyle(MSSAVar const *v) const;
  static std::string getNodeStyle(llvm::Function const *f);
  static std::string getCallNodeStyle(llvm::Value const *v);

  struct DGDebugLoc {
    llvm::Function const *F;
    std::string filename;
    int line;

    bool operator<(DGDebugLoc const &o) const { return line < o.line; }
  };

  static bool getDGDebugLoc(llvm::Value const *v, DGDebugLoc &DL);
  static bool getDGDebugLoc(MSSAVar *v, DGDebugLoc &DL);
  static std::string getStringMsg(llvm::Value const *v);
  static std::string getStringMsg(MSSAVar *v);
  static bool getDebugTrace(std::vector<DGDebugLoc> &DLs, std::string &trace,
                            llvm::Instruction const *collective);
  static void reorderAndRemoveDup(std::vector<DGDebugLoc> &DLs);

  /* options */
  bool noPtrDep;
  bool noPred;
  bool disablePhiElim;
};

class DepGraphDCFAnalysis
    : public llvm::AnalysisInfoMixin<DepGraphDCFAnalysis> {
  friend llvm::AnalysisInfoMixin<DepGraphDCFAnalysis>;
  static llvm::AnalysisKey Key;
  bool ContextInsensitive_;

public:
  DepGraphDCFAnalysis(bool ContextInsensitive)
      : ContextInsensitive_(ContextInsensitive){};
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<DepGraphDCF>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};
} // namespace parcoach
