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
      std::map<llvm::Function const *, std::unique_ptr<PTACallGraphNode>>;
  using iterator = FunctionMapTy::iterator;
  using const_iterator = FunctionMapTy::const_iterator;

private:
  Andersen const &AA;

  /// \brief A map from \c Function* to \c CallGraphNode*.
  FunctionMapTy FunctionMap;

  // Must be main
  PTACallGraphNode *Root;

  PTACallGraphNode *ProgEntry;

  std::set<llvm::Function const *> reachableFunctions;

  /// \brief This node has edges to all external functions and those internal
  /// functions that have their address taken.
  PTACallGraphNode *ExternalCallingNode;

  /// \brief This node has edges to it from all functions making indirect calls
  /// or calling an external function.
  std::unique_ptr<PTACallGraphNode> CallsExternalNode;

  /// \brief Add a function to the call graph, and link the node to all of the
  /// functions that it calls.
  void addToCallGraph(llvm::Function const &F);

  llvm::ValueMap<llvm::Instruction const *, std::set<llvm::Function const *>>
      indirectCallMap;

  PTACallGraphNode *getOrInsertFunction(llvm::Function const *F);

public:
  explicit PTACallGraph(llvm::Module const &M, Andersen const &AA);
  ~PTACallGraph();

  PTACallGraphNode *getEntry() const { return Root; }

  /// \brief Returns the \c CallGraphNode which is used to represent
  /// undetermined calls into the callgraph.
  PTACallGraphNode *getExternalCallingNode() const {
    return ExternalCallingNode;
  }

  bool isReachableFromEntry(llvm::Function const &F) const;
  auto const &getIndirectCallMap() const { return indirectCallMap; }
};

class PTACallGraphAnalysis
    : public llvm::AnalysisInfoMixin<PTACallGraphAnalysis> {
  friend llvm::AnalysisInfoMixin<PTACallGraphAnalysis>;
  static llvm::AnalysisKey Key;

public:
  // We return a unique_ptr to ensure stability of the analysis' internal state.
  using Result = std::unique_ptr<PTACallGraph>;
  static Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

//===----------------------------------------------------------------------===//
// GraphTraits specializations for call graphs so that they can be treated as
// graphs by the generic graph algorithms.
//

// Provide graph traits for tranversing call graphs using standard graph
// traversals.

namespace llvm {

template <> struct GraphTraits<PTACallGraphNode const *> {
  using NodeType = const PTACallGraphNode;
  using NodeRef = NodeType *;

  using CGNPairTy = PTACallGraphNode::CallRecord;

  static NodeRef CGNDeref(CGNPairTy P) { return P.second; }

  typedef mapped_iterator<NodeType::const_iterator, decltype(&CGNDeref)>
      ChildIteratorType;

  static inline ChildIteratorType child_begin(NodeType *N) {
    return map_iterator(N->begin(), CGNDeref);
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return map_iterator(N->end(), CGNDeref);
  }
};

template <>
struct GraphTraits<PTACallGraph const *>
    : public GraphTraits<PTACallGraphNode const *> {
  static NodeType *getEntryNode(PTACallGraph const *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }
};
} // namespace llvm

#endif /* PTACALLGRAPH_H */
