#ifndef PTACALLGRAPH_H
#define PTACALLGRAPH_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Passes/PassBuilder.h"

#include <map>
#include <set>

namespace llvm {
class CallBase;
class Function;
class Module;
} // namespace llvm
class PTACallGraphNode;
class Andersen;

class PTACallGraphNode {
public:
  using CallRecord = std::pair<llvm::CallBase const *, PTACallGraphNode *>;

  using CalledFunctionsVector = std::vector<CallRecord>;

  inline PTACallGraphNode(llvm::Function *F) : F(F) {}

  ~PTACallGraphNode() {}

  using iterator = std::vector<CallRecord>::iterator;
  using const_iterator = std::vector<CallRecord>::const_iterator;

  /// \brief Returns the function that this call graph node represents.
  llvm::Function *getFunction() const { return F; }

  inline const_iterator begin() const { return CalledFunctions.begin(); }
  inline const_iterator end() const { return CalledFunctions.end(); }

  /// \brief Adds a function to the list of functions called by this one.
  void addCalledFunction(llvm::CallBase const *CB, PTACallGraphNode *M);

private:
  friend class PTACallGraph;

  llvm::AssertingVH<llvm::Function> F;

  std::vector<CallRecord> CalledFunctions;
};

class PTACallGraph {
public:
  using FunctionMapTy =
      std::map<const llvm::Function *, std::unique_ptr<PTACallGraphNode>>;
  using iterator = FunctionMapTy::iterator;
  using const_iterator = FunctionMapTy::const_iterator;

private:
  llvm::Module const &M;
  Andersen const &AA;

  /// \brief A map from \c Function* to \c CallGraphNode*.
  FunctionMapTy FunctionMap;

  // Must be main
  PTACallGraphNode *Root;

  PTACallGraphNode *ProgEntry;

  std::set<const llvm::Function *> reachableFunctions;

  /// \brief This node has edges to all external functions and those internal
  /// functions that have their address taken.
  PTACallGraphNode *ExternalCallingNode;

  /// \brief This node has edges to it from all functions making indirect calls
  /// or calling an external function.
  std::unique_ptr<PTACallGraphNode> CallsExternalNode;

  /// \brief Add a function to the call graph, and link the node to all of the
  /// functions that it calls.
  void addToCallGraph(llvm::Function const &F);

  llvm::ValueMap<const llvm::Instruction *, std::set<const llvm::Function *>>
      indirectCallMap;

  inline iterator begin() { return FunctionMap.begin(); }
  inline iterator end() { return FunctionMap.end(); }

  PTACallGraphNode *getOrInsertFunction(const llvm::Function *F);

public:
  explicit PTACallGraph(llvm::Module const &M, Andersen const &AA);
  ~PTACallGraph();

  PTACallGraphNode *getEntry() const { return Root; }

  /// \brief Returns the module the call graph corresponds to.
  llvm::Module const &getModule() const { return M; }

  inline const_iterator begin() const { return FunctionMap.begin(); }
  inline const_iterator end() const { return FunctionMap.end(); }

  /// \brief Returns the call graph node for the provided function.
  inline const PTACallGraphNode *operator[](const llvm::Function *F) const {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second.get();
  }

  /// \brief Returns the \c CallGraphNode which is used to represent
  /// undetermined calls into the callgraph.
  PTACallGraphNode *getExternalCallingNode() const {
    return ExternalCallingNode;
  }

  PTACallGraphNode const *getCallsExternalNode() const {
    return CallsExternalNode.get();
  }

  bool isReachableFromEntry(const llvm::Function &F) const;
  auto const &getIndirectCallMap() const { return indirectCallMap; }
};

class PTACallGraphAnalysis
    : public llvm::AnalysisInfoMixin<PTACallGraphAnalysis> {
  friend llvm::AnalysisInfoMixin<PTACallGraphAnalysis>;
  static llvm::AnalysisKey Key;

public:
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<PTACallGraph>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

//===----------------------------------------------------------------------===//
// GraphTraits specializations for call graphs so that they can be treated as
// graphs by the generic graph algorithms.
//

// Provide graph traits for tranversing call graphs using standard graph
// traversals.

namespace llvm {

template <> struct GraphTraits<const PTACallGraphNode *> {
  typedef const PTACallGraphNode NodeType;
  typedef const PTACallGraphNode *NodeRef;

  typedef PTACallGraphNode::CallRecord CGNPairTy;
  typedef std::pointer_to_unary_function<CGNPairTy, const PTACallGraphNode *>
      CGNDerefFun;

  static NodeType *getEntryNode(const PTACallGraphNode *CGN) { return CGN; }

  typedef mapped_iterator<NodeType::const_iterator, CGNDerefFun>
      ChildIteratorType;

  static inline ChildIteratorType child_begin(NodeType *N) {
    return map_iterator(N->begin(), CGNDerefFun(CGNDeref));
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return map_iterator(N->end(), CGNDerefFun(CGNDeref));
  }

  static const PTACallGraphNode *CGNDeref(CGNPairTy P) { return P.second; }
};

template <>
struct GraphTraits<const PTACallGraph *>
    : public GraphTraits<const PTACallGraphNode *> {
  static NodeType *getEntryNode(const PTACallGraph *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }
  typedef std::pair<const Function *const, std::unique_ptr<PTACallGraphNode>>
      PairTy;
  typedef std::pointer_to_unary_function<const PairTy &,
                                         const PTACallGraphNode &>
      DerefFun;

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<PTACallGraph::const_iterator, DerefFun>
      nodes_iterator;
  static nodes_iterator nodes_begin(const PTACallGraph *CG) {
    return map_iterator(CG->begin(), DerefFun(CGdereference));
  }
  static nodes_iterator nodes_end(const PTACallGraph *CG) {
    return map_iterator(CG->end(), DerefFun(CGdereference));
  }

  static const PTACallGraphNode &CGdereference(const PairTy &P) {
    return *P.second;
  }
};
} // namespace llvm

#endif /* PTACALLGRAPH_H */
