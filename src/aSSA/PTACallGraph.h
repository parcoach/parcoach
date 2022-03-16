#ifndef PTACALLGRAPH_H
#define PTACALLGRAPH_H

#include "andersen/Andersen.h"

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/ValueHandle.h"

#include "CallSite.h"

#include <map>
#include <set>

class PTACallGraphNode;

class PTACallGraph {
  llvm::Module &M;
  Andersen *AA;

  typedef std::map<const llvm::Function *, std::unique_ptr<PTACallGraphNode>>
      FunctionMapTy;

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
  void addToCallGraph(llvm::Function *F);

public:
  explicit PTACallGraph(llvm::Module &M, Andersen *AA);
  ~PTACallGraph();

  PTACallGraphNode *getEntry() const { return Root; }

  std::map<const llvm::Instruction *, std::set<const llvm::Function *>>
      indirectCallMap;

  typedef FunctionMapTy::iterator iterator;
  typedef FunctionMapTy::const_iterator const_iterator;

  /// \brief Returns the module the call graph corresponds to.
  llvm::Module &getModule() const { return M; }

  inline iterator begin() { return FunctionMap.begin(); }
  inline iterator end() { return FunctionMap.end(); }
  inline const_iterator begin() const { return FunctionMap.begin(); }
  inline const_iterator end() const { return FunctionMap.end(); }

  /// \brief Returns the call graph node for the provided function.
  inline const PTACallGraphNode *operator[](const llvm::Function *F) const {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second.get();
  }

  /// \brief Returns the call graph node for the provided function.
  inline PTACallGraphNode *operator[](const llvm::Function *F) {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second.get();
  }

  /// \brief Returns the \c CallGraphNode which is used to represent
  /// undetermined calls into the callgraph.
  PTACallGraphNode *getExternalCallingNode() const {
    return ExternalCallingNode;
  }

  PTACallGraphNode *getCallsExternalNode() const {
    return CallsExternalNode.get();
  }

  PTACallGraphNode *getOrInsertFunction(const llvm::Function *F);

  bool isReachableFromEntry(const llvm::Function *F) const;
};

class PTACallGraphNode {
public:
  typedef std::pair<llvm::WeakVH, PTACallGraphNode *> CallRecord;

  typedef std::vector<CallRecord> CalledFunctionsVector;

  inline PTACallGraphNode(llvm::Function *F) : F(F) {}

  ~PTACallGraphNode() {}

  typedef std::vector<CallRecord>::iterator iterator;
  typedef std::vector<CallRecord>::const_iterator const_iterator;

  /// \brief Returns the function that this call graph node represents.
  llvm::Function *getFunction() const { return F; }

  inline iterator begin() { return CalledFunctions.begin(); }
  inline iterator end() { return CalledFunctions.end(); }
  inline const_iterator begin() const { return CalledFunctions.begin(); }
  inline const_iterator end() const { return CalledFunctions.end(); }
  inline bool empty() const { return CalledFunctions.empty(); }
  inline unsigned size() const { return (unsigned)CalledFunctions.size(); }

  /// \brief Returns the i'th called function.
  PTACallGraphNode *operator[](unsigned i) const {
    assert(i < CalledFunctions.size() && "Invalid index");
    return CalledFunctions[i].second;
  }

  /// \brief Adds a function to the list of functions called by this one.
  void addCalledFunction(llvm::CallSite CS, PTACallGraphNode *M) {
    CalledFunctions.emplace_back(CS.getInstruction(), M);
  }

private:
  friend class PTACallGraph;

  llvm::AssertingVH<llvm::Function> F;

  std::vector<CallRecord> CalledFunctions;

  PTACallGraphNode(const PTACallGraphNode &) = delete;
  void operator=(const PTACallGraphNode &) = delete;
};

//===----------------------------------------------------------------------===//
// GraphTraits specializations for call graphs so that they can be treated as
// graphs by the generic graph algorithms.
//

// Provide graph traits for tranversing call graphs using standard graph
// traversals.

namespace llvm {

template <> struct GraphTraits<PTACallGraphNode *> {
  typedef PTACallGraphNode NodeType;
  typedef PTACallGraphNode *NodeRef;

  typedef PTACallGraphNode::CallRecord CGNPairTy;
  typedef std::pointer_to_unary_function<CGNPairTy, PTACallGraphNode *>
      CGNDerefFun;

  static NodeType *getEntryNode(PTACallGraphNode *CGN) { return CGN; }

  typedef mapped_iterator<NodeType::iterator, CGNDerefFun> ChildIteratorType;

  static inline ChildIteratorType child_begin(NodeType *N) {
    return map_iterator(N->begin(), CGNDerefFun(CGNDeref));
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return map_iterator(N->end(), CGNDerefFun(CGNDeref));
  }

  static PTACallGraphNode *CGNDeref(CGNPairTy P) { return P.second; }
};

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
struct GraphTraits<PTACallGraph *> : public GraphTraits<PTACallGraphNode *> {
  static NodeType *getEntryNode(PTACallGraph *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }
  typedef std::pair<const Function *const, std::unique_ptr<PTACallGraphNode>>
      PairTy;
  typedef std::pointer_to_unary_function<const PairTy &, PTACallGraphNode &>
      DerefFun;

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<PTACallGraph::iterator, DerefFun> nodes_iterator;
  static nodes_iterator nodes_begin(PTACallGraph *CG) {
    return map_iterator(CG->begin(), DerefFun(CGdereference));
  }
  static nodes_iterator nodes_end(PTACallGraph *CG) {
    return map_iterator(CG->end(), DerefFun(CGdereference));
  }

  static PTACallGraphNode &CGdereference(const PairTy &P) { return *P.second; }
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
}

class PTACallGraphSCC {
  const PTACallGraph &CG; // The call graph for this SCC.
  std::vector<PTACallGraphNode *> Nodes;

public:
  PTACallGraphSCC(PTACallGraph &cg) : CG(cg) {}

  void initialize(PTACallGraphNode *const *I, PTACallGraphNode *const *E) {
    Nodes.assign(I, E);
  }

  bool isSingular() const { return Nodes.size() == 1; }
  unsigned size() const { return Nodes.size(); }

  typedef std::vector<PTACallGraphNode *>::const_iterator iterator;
  iterator begin() const { return Nodes.begin(); }
  iterator end() const { return Nodes.end(); }

  const PTACallGraph &getCallGraph() { return CG; }
};

#endif /* PTACALLGRAPH_H */
