#ifndef INTRADEPGRAPH_H
#define INTRADEPGRAPH_H

#include "GlobalDepGraph.h"

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

/*
 * Class representing the dependencies inside a function as a directed graph.
 *
 * Roots are rank dependent arguments or memory location where the rank is
 * by a call to the MPI_Comm_rank() function.
 *
 * Nodes are memory locations whose value depend on the rank.
 *
 * Leafs are MPI barriers depending on rank.
 *
 * There if 4 types of dependencies:
 * - Alias dependencies (Must/May/Partial alias).
 * - Control flow dependencies.
 */

class IntraDepGraph {
  friend class GlobalDepGraph;

 public:

  enum type {
    MustAlias,
    MayAlias,
    PartialAlias,
    Flow,
  };

  // Set of memory memory locations.
  typedef llvm::DenseSet<llvm::MemoryLocation> NodeTy;
  // Set of directed edges with same source.
  typedef llvm::DenseMap<llvm::MemoryLocation, NodeTy *> EdgeMapTy;

  IntraDepGraph();
  ~IntraDepGraph();

  bool addRoot(llvm::MemoryLocation ML);
  bool addEdge(llvm::MemoryLocation src, llvm::MemoryLocation dst,
	       enum type depType);
  void addBarrierEdge(llvm::MemoryLocation ML,
		      const llvm::Instruction *barrier);

  void debug();
  void toDot(llvm::StringRef filename);
  void toDot(llvm::raw_fd_ostream &stream);

  NodeTy::iterator nodeBegin();
  NodeTy::iterator nodeEnd();

 private:
  NodeTy roots; // Set of roots (rank dependent memory locations) of the graph.
  NodeTy nodes;

  EdgeMapTy mustAliasEdges;
  EdgeMapTy mayAliasEdges;
  EdgeMapTy partialAliasEdges;
  EdgeMapTy flowEdges;

  typedef llvm::DenseSet<const llvm::Instruction *> LeafTy;
  LeafTy barriers;
  llvm::DenseMap<llvm::MemoryLocation, LeafTy *> barrierEdges;
};

#endif /* INTRADEPGRAPH_H */
