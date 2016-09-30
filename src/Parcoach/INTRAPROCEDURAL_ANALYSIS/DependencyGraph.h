#ifndef DEPENDENCYGRAPH
#define DEPENDENCYGRAPH

#include "GlobalGraph.h"

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

class DependencyGraph {
  friend class GlobalGraph;

 public:
  enum color {
    WHITE,
    BLACK
  };

  enum type {
    MustAlias,
    MayAlias,
    PartialAlias,
    ValueDep
  };

  struct Node {
    color c;
    llvm::DenseSet<llvm::MemoryLocation> children;
  };

  DependencyGraph();
  ~DependencyGraph();

  bool addRoot(llvm::MemoryLocation ML);
  bool addEdge(llvm::MemoryLocation src, llvm::MemoryLocation dst,
	       enum type depType);
  void addBarrierEdge(llvm::MemoryLocation ML,
		      const llvm::Instruction *barrier);

  void debug();
  void toDot(llvm::StringRef filename);
  void toDot(llvm::raw_fd_ostream &stream);

  typedef llvm::DenseMap<llvm::MemoryLocation, Node *>::iterator EdgeIterator;
  typedef llvm::DenseSet<llvm::MemoryLocation>::iterator RootIterator;
  typedef llvm::DenseSet<llvm::MemoryLocation>::iterator NodeIterator;
  typedef llvm::DenseSet<llvm::MemoryLocation>::iterator ChildrenIterator;

  NodeIterator nodeBegin();
  NodeIterator nodeEnd();

 private:
  llvm::DenseSet<llvm::MemoryLocation> roots;
  llvm::DenseSet<llvm::MemoryLocation> nodes;

  llvm::DenseMap<llvm::MemoryLocation, Node *> mustAliasEdges;
  llvm::DenseMap<llvm::MemoryLocation, Node *> mayAliasEdges;
  llvm::DenseMap<llvm::MemoryLocation, Node *> partialAliasEdges;
  llvm::DenseMap<llvm::MemoryLocation, Node *> valueDepEdges;

  std::vector<const llvm::Instruction *> barrierNodes;
  llvm::DenseMap<llvm::MemoryLocation, std::vector<const llvm::Instruction *>>
		 barrierEdges;
};

#endif /* DEPENDENCYGRAPH */
